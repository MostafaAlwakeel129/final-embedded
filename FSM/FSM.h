#ifndef FSM_H
#define FSM_H

#include "project.h"

#define SPEED_STOP   0U
#define SPEED_SLOW   20U
#define SPEED_FULL   100U

#define DOOR_OPEN_MS 3000U

/* Timer and PWM resources used by FSM — agree with main.c */
#define FSM_DOOR_TIMER     TIMER3
#define FSM_MOTOR_TIMER    TIMER4
#define FSM_MOTOR_CHANNEL  CH1

/*
 * FSM_Init
 * Sets elevator to ELEV_IDLE at floor 1, direction NONE,
 * speed 0. Initialises PWM for motor LED.
 * Does NOT register any EXTI callbacks — that is done
 * by the application in main.c which then calls
 * FSM_FloorReached and FSM_EmergencyStop.
 *
 * IN:  elev — pointer to Elevator_t to initialise
 * OUT: elev — all fields set to default safe state
 */
void FSM_Init(Elevator_t *elev);

/*
 * FSM_Update
 * Main FSM tick. Call every iteration of the main loop.
 * Checks internal flags set by ISRs and drives state
 * transitions. Never blocks.
 *
 * Transitions handled:
 *   IDLE        + target assigned     → MOVING_UP or MOVING_DOWN
 *   MOVING_UP   + floor reached       → check if target floor
 *                                        YES → DOORS_OPEN
 *                                        NO  → stay MOVING_UP
 *   MOVING_DOWN + floor reached       → same logic as above
 *   DOORS_OPEN  + door timer expired  → IDLE
 *   ANY STATE   + emergency flag set  → ELEV_EMERGENCY
 *
 * IN:  elev — pointer to active Elevator_t
 * OUT: elev — state, direction, speed fields updated
 */
void FSM_Update(Elevator_t *elev);

/*
 * FSM_SetTarget
 * Called by Dispatch to assign a destination floor.
 * If elevator is IDLE, immediately sets target_floor and
 * raises an internal flag so FSM_Update starts moving
 * on its next call.
 * If elevator is already MOVING and target is on the way,
 * updates target_floor silently.
 * Ignored if elevator is in EMERGENCY state.
 *
 * IN:  elev  — pointer to active Elevator_t
 * IN:  floor — FLOOR_1..FLOOR_4
 * OUT: elev  — target_floor updated
 */
void FSM_SetTarget(Elevator_t *elev, uint8 floor);

/*
 * FSM_FloorReached
 * Called from floor sensor EXTI ISR.
 * Updates elev->current_floor.
 * Sets an internal flag that FSM_Update checks next cycle.
 * Does NOT change state directly — FSM_Update does that.
 *
 * IN:  elev  — pointer to active Elevator_t
 * IN:  floor — floor number detected by sensor (FLOOR_1..4)
 * OUT: elev  — current_floor updated
 */
void FSM_FloorReached(Elevator_t *elev, uint8 floor);

/*
 * FSM_EmergencyStop
 * Called from emergency button EXTI ISR (highest priority).
 * Immediately sets state to ELEV_EMERGENCY and speed to 0.
 * Stops PWM output instantly.
 * Sets a flag so FSM_Update stays in EMERGENCY until
 * FSM_ClearEmergency is called.
 *
 * IN:  elev — pointer to active Elevator_t
 * OUT: elev — state = ELEV_EMERGENCY, speed = 0
 */
void FSM_EmergencyStop(Elevator_t *elev);

/*
 * FSM_ClearEmergency
 * Resets the emergency flag and returns elevator to IDLE.
 * In a real system this would require a key switch —
 * here it is called manually for testing.
 *
 * IN:  elev — pointer to active Elevator_t
 * OUT: elev — state = ELEV_IDLE
 */
void FSM_ClearEmergency(Elevator_t *elev);

/*
 * FSM_DoorTimerExpired
 * Called from TIMER3 ISR after DOOR_OPEN_MS (3 seconds).
 * Sets an internal flag so FSM_Update transitions
 * from DOORS_OPEN back to IDLE on the next cycle.
 *
 * IN:  elev — pointer to active Elevator_t
 * OUT: nothing (sets internal flag only)
 */
void FSM_DoorTimerExpired(Elevator_t *elev);

/*
 * FSM_IsIdle
 * Returns 1 if elevator state is ELEV_IDLE and no
 * target is pending. Used by Dispatch to check availability.
 *
 * IN:  elev — pointer to Elevator_t to check
 * RET: 1 = idle and available, 0 = busy
 */
uint8 FSM_IsIdle(Elevator_t *elev);


void FSM_TickMs(void);

#endif /* FSM_H */