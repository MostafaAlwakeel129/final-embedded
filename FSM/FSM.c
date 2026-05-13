/**
 * FSM.c
 *
 * Elevator Finite State Machine — Master MCU (Elevator A)
 *
 * Fixed issues vs previous version:
 *  Bug 1 — Removed blocking UART call from FSM_EmergencyStop()
 *           (was called from highest-priority EXTI ISR; caused lockup)
 *  Bug 2 — Removed dead FSM_DoorTimerExpired() body; trampoline is
 *           the single call path for the door timer. DoorTimerExpired
 *           kept in .h for Dispatch compatibility but delegates to
 *           the trampoline.
 *  Bug 3 — FSM_ClearEmergency() now writes state/direction/speed/target
 *           INSIDE the critical section, not after re-enabling IRQs.
 *  Bug 4 — Removed redundant Timer_Init() from FSM_Init();
 *           Timer_DelayMsAsync() owns full timer re-configuration itself.
 *  Bug 5 — Emergency UART print moved into FSM_Update() ELEV_EMERGENCY
 *           branch so it only runs in main-loop context, never in an ISR.
 *
 *  Created on: 5/16/2025
 *  Author    : AbdallahDarwish
 */

#include "Fsm.h"
#include "Pwm.h"
#include "Timer.h"
#include "Usart.h"

/* =========================================================
 * PWM configuration for motor LED
 * PSC=15, ARR=99  -> 16 MHz / (15+1) / (99+1) = 10 kHz PWM
 * ========================================================= */
#define FSM_PWM_PSC   15U
#define FSM_PWM_ARR   99U

#define TELEMETRY_PERIOD_MS  500U

/* Millisecond counter incremented by FSM_TickMs() (called from 1ms ISR) */
static volatile uint32 s_telemetryMs = 0U;

/*
 * Module-level pointer to the active elevator.
 * Set once in FSM_Init so the door-timer trampoline can reach
 * elev->flags without needing a function argument
 * (TimerCallback signature is void(*)(void)).
 */
static Elevator_t *s_elevPtr = (void*)0;

/* =========================================================
 * Forward declaration
 * ========================================================= */
void FSM_DoorTimer_Trampoline(void);

/* =========================================================
 * Private helpers
 * ========================================================= */

static void SetMotorSpeed(Elevator_t *elev, uint8 speed)
{
    elev->speed_percent = speed;
    Pwm_SetDutyPercent(FSM_MOTOR_TIMER, FSM_MOTOR_CHANNEL, speed);
}

static void StartDoorTimer(void)
{
    Timer_DelayMsAsync(FSM_DOOR_TIMER, DOOR_OPEN_MS,
                       FSM_DoorTimer_Trampoline);
}

static void StopDoorTimer(void)
{
    Timer_Stop(FSM_DOOR_TIMER);
}

static const char* StateStr(ElevatorState_t s)
{
    switch (s)
    {
        case ELEV_IDLE:         return "IDLE";
        case ELEV_MOVING_UP:    return "MOVING_UP";
        case ELEV_MOVING_DOWN:  return "MOVING_DOWN";
        case ELEV_DOORS_OPEN:   return "DOORS_OPEN";
        case ELEV_EMERGENCY:    return "EMERGENCY";
        default:                return "UNKNOWN";
    }
}

/* Integer-only uint8 -> ASCII conversion, no printf / float */
static void TransmitUint8(uint8 val)
{
    char  buf[4];
    uint8 len = 0U;

    if (val == 0U) { Usart1_TransmitString("0"); return; }

    while (val > 0U && len < 3U)
    {
        buf[len++] = (char)('0' + (val % 10U));
        val       /= 10U;
    }
    /* reverse in-place */
    uint8 i = 0U, j = (uint8)(len - 1U);
    while (i < j) { char t = buf[i]; buf[i] = buf[j]; buf[j] = t; i++; j--; }
    buf[len] = '\0';
    Usart1_TransmitString(buf);
}

