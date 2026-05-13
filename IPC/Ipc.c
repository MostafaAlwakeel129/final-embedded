/**
 * Ipc.c
 *
 *  Created on: 5/10/2026
 *  Author    : AbdallahDarwish
 */

#include "Ipc.h"

/* ── Internal state ──────────────────────────────────────────────── */

static RemoteState_t  s_remoteState;          /* last decoded state   */
static uint8          s_commHealthy  = 0U;    /* 1 = healthy          */
static uint32         s_missedTicks  = 0U;    /* timeout counter      */
static uint8          s_pendingFloor = 0U;    /* IPC_SendTargetFloor  */

/* ── Helpers ─────────────────────────────────────────────────────── */

/*
 * Compute XOR checksum of the first 7 bytes of the packet
 * (bytes 0-6: header, state, floor, direction, speed, flags, reserved).
 */
static uint8 IPC_ComputeChecksum(const IPC_Packet_t *pkt)
{
    return (uint8)(  pkt->header
                   ^ pkt->state
                   ^ pkt->floor
                   ^ pkt->direction
                   ^ pkt->speed
                   ^ pkt->flags
                   ^ pkt->reserved);
}

/* ── Public API ──────────────────────────────────────────────────── */

void IPC_EncodeTxPacket(Elevator_t *elev, IPC_Packet_t *pkt)
{
    uint8 flags = 0U;

    if (elev->state == ELEV_EMERGENCY)
    {
        flags |= (uint8)(1U << 0U);   /* Bit 0: emergency active */
    }
    if (elev->state == ELEV_DOORS_OPEN)
    {
        flags |= (uint8)(1U << 1U);   /* Bit 1: doors open       */
    }

    pkt->header    = IPC_HEADER;
    pkt->state     = (uint8)elev->state;
    pkt->floor     = elev->current_floor;
    pkt->direction = elev->direction;
    pkt->speed     = elev->speed_percent;
    pkt->flags     = flags;
    pkt->reserved  = 0x00U;
    pkt->checksum  = IPC_ComputeChecksum(pkt);
}

uint8 IPC_DecodeRxPacket(IPC_Packet_t *pkt, RemoteState_t *remote)
{
    uint8 expected;

    /* Validate magic header */
    if (pkt->header != IPC_HEADER)
    {
        s_commHealthy = 0U;   /* bad packet = comm fault */
        return 0U;
    }

    /* Validate checksum */
    expected = IPC_ComputeChecksum(pkt);
    if (pkt->checksum != expected)
    {
        s_commHealthy = 0U;   /* bad packet = comm fault */
        return 0U;
    }

    /* Deserialise into remote state */
    remote->state         = (ElevatorState_t)pkt->state;
    remote->current_floor = pkt->floor;
    remote->direction     = pkt->direction;
    remote->speed_percent = pkt->speed;

    /* target_floor is not transmitted — keep at 0 */
    remote->target_floor  = 0U;

    /* Update internal copy and mark comm healthy */
    s_remoteState  = *remote;
    s_commHealthy  = 1U;
    s_missedTicks  = 0U;

    return 1U;
}

/* ── Stub implementations (not exercised by Stage-3 test) ───────── */

void IPC_Init(void)
{
    /* Hardware init (SPI, timer) goes here in full build */
    s_commHealthy = 0U;
    s_missedTicks = 0U;
    s_pendingFloor = 0U;
}

void IPC_Tick(void)
{
    /* Called from TIMER2 ISR every 50 ms on Master */
    s_missedTicks++;
    if (s_missedTicks >= (IPC_TIMEOUT_MS / IPC_TICK_MS))
    {
        s_commHealthy = 0U;
    }
    /* Full build: encode local state, call Spi1_MasterTransferAsync */
}

uint8 IPC_IsCommHealthy(void)
{
    return s_commHealthy;
}

void IPC_GetRemoteState(RemoteState_t *out)
{
    /* Critical section would disable/re-enable IRQs in full build */
    *out = s_remoteState;
}

void IPC_SendTargetFloor(uint8 floor)
{
    s_pendingFloor = floor;
}