/**
 * Ipc.c
 *
 *  Created on: 5/10/2026
 *  Author    : AbdallahDarwish
 */

#include "Ipc.h"
#include "Spi.h"
#include "Gpio.h"
#include "Timer.h"
#include "Fsm.h"


/* ── Internal state ──────────────────────────────────────────────── */

static RemoteState_t  s_remoteState;          /* last decoded state   */
static uint8          s_commHealthy  = 0U;    /* 1 = healthy          */
static uint32         s_missedTicks  = 0U;    /* timeout counter      */
static uint8          s_pendingFloor = 0U;    /* IPC_SendTargetFloor  */
static uint8 s_spiTxBuf[SPI_PACKET_LEN];
static uint8 s_spiRxBuf[SPI_PACKET_LEN];
static uint8 s_isMaster = 0U;
static Elevator_t *s_localElev = NULL;

/* Add CS helpers */
static void CS_Low(void)  { Gpio_WritePin(GPIO_A, 4, LOW);  }
static void CS_High(void) { Gpio_WritePin(GPIO_A, 4, HIGH); }

/* Add SPI done callback */
static void IPC_SpiDoneCallback(void)
{
    IPC_Packet_t rxPkt;
    uint8 i;

    CS_High();

    for (i = 0U; i < SPI_PACKET_LEN; i++)
    {
        ((uint8 *)&rxPkt)[i] = s_spiRxBuf[i];
    }

    IPC_DecodeRxPacket(&rxPkt, &s_remoteState);
}

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

static void Slave_SpiDoneCallback(void)
{
    uint8 rawBuf[SPI_PACKET_LEN];
    IPC_Packet_t rxPkt;
    IPC_Packet_t txPkt;
    uint8 i;

    Spi1_SlaveGetRxBuffer(rawBuf);
    for (i = 0U; i < SPI_PACKET_LEN; i++)
    {
        ((uint8 *)&rxPkt)[i] = rawBuf[i];
    }

    if (IPC_DecodeRxPacket(&rxPkt, &s_remoteState) != 0U)
    {
        if (rxPkt.reserved != 0U)
        {
            FSM_SetTarget(s_localElev, rxPkt.reserved);
        }
    }

    IPC_EncodeTxPacket(s_localElev, &txPkt);
    for (i = 0U; i < SPI_PACKET_LEN; i++)
    {
        rawBuf[i] = ((uint8 *)&txPkt)[i];
    }
    Spi1_SlavePreload(rawBuf, SPI_PACKET_LEN);
}
/* ── Stub implementations (not exercised by Stage-3 test) ───────── */

void IPC_Init(uint8 isMaster, Elevator_t *elev)
{
    s_localElev    = elev;
    s_isMaster     = isMaster;
    s_commHealthy  = 1U;
    s_missedTicks  = 0U;
    s_pendingFloor = 0U;

    if (isMaster)
    {
        Gpio_Init(GPIO_A, 4, GPIO_OUTPUT, GPIO_PUSH_PULL);
        CS_High();
        Spi1_Init(SPI_MASTER, SPI_IDLE_LOW, SPI_SAMPLE_FIRST_TRANSITION);
        Timer_StartPeriodic(TIMER2, 6U, IPC_Tick);
    }
    else
    {
        Spi1_Init(SPI_SLAVE, SPI_IDLE_LOW, SPI_SAMPLE_FIRST_TRANSITION);
        IPC_EncodeTxPacket(s_localElev, (IPC_Packet_t*)s_spiTxBuf);
        Spi1_SlavePreload(s_spiTxBuf, SPI_PACKET_LEN);
        Spi1_SlaveEnableRx(Slave_SpiDoneCallback);
    }
}

void IPC_Tick(void)
{
    IPC_Packet_t txPkt;
    uint8 i;
    uint8 pendingFloor;

    s_missedTicks++;
    if (s_missedTicks >= (IPC_TIMEOUT_MS / IPC_TICK_MS))
    {
        s_commHealthy = 0U;
    }

    /* Only Master drives the transfer */
    if (s_isMaster == 0U) return;

    IPC_EncodeTxPacket(s_localElev, &txPkt);

    pendingFloor = IPC_ConsumePendingFloor();
    txPkt.reserved = pendingFloor;
    txPkt.checksum = (uint8)(txPkt.header ^ txPkt.state ^ txPkt.floor
                             ^ txPkt.direction ^ txPkt.speed
                             ^ txPkt.flags ^ txPkt.reserved);

    for (i = 0U; i < SPI_PACKET_LEN; i++)
    {
        s_spiTxBuf[i] = ((uint8 *)&txPkt)[i];
    }

    CS_Low();
    Spi1_MasterTransferAsync(s_spiTxBuf, s_spiRxBuf,
                             SPI_PACKET_LEN, IPC_SpiDoneCallback);
}

uint8 IPC_IsCommHealthy(void)
{
    return s_commHealthy;
}

void IPC_GetRemoteState(RemoteState_t *out)
{
    Enter_Critical();
    *out = s_remoteState;
    Exit_Critical();
}

void IPC_SendTargetFloor(uint8 floor)
{
    s_pendingFloor = floor;
}

uint8 IPC_ConsumePendingFloor(void)
{
    uint8 floor;
    Enter_Critical();
    floor          = s_pendingFloor;
    s_pendingFloor = 0U;
    Exit_Critical();
    return floor;
}

