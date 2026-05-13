/**
 * FSM.c
 *
 * Elevator Finite State Machine — Upgraded with Floor Queue
 */

#include "Fsm.h"
#include "Pwm.h"
#include "Timer.h"
#include "Usart.h"

#define FSM_PWM_PSC   15U
#define FSM_PWM_ARR   99U

static Elevator_t *s_elevPtr = (void*)0;

/* ---> NEW: Queue to track multiple destinations (0 is unused, 1-4 are floors) <--- */
static volatile uint8 s_stops[5] = {0U, 0U, 0U, 0U, 0U}; 

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

/* =========================================================
 * FSM Core Functions
 * ========================================================= */

void FSM_Init(Elevator_t *elev)
{
    uint8 i;
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

    /* Clear queue */
    for(i=0; i<5; i++){ s_stops[i] = 0U; }

    s_elevPtr = elev;

    Pwm_Init(FSM_MOTOR_TIMER, FSM_MOTOR_CHANNEL, FSM_PWM_PSC, FSM_PWM_ARR);
    SetMotorSpeed(elev, SPEED_STOP);
    Pwm_Start(FSM_MOTOR_TIMER, FSM_MOTOR_CHANNEL);
}

void FSM_Update(Elevator_t *elev)
{
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
        case ELEV_IDLE:
        {
            uint8 next_target = 0U;
            uint8 i;

            /* Sweep Logic: Find the next stop based on current direction */
            if (elev->direction == DIR_UP || elev->direction == DIR_NONE) {
                for (i = elev->current_floor; i <= FLOOR_4; i++) {
                    if (s_stops[i]) { next_target = i; break; }
                }
                if (!next_target) {
                    for (i = elev->current_floor; i >= FLOOR_1; i--) {
                        if (s_stops[i]) { next_target = i; elev->direction = DIR_DOWN; break; }
                    }
                } else { elev->direction = DIR_UP; }
            } else { /* DIR_DOWN */
                for (i = elev->current_floor; i >= FLOOR_1; i--) {
                    if (s_stops[i]) { next_target = i; break; }
                }
                if (!next_target) {
                    for (i = elev->current_floor; i <= FLOOR_4; i++) {
                        if (s_stops[i]) { next_target = i; elev->direction = DIR_UP; break; }
                    }
                } else { elev->direction = DIR_DOWN; }
            }

            if (next_target != 0U)
            {
                elev->target_floor = next_target;
                elev->flags.newTargetFlag = 0U;

                if (elev->target_floor > elev->current_floor)
                {
                    elev->state = ELEV_MOVING_UP;
                    uint8 dist = elev->target_floor - elev->current_floor;
                    SetMotorSpeed(elev, (dist == 1U) ? SPEED_SLOW : SPEED_FULL);
                }
                else if (elev->target_floor < elev->current_floor)
                {
                    elev->state = ELEV_MOVING_DOWN;
                    uint8 dist = elev->current_floor - elev->target_floor;
                    SetMotorSpeed(elev, (dist == 1U) ? SPEED_SLOW : SPEED_FULL);
                }
                else
                {
                    /* Already at target floor */
                    s_stops[elev->current_floor] = 0U;
                    elev->state     = ELEV_DOORS_OPEN;
                    SetMotorSpeed(elev, SPEED_STOP);
                    StartDoorTimer();
                }
            }
            else
            {
                elev->direction = DIR_NONE;
                elev->flags.newTargetFlag = 0U;
            }
            break;
        }

        case ELEV_MOVING_UP:
        case ELEV_MOVING_DOWN:
        {
            if (elev->flags.floorReachedFlag)
            {
                Enter_Critical();
                uint8 reached = elev->flags.floorReachedValue;
                elev->flags.floorReachedFlag = 0U;
                Exit_Critical();

                elev->current_floor = reached;

                /* ---> Check the queue: Is this an intermediate stop? <--- */
                if (s_stops[reached] == 1U || reached == elev->target_floor)
                {
                    s_stops[reached] = 0U; /* Remove from queue */
                    SetMotorSpeed(elev, SPEED_SLOW);
                    SetMotorSpeed(elev, SPEED_STOP);
                    elev->state = ELEV_DOORS_OPEN;
                    /* NOTE: We do NOT clear direction here, so IDLE knows to continue the sweep! */
                    StartDoorTimer();
                }
                else
                {
                    SetMotorSpeed(elev, SPEED_FULL);
                }
            }
            break;
        }

        case ELEV_DOORS_OPEN:
        {
            if (elev->flags.doorTimerFlag)
            {
                elev->flags.doorTimerFlag = 0U;
                elev->state               = ELEV_IDLE;
                SetMotorSpeed(elev, SPEED_STOP);
            }
            break;
        }

        case ELEV_EMERGENCY:
            break;

        default:
            break;
    }
}

void FSM_SetTarget(Elevator_t *elev, uint8 floor)
{
    if (floor < FLOOR_1 || floor > FLOOR_4) return;
    if (elev->state == ELEV_EMERGENCY)      return;

    /* ---> Add stop to queue instead of overwriting current target <--- */
    Enter_Critical();
    s_stops[floor] = 1U;
    elev->flags.newTargetFlag = 1U; /* Triggers IDLE to re-evaluate */
    Exit_Critical();
}

void FSM_FloorReached(Elevator_t *elev, uint8 floor)
{
    Enter_Critical();
    elev->current_floor           = floor;
    elev->flags.floorReachedValue = floor;
    elev->flags.floorReachedFlag  = 1U;
    Exit_Critical();
}

void FSM_EmergencyStop(Elevator_t *elev)
{
    Pwm_SetDutyPercent(FSM_MOTOR_TIMER, FSM_MOTOR_CHANNEL, SPEED_STOP);
    elev->speed_percent       = SPEED_STOP;
    elev->flags.emergencyFlag = 1U;
}

void FSM_ClearEmergency(Elevator_t *elev)
{
    Enter_Critical();
    elev->flags.emergencyFlag = 0U;
    elev->flags.newTargetFlag = 0U;

    s_stops[1] = 0U; s_stops[2] = 0U; s_stops[3] = 0U; s_stops[4] = 0U;

    elev->state         = ELEV_IDLE;
    elev->direction     = DIR_NONE;
    elev->speed_percent = SPEED_STOP;
    elev->target_floor  = elev->current_floor;
    Exit_Critical();
}

void FSM_DoorTimerExpired(Elevator_t *elev)
{
    (void)elev; 
    FSM_DoorTimer_Trampoline();
}

void FSM_DoorTimer_Trampoline(void)
{
    if (s_elevPtr != (void*)0)
    {
        s_elevPtr->flags.doorTimerFlag = 1U;
    }
}

uint8 FSM_IsIdle(Elevator_t *elev)
{
    /* Only truly idle if the state is IDLE and the queue is completely empty */
    uint8 has_stops = s_stops[1] | s_stops[2] | s_stops[3] | s_stops[4];
    return ((elev->state == ELEV_IDLE) && (has_stops == 0U)) ? 1U : 0U;
}