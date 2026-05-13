#ifndef PROJECT_H
#define PROJECT_H
#include "Std_Types.h"

#define Enter_Critical()  __asm volatile ("CPSID I" ::: "memory")
#define Exit_Critical()   __asm volatile ("CPSIE I" ::: "memory")

/* Direction values */
#define DIR_NONE   0U
#define DIR_UP     1U
#define DIR_DOWN   2U

/* Floor numbers */
#define FLOOR_1    1U
#define FLOOR_2    2U
#define FLOOR_3    3U
#define FLOOR_4    4U

/* IPC packet header magic byte */
#define IPC_HEADER  0xA5

/* Elevator FSM states */
typedef enum {
    ELEV_IDLE        = 0,
    ELEV_MOVING_UP   = 1,
    ELEV_MOVING_DOWN = 2,
    ELEV_DOORS_OPEN  = 3,
    ELEV_EMERGENCY   = 4
} ElevatorState_t;

/*
 * Core elevator data structure.
 * Shared between FSM, Dispatch, IPC, and Telemetry.
 * All fields must be declared volatile when used as globals
 * shared between ISRs and main loop.
 *
 */

/*  flags per elevator */
typedef struct {
    volatile uint8 floorReachedFlag;
    volatile uint8 floorReachedValue;
    volatile uint8 doorTimerFlag;
    volatile uint8 emergencyFlag;
    volatile uint8 newTargetFlag;
} FSM_Flags_t;


typedef struct {
    ElevatorState_t state;     /* Current FSM state                  */
    uint8 current_floor;       /* Floor the elevator is currently at  */
    uint8 target_floor;        /* Floor the elevator is heading to    */
    uint8 direction;           /* DIR_UP, DIR_DOWN, or DIR_NONE       */
    uint8 speed_percent;       /* 0, 20, or 100                       */
    FSM_Flags_t flags;

} Elevator_t;

/* RemoteState_t is identical in layout to Elevator_t.
 * Separate typedef to make it clear in function signatures
 * whether you are referring to the local or remote elevator. */
typedef Elevator_t RemoteState_t;

/* 8-byte SPI packet exchanged every 50ms */
/* Ensure the struct is exactly 8 bytes with no padding */
typedef struct __attribute__((packed)) {
    uint8 header;    /* Always 0xA5 — marks start of valid frame     */
    uint8 state;     /* ElevatorState_t cast to uint8                */
    uint8 floor;     /* current_floor of sender                      */
    uint8 direction; /* DIR_UP / DIR_DOWN / DIR_NONE                 */
    uint8 speed;     /* speed_percent of sender                      */
    uint8 flags;     /* Bit 0: emergency active. Bit 1: doors open.  */
    uint8 reserved;  /* Always 0x00, reserved for future use         */
    uint8 checksum;  /* XOR of bytes [0..6]                          */
} IPC_Packet_t;

#endif