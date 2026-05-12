#include "Fsm.h"
#include "Pwm.h"
#include "Timer.h"
#include "Usart.h"

/* ================= PWM ================= */
#define FSM_PWM_PSC   15U
#define FSM_PWM_ARR   99U

#define TELEMETRY_PERIOD_MS 500U

static volatile uint32 s_telemetryMs = 0U;

/*
 * Module-level pointer to active elevator.
 * Set by FSM_Init so the trampoline can reach elev->flags
 * without needing an argument (Timer callback is void(*)(void)).
 */
static Elevator_t *s_elevPtr = (void*)0;

/* ================= Forward declaration ================= */
void FSM_DoorTimer_Trampoline(void);

/* ================= Helpers ================= */

static void SetMotorSpeed(Elevator_t *elev, uint8 speed)
{
    elev->speed_percent = speed;
    Pwm_SetDutyPercent(FSM_MOTOR_TIMER, FSM_MOTOR_CHANNEL, speed);
}

static void StartDoorTimer(void)
{
    /* No extern needed — FSM_DoorTimer_Trampoline declared above */
    Timer_DelayMsAsync(FSM_DOOR_TIMER, DOOR_OPEN_MS,
                       FSM_DoorTimer_Trampoline);
}

static void StopDoorTimer(void)
{
    Timer_Stop(FSM_DOOR_TIMER);
}

static const char* StateStr(ElevatorState_t s)
{
    switch(s)
    {
        case ELEV_IDLE:         return "IDLE";
        case ELEV_MOVING_UP:    return "MOVING_UP";
        case ELEV_MOVING_DOWN:  return "MOVING_DOWN";
        case ELEV_DOORS_OPEN:   return "DOORS_OPEN";
        case ELEV_EMERGENCY:    return "EMERGENCY";
        default:                return "UNKNOWN";
    }
}

/* Fixed: "0"+elev->current_floor was pointer arithmetic (UB) */
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
    /* reverse */
    uint8 i = 0U, j = (uint8)(len - 1U);
    while (i < j) { char t = buf[i]; buf[i]=buf[j]; buf[j]=t; i++; j--; }
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

/* ================= INIT ================= */

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

    /* Store pointer so trampoline can reach flags */
    s_elevPtr = elev;

    Pwm_Init(FSM_MOTOR_TIMER, FSM_MOTOR_CHANNEL, FSM_PWM_PSC, FSM_PWM_ARR);
    SetMotorSpeed(elev, SPEED_STOP);
    Pwm_Start(FSM_MOTOR_TIMER, FSM_MOTOR_CHANNEL);

    Timer_Init(FSM_DOOR_TIMER, 5333U, (uint16)(DOOR_OPEN_MS - 1U));
}

/* ================= UPDATE ================= */

void FSM_Update(Elevator_t *elev)
{
    /* Telemetry — non-blocking, gated by tick counter */
    if (s_telemetryMs >= TELEMETRY_PERIOD_MS)
    {
        s_telemetryMs = 0U;
        SendTelemetry(elev);
    }

    /* Emergency — highest priority, overrides everything */
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

    switch (elev->state)
    {
        /* -------------------------------------------------- */
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
                    /* Already at target — open doors immediately */
                    elev->state     = ELEV_DOORS_OPEN;
                    elev->direction = DIR_NONE;
                    SetMotorSpeed(elev, SPEED_STOP);
                    StartDoorTimer();
                }
            }
            break;
        }

        /* -------------------------------------------------- */
        case ELEV_MOVING_UP:
        case ELEV_MOVING_DOWN:
        {
            if (elev->flags.floorReachedFlag)
            {
                /* Atomic read of ISR-written value */
                __asm volatile ("CPSID I" ::: "memory");
                uint8 reached = elev->flags.floorReachedValue;
                elev->flags.floorReachedFlag = 0U;
                __asm volatile ("CPSIE I" ::: "memory");

                elev->current_floor = reached;

                if (reached == elev->target_floor)
                {
                    /* PWM ramp: brief slow then full stop */
                    SetMotorSpeed(elev, SPEED_SLOW);
                    SetMotorSpeed(elev, SPEED_STOP);
                    elev->state     = ELEV_DOORS_OPEN;
                    elev->direction = DIR_NONE;
                    StartDoorTimer();
                }
                else
                {
                    /* Intermediate floor — keep direction, full speed */
                    elev->direction = (elev->state == ELEV_MOVING_UP)
                                      ? DIR_UP : DIR_DOWN;
                    SetMotorSpeed(elev, SPEED_FULL);
                }
            }
            break;
        }

        /* -------------------------------------------------- */
        case ELEV_DOORS_OPEN:
        {
            if (elev->flags.doorTimerFlag)
            {
                elev->flags.doorTimerFlag = 0U;
                elev->state     = ELEV_IDLE;
                elev->direction = DIR_NONE;
                SetMotorSpeed(elev, SPEED_STOP);
            }
            break;
        }

        /* -------------------------------------------------- */
        case ELEV_EMERGENCY:
            break;

        default:
            break;
    }
}

/* ================= API ================= */

void FSM_SetTarget(Elevator_t *elev, uint8 floor)
{
    if (floor < FLOOR_1 || floor > FLOOR_4) return;
    if (elev->state == ELEV_EMERGENCY)      return;

    __asm volatile ("CPSID I" ::: "memory");
    elev->target_floor        = floor;
    elev->flags.newTargetFlag = 1U;
    __asm volatile ("CPSIE I" ::: "memory");
}

void FSM_FloorReached(Elevator_t *elev, uint8 floor)
{
    /* Called from EXTI ISR — atomic two-step write */
    __asm volatile ("CPSID I" ::: "memory");
    elev->current_floor           = floor;
    elev->flags.floorReachedValue = floor;
    elev->flags.floorReachedFlag  = 1U;
    __asm volatile ("CPSIE I" ::: "memory");
}

void FSM_EmergencyStop(Elevator_t *elev)
{
    /* Called from highest-priority EXTI ISR — stop PWM instantly */
    elev->flags.emergencyFlag = 1U;
    elev->state               = ELEV_EMERGENCY;
    elev->direction           = DIR_NONE;
    elev->speed_percent       = SPEED_STOP;
    Pwm_SetDutyPercent(FSM_MOTOR_TIMER, FSM_MOTOR_CHANNEL, SPEED_STOP);
    Usart1_TransmitString("ALERT: Emergency Activated\r\n");
}

void FSM_ClearEmergency(Elevator_t *elev)
{
    __asm volatile ("CPSID I" ::: "memory");
    elev->flags.emergencyFlag = 0U;
    elev->flags.newTargetFlag = 0U;
    __asm volatile ("CPSIE I" ::: "memory");

    elev->state         = ELEV_IDLE;
    elev->direction     = DIR_NONE;
    elev->speed_percent = SPEED_STOP;
    elev->target_floor  = elev->current_floor;
}

void FSM_DoorTimerExpired(Elevator_t *elev)
{
    /* Called from TIMER3 ISR — set flag only, no state change */
    (void)elev;
    if (s_elevPtr != (void*)0)
    {
        s_elevPtr->flags.doorTimerFlag = 1U;
    }
}

/*
 * Trampoline — void(*)(void) signature matches TimerCallback.
 * Timer_DelayMsAsync stores this and calls it from TIMER3 IRQHandler.
 * Reaches elevator flags via s_elevPtr set in FSM_Init.
 */
void FSM_DoorTimer_Trampoline(void)
{
    if (s_elevPtr != (void*)0)
    {
        s_elevPtr->flags.doorTimerFlag = 1U;
    }
}

uint8 FSM_IsIdle(Elevator_t *elev)
{
    return ((elev->state == ELEV_IDLE) &&
            (elev->flags.newTargetFlag == 0U)) ? 1U : 0U;
}

void FSM_TickMs(void)
{
    s_telemetryMs++;
}