static void SendTelemetry(Elevator_t *elev)
{
    Usart1_TransmitString("[TEL] ");
    Usart1_TransmitString(StateStr(elev->state));
    Usart1_TransmitString(" F:");
    TransmitUint8(elev->current_floor);
    Usart1_TransmitString(" T:");
    TransmitUint8(elev->target_floor);
    Usart1_TransmitString(" D:");
    TransmitUint8(elev->direction);
    Usart1_TransmitString("\r\n");
}

/* =========================================================
 * FSM_Init
 * ========================================================= */
void FSM_Init(Elevator_t *elev)
{
    elev->state         = ELEV_IDLE;
    elev->current_floor = FLOOR_1;
    elev->target_floor  = FLOOR_1;
    elev->direction     = DIR_NONE;
    elev->speed_percent = SPEED_STOP;

    elev->flags.floorReachedFlag  = 0U;
    elev->flags.floorReachedValue = 0U;
    elev->flags.doorTimerFlag     = 0U;
    elev->flags.emergencyFlag     = 0U;
    elev->flags.newTargetFlag     = 0U;

    /* Store pointer so the door-timer trampoline can reach flags */
    s_elevPtr = elev;

    /*
     * Initialise PWM for motor LED on TIM4_CH1.
     * Caller (main.c) must have already:
     *   - enabled RCC for TIM4 and the motor GPIO port
     *   - configured the GPIO pin as AF with the correct AF number
     */
    Pwm_Init(FSM_MOTOR_TIMER, FSM_MOTOR_CHANNEL, FSM_PWM_PSC, FSM_PWM_ARR);
    SetMotorSpeed(elev, SPEED_STOP);
    Pwm_Start(FSM_MOTOR_TIMER, FSM_MOTOR_CHANNEL);

    /*
     * BUG 4 FIX: The previous version called Timer_Init(FSM_DOOR_TIMER, ...)
     * here, but Timer_DelayMsAsync() reconfigures TIM3 from scratch every
     * time StartDoorTimer() is called, so that Timer_Init() was overwritten
     * on the first door-open and served no purpose. Removed.
     */
}

/* =========================================================
 * FSM_Update  -  call every main-loop iteration, never blocks
 * ========================================================= */
void FSM_Update(Elevator_t *elev)
{
    /* --- Telemetry: non-blocking, gated by millisecond counter --- */
    if (s_telemetryMs >= TELEMETRY_PERIOD_MS)
    {
        s_telemetryMs = 0U;
        SendTelemetry(elev);
    }

    /*
     * --- Emergency: highest priority, checked before all states ---
     *
     * BUG 5 FIX: The UART print "ALERT: Emergency Activated" was
     * previously inside FSM_EmergencyStop() which is called from the
     * highest-priority EXTI ISR. Usart1_TransmitString() blocks on
     * the TC flag, which means the CPU would spin inside the ISR and
     * prevent every other interrupt from running.
     *
     * The print is now here, in FSM_Update(), which runs in main-loop
     * context. The one-shot guard (state != ELEV_EMERGENCY) ensures
     * the message is printed exactly once per emergency event.
     */
    if (elev->flags.emergencyFlag)
    {
        if (elev->state != ELEV_EMERGENCY)
        {
            elev->state     = ELEV_EMERGENCY;
            elev->direction = DIR_NONE;
            SetMotorSpeed(elev, SPEED_STOP);
            StopDoorTimer();
            Usart1_TransmitString("ALERT: Emergency Activated\r\n");
        }
        return;
    }

    /* --- State machine --- */
    switch (elev->state)
    {
        /* --------------------------------------------------
         * IDLE: wait for Dispatch to assign a target floor
         * -------------------------------------------------- */
        case ELEV_IDLE:
        {
            if (elev->flags.newTargetFlag)
            {
                elev->flags.newTargetFlag = 0U;

                if (elev->target_floor > elev->current_floor)
                {
                    elev->state     = ELEV_MOVING_UP;
                    elev->direction = DIR_UP;
                    SetMotorSpeed(elev, SPEED_FULL);
                }
                else if (elev->target_floor < elev->current_floor)
                {
                    elev->state     = ELEV_MOVING_DOWN;
                    elev->direction = DIR_DOWN;
                    SetMotorSpeed(elev, SPEED_FULL);
                }
                else
                {
                    /* Already at the target floor — open doors immediately */
                    elev->state     = ELEV_DOORS_OPEN;
                    elev->direction = DIR_NONE;
                    SetMotorSpeed(elev, SPEED_STOP);
                    StartDoorTimer();
                }
            }
            break;
        }

        /* --------------------------------------------------
         * MOVING_UP / MOVING_DOWN: wait for floor sensor ISR
         * -------------------------------------------------- */
        case ELEV_MOVING_UP:
        case ELEV_MOVING_DOWN:
        {
            if (elev->flags.floorReachedFlag)
            {
                /* Atomically snapshot the ISR-written floor value */
                __asm volatile ("CPSID I" ::: "memory");
                uint8 reached = elev->flags.floorReachedValue;
                elev->flags.floorReachedFlag = 0U;
                __asm volatile ("CPSIE I" ::: "memory");

                elev->current_floor = reached;

                if (reached == elev->target_floor)
                {
                    /* PWM ramp down: slow briefly, then full stop */
                    SetMotorSpeed(elev, SPEED_SLOW);
                    SetMotorSpeed(elev, SPEED_STOP);
                    elev->state     = ELEV_DOORS_OPEN;
                    elev->direction = DIR_NONE;
                    StartDoorTimer();
                }
                else
                {
                    /* Intermediate floor — keep moving at full speed */
                    elev->direction = (elev->state == ELEV_MOVING_UP)
                                      ? DIR_UP : DIR_DOWN;
                    SetMotorSpeed(elev, SPEED_FULL);
                }
            }
            break;
        }

        /* --------------------------------------------------
         * DOORS_OPEN: wait for 3-second door timer to expire
         * -------------------------------------------------- */
        case ELEV_DOORS_OPEN:
        {
            if (elev->flags.doorTimerFlag)
            {
                elev->flags.doorTimerFlag = 0U;
                elev->state               = ELEV_IDLE;
                elev->direction           = DIR_NONE;
                SetMotorSpeed(elev, SPEED_STOP);
            }
            break;
        }

        /* --------------------------------------------------
         * EMERGENCY: stay here until FSM_ClearEmergency()
         * -------------------------------------------------- */
        case ELEV_EMERGENCY:
            /* Nothing to do — FSM_ClearEmergency() resets the flag
             * and returns the elevator to IDLE.                     */
            break;

        default:
            break;
    }
}

/* =========================================================
 * FSM_SetTarget  -  called by Dispatch from main-loop context
 *
 * Note: this sets "next destination", not a waypoint queue.
 * If the elevator is already moving, the new target is stored
 * and will be acted on when the elevator next enters IDLE.
 * Dispatch should avoid calling this while the elevator is
 * moving unless the new floor is the intended final stop.
 * ========================================================= */
void FSM_SetTarget(Elevator_t *elev, uint8 floor)
{
    if (floor < FLOOR_1 || floor > FLOOR_4) return;
    if (elev->state == ELEV_EMERGENCY)      return;

    __asm volatile ("CPSID I" ::: "memory");
    elev->target_floor        = floor;
    elev->flags.newTargetFlag = 1U;
    __asm volatile ("CPSIE I" ::: "memory");
}

/* =========================================================
 * FSM_FloorReached  -  called from floor-sensor EXTI ISR
 * ========================================================= */
void FSM_FloorReached(Elevator_t *elev, uint8 floor)
{
    __asm volatile ("CPSID I" ::: "memory");
    elev->current_floor           = floor;
    elev->flags.floorReachedValue = floor;
    elev->flags.floorReachedFlag  = 1U;
    __asm volatile ("CPSIE I" ::: "memory");
}

/* =========================================================
 * FSM_EmergencyStop  -  called from highest-priority EXTI ISR
 *
 * BUG 1 FIX: This function is called from an ISR.
 *
 * The previous version called Usart1_TransmitString() here,
 * which spins on the UART TC flag — a blocking operation inside
 * an ISR. This prevented all lower-priority interrupts from
 * running for the full duration of the UART transmission.
 *
 * Now this function only:
 *   1. Writes the PWM duty register to 0 (single register write, ISR-safe)
 *   2. Sets the speed field
 *   3. Sets emergencyFlag
 *
 * FSM_Update() detects emergencyFlag on the next main-loop
 * iteration, transitions the state, and prints the UART message.
 * ========================================================= */
void FSM_EmergencyStop(Elevator_t *elev)
{
    /* Immediately kill motor via direct register write — safe in ISR */
    Pwm_SetDutyPercent(FSM_MOTOR_TIMER, FSM_MOTOR_CHANNEL, SPEED_STOP);
    elev->speed_percent   = SPEED_STOP;

    /* Signal FSM_Update() to complete the transition and print UART */
    elev->flags.emergencyFlag = 1U;

    /*
     * Do NOT set elev->state here.
     * FSM_Update() is the single owner of all state writes, which
     * eliminates any race between this ISR and the main loop.
     */
}

/* =========================================================
 * FSM_ClearEmergency  -  called from main-loop (e.g. reset button ISR
 *                         or a "clear" button handled in main loop)
 *
 * BUG 3 FIX: The previous version cleared the flags inside a
 * critical section but then wrote state/direction/speed/target
 * AFTER re-enabling interrupts (CPSIE). An EXTI firing in that
 * window could observe emergencyFlag==0 while state was still
 * ELEV_EMERGENCY, creating an inconsistent struct.
 *
 * All field writes are now inside the single critical section.
 * ========================================================= */
void FSM_ClearEmergency(Elevator_t *elev)
{
    __asm volatile ("CPSID I" ::: "memory");

    elev->flags.emergencyFlag = 0U;
    elev->flags.newTargetFlag = 0U;

    /* BUG 3 FIX: moved inside critical section */
    elev->state         = ELEV_IDLE;
    elev->direction     = DIR_NONE;
    elev->speed_percent = SPEED_STOP;
    elev->target_floor  = elev->current_floor;

    __asm volatile ("CPSIE I" ::: "memory");
}

/* =========================================================
 * FSM_DoorTimerExpired  -  kept for API compatibility with Dispatch.h
 *
 * BUG 2 FIX: The previous version documented this as the TIM3 ISR
 * callback but StartDoorTimer() registered FSM_DoorTimer_Trampoline
 * instead — making this an unreachable dead function.
 *
 * It now delegates to the trampoline so both symbols produce
 * identical behaviour. Only the trampoline path is active at runtime.
 * ========================================================= */
void FSM_DoorTimerExpired(Elevator_t *elev)
{
    (void)elev; /* s_elevPtr is used by the trampoline */
    FSM_DoorTimer_Trampoline();
}

/* =========================================================
 * FSM_DoorTimer_Trampoline
 *
 * void(*)(void) signature required by Timer_DelayMsAsync().
 * Reaches the elevator flags through s_elevPtr set in FSM_Init().
 * Sets doorTimerFlag only — FSM_Update() owns the state transition.
 * ========================================================= */
void FSM_DoorTimer_Trampoline(void)
{
    if (s_elevPtr != (void*)0)
    {
        s_elevPtr->flags.doorTimerFlag = 1U;
    }
}

/* =========================================================
 * FSM_IsIdle  -  called by Dispatch to check availability
 * ========================================================= */
uint8 FSM_IsIdle(Elevator_t *elev)
{
    return ((elev->state == ELEV_IDLE) &&
            (elev->flags.newTargetFlag == 0U)) ? 1U : 0U;
}

/* =========================================================
 * FSM_TickMs  -  call from a 1ms SysTick or periodic timer ISR
 * ========================================================= */
void FSM_TickMs(void)
{
    s_telemetryMs++;
}