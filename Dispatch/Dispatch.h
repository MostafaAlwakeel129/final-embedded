#ifndef DISPATCH_H
#define DISPATCH_H

#include "project.h"

#define HALL_CALL_MAX   6U

#define ASSIGNED_NONE   0U
#define ASSIGNED_A      1U
#define ASSIGNED_B      2U

typedef struct {
    uint8 floor;      /* FLOOR_1 to FLOOR_4    */
    uint8 direction;  /* DIR_UP or DIR_DOWN     */
    uint8 assigned;   /* ASSIGNED_NONE / A / B  */
} HallCall_t;


/* Initialize dispatch system and clear all pending calls */
void Dispatch_Init(void);


/* Register a new hall call (floor + direction) into pending queue */
void Dispatch_RegisterCall(uint8 floor, uint8 direction);


/* Run scheduling algorithm to assign calls to elevator A or B */
void Dispatch_RunAlgorithm(Elevator_t *elevA, RemoteState_t *elevB);


/* Mark a hall call as assigned to a specific elevator */
void Dispatch_MarkAssigned(uint8 floor, uint8 direction, uint8 assignedTo);


/* Remove a hall call from the pending queue */
void Dispatch_ClearCall(uint8 floor, uint8 direction);


/* Get number of pending (unassigned) hall calls */
uint8 Dispatch_GetPendingCount(void);

#endif