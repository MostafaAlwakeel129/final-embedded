#include "Dispatch.h"
#include "Fsm.h"
#include "Ipc.h"

extern void IPC_SendTargetFloor(uint8 floor);

#define HALL_CALL_MAX   6U

#define ASSIGNED_NONE   0U
#define ASSIGNED_A      1U
#define ASSIGNED_B      2U

static HallCall_t s_pendingCalls[HALL_CALL_MAX];

static uint8 Dispatch_IsValidCall(uint8 floor, uint8 direction)
{
    if (floor < FLOOR_1 || floor > FLOOR_4)
    {
        return 0U;
    }

    if (floor == FLOOR_1)
    {
        return (direction == DIR_UP) ? 1U : 0U;
    }

    if (floor == FLOOR_4)
    {
        return (direction == DIR_DOWN) ? 1U : 0U;
    }

    return ((direction == DIR_UP) || (direction == DIR_DOWN)) ? 1U : 0U;
}

static uint8 Dispatch_FindCallIndex(uint8 floor, uint8 direction)
{
    uint8 i;
    for (i = 0U; i < HALL_CALL_MAX; ++i)
    {
        if ((s_pendingCalls[i].floor == floor) &&
            (s_pendingCalls[i].direction == direction)){return i;}
    }
    return HALL_CALL_MAX;
}

static uint8 Dispatch_FindFreeSlot(void)
{
    uint8 i;

    for (i = 0U; i < HALL_CALL_MAX; ++i)
    {
        if (s_pendingCalls[i].assigned == ASSIGNED_NONE)
        {
            return i;
        }
    }

    return HALL_CALL_MAX;
}

static uint8 Dispatch_AbsDiff(uint8 a, uint8 b)
{
    return (a >= b) ? (a - b) : (b - a);
}

static uint8 Dispatch_IsImmediateMatch(Elevator_t *elev, HallCall_t *call)
{
    return ((FSM_IsIdle(elev) != 0U) && (elev->current_floor == call->floor)) ? 1U : 0U;
}

static uint8 Dispatch_IsPerfectMatch(Elevator_t *elev, HallCall_t *call)
{
    if ((elev->state == ELEV_MOVING_UP) &&
        (call->direction == DIR_UP) &&
        (elev->current_floor <= call->floor)){return 1U;}

    if ((elev->state == ELEV_MOVING_DOWN) &&
        (call->direction == DIR_DOWN) &&
        (elev->current_floor >= call->floor)){return 1U;}

    return 0U;
}

static uint8 Dispatch_IsPassedMatch(Elevator_t *elev, HallCall_t *call)
{
    if ((elev->state == ELEV_MOVING_UP) &&
        (call->direction == DIR_UP) &&
        (elev->current_floor > call->floor))
    {
        return 1U;
    }

    if ((elev->state == ELEV_MOVING_DOWN) &&
        (call->direction == DIR_DOWN) &&
        (elev->current_floor < call->floor))
    {
        return 1U;
    }

    return 0U;
}

static uint8 Dispatch_IsOppositeDirection(Elevator_t *elev, HallCall_t *call)
{
    if ((elev->state == ELEV_MOVING_UP) && (call->direction == DIR_DOWN))
    {
        return 1U;
    }

    if ((elev->state == ELEV_MOVING_DOWN) && (call->direction == DIR_UP))
    {
        return 1U;
    }

    return 0U;
}

static sint8 Dispatch_SelectBestElevator(HallCall_t *call,Elevator_t *elevA,RemoteState_t *elevB,
    uint8 slave_comm_ok)
{
    uint8 a_score = 4U;
    uint8 b_score = 4U;

    if (slave_comm_ok == 0U)
    {
        return 0;
    }

    if (Dispatch_IsImmediateMatch(elevA, call) != 0U)
    {
        return 0;
    }

    if ((elevB != NULL) && (elevB->state != ELEV_EMERGENCY) &&
        (elevB->current_floor == call->floor) &&
        (elevB->state == ELEV_IDLE))
    {
        return 1;
    }

    if ((elevA->state != ELEV_EMERGENCY) &&
        ((elevA->state == ELEV_IDLE) || Dispatch_IsPerfectMatch(elevA, call)))
    {
        a_score = 0U;
    }
    else if (Dispatch_IsPassedMatch(elevA, call) != 0U)
    {
        a_score = 1U;
    }
    else if (Dispatch_IsOppositeDirection(elevA, call) != 0U)
    {
        a_score = 2U;
    }
    else if (elevA->state == ELEV_IDLE)
    {
        a_score = 3U;
    }

    if ((elevB != NULL) && (elevB->state != ELEV_EMERGENCY) &&
        ((elevB->state == ELEV_IDLE) || Dispatch_IsPerfectMatch((Elevator_t *)elevB, call)))
    {
        b_score = 0U;
    }
    else if ((elevB != NULL) && Dispatch_IsPassedMatch((Elevator_t *)elevB, call) != 0U)
    {
        b_score = 1U;
    }
    else if ((elevB != NULL) && Dispatch_IsOppositeDirection((Elevator_t *)elevB, call) != 0U)
    {
        b_score = 2U;
    }
    else if ((elevB != NULL) && (elevB->state == ELEV_IDLE))
    {
        b_score = 3U;
    }

    if (a_score < b_score)
    {
        return 0;
    }

    if (b_score < a_score)
    {
        return 1;
    }

    if ((a_score == 3U) && (b_score == 3U) )
    {
        if (Dispatch_AbsDiff(elevA->current_floor, call->floor) <=
            Dispatch_AbsDiff(elevB->current_floor, call->floor))
        {return 0;}

        return 1;
    }

    if ((a_score <= 1U) && (b_score <= 1U))
    {
        if (Dispatch_AbsDiff(elevA->current_floor, call->floor) <=
            Dispatch_AbsDiff(elevB->current_floor, call->floor))
        {
            return 0;
        }
        return 1;
    }
    return 0;
}

void Dispatch_Init(void)
{
    uint8 i;

    for (i = 0U; i < HALL_CALL_MAX; ++i)
    {
        s_pendingCalls[i].floor = 0U;
        s_pendingCalls[i].direction = DIR_NONE;
        s_pendingCalls[i].assigned = ASSIGNED_NONE;
    }
}

void Dispatch_RegisterCall(uint8 floor, uint8 direction)
{
    uint8 index;
    uint8 slot;

    if (Dispatch_IsValidCall(floor, direction) == 0U)
    {
        return;
    }

    index = Dispatch_FindCallIndex(floor, direction);
    if (index < HALL_CALL_MAX)
    {
        s_pendingCalls[index].assigned = ASSIGNED_NONE;
        return;
    }

    slot = Dispatch_FindFreeSlot();
    if (slot >= HALL_CALL_MAX)
    {
        return;
    }

    s_pendingCalls[slot].floor = floor;
    s_pendingCalls[slot].direction = direction;
    s_pendingCalls[slot].assigned = ASSIGNED_NONE;
}

void Dispatch_RunAlgorithm(Elevator_t *elevA, RemoteState_t *elevB)
{
        extern void Usart1_TransmitString(const char *);
    char dbg[60];
    Usart1_TransmitString("[DBG] elevB floor=");
    /* print elevB->current_floor */
    char fc[4];
    fc[0] = '0' + elevB->current_floor;
    fc[1] = ' ';
    fc[2] = 's';
    fc[3] = '\0';
    /* elevB state */
    fc[2] = '0' + (uint8)elevB->state;
    Usart1_TransmitString(fc);
    Usart1_TransmitString(" commOK=");
    char hc[2];
    hc[0] = '0' + IPC_IsCommHealthy();
    hc[1] = '\0';
    Usart1_TransmitString(hc);
    Usart1_TransmitString("\r\n");
    
    uint8 i;
    uint8 slave_comm_ok;

    slave_comm_ok = IPC_IsCommHealthy();

    for (i = 0U; i < HALL_CALL_MAX; ++i)
    {
        sint8 best;

        if (s_pendingCalls[i].assigned != ASSIGNED_NONE)
        {
            continue;
        }

        if (s_pendingCalls[i].floor == 0U)
        {
            continue;
        }

        best = Dispatch_SelectBestElevator(&s_pendingCalls[i], elevA, elevB, slave_comm_ok);

        if (best == 0)
        {
            FSM_SetTarget(elevA, s_pendingCalls[i].floor);
            Dispatch_MarkAssigned(s_pendingCalls[i].floor,
                                  s_pendingCalls[i].direction,
                                  ASSIGNED_A);
        }
        else if (best == 1)
        {
            IPC_SendTargetFloor(s_pendingCalls[i].floor);
            Dispatch_MarkAssigned(s_pendingCalls[i].floor,
                                  s_pendingCalls[i].direction,
                                  ASSIGNED_B);
        }
        else
        {
            /* Keep it pending for the next scheduler cycle. */
        }
    }
}

void Dispatch_MarkAssigned(uint8 floor, uint8 direction, uint8 assignedTo)
{
    uint8 index;

    index = Dispatch_FindCallIndex(floor, direction);
    if (index < HALL_CALL_MAX)
    {
        s_pendingCalls[index].assigned = assignedTo;
    }
}

void Dispatch_ClearCall(uint8 floor, uint8 direction)
{
    uint8 index;

    index = Dispatch_FindCallIndex(floor, direction);
    if (index < HALL_CALL_MAX)
    {
        s_pendingCalls[index].floor = 0U;
        s_pendingCalls[index].direction = DIR_NONE;
        s_pendingCalls[index].assigned = ASSIGNED_NONE;
    }
}

uint8 Dispatch_GetPendingCount(void)
{
    uint8 i;
    uint8 count = 0U;

    for (i = 0U; i < HALL_CALL_MAX; ++i)
    {
        if ((s_pendingCalls[i].floor != 0U) &&
            (s_pendingCalls[i].assigned == ASSIGNED_NONE))
        {
            count++;
        }
    }

    return count;
}
