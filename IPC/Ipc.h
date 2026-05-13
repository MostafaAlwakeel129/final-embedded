/**
 * Ipc.h
 *
 *  Created on: 5/10/2026
 *  Author    : AbdallahDarwish
 */

#ifndef IPC_H
#define IPC_H

#include "Project.h"

#define IPC_TICK_MS      50U    /* Exchange period (ms)              */
#define IPC_TIMEOUT_MS   200U   /* 4 missed ticks = comm fault       */

/*
 * IPC_Init
 * Initialises the IPC layer.
 * Master: starts a 50 ms periodic timer (TIMER2) that calls IPC_Tick,
 *         enables SPI master async transfer.
 * Slave:  pre-loads an empty packet into SPI TX and enables RXNEIE.
 * Call once at startup after SPI and Timer drivers are ready.
 */
void IPC_Init(void);

/*
 * IPC_Tick  — called from TIMER2 ISR every 50 ms (Master only)
 * Encodes the local elevator state, starts an async SPI transfer,
 * and increments the comm-fault timeout counter.
 * Counter resets when the SPI done-callback fires.
 */
void IPC_Tick(void);

/*
 * IPC_EncodeTxPacket
 * Serialises an Elevator_t into an IPC_Packet_t.
 *   header    = IPC_HEADER (0xA5)
 *   state     = elev->state  (cast to uint8)
 *   floor     = elev->current_floor
 *   direction = elev->direction
 *   speed     = elev->speed_percent
 *   flags     Bit 0: emergency active (state == ELEV_EMERGENCY)
 *             Bit 1: doors open       (state == ELEV_DOORS_OPEN)
 *   reserved  = 0x00
 *   checksum  = XOR of bytes [0..6]
 *
 * IN:  elev — source elevator state
 * OUT: pkt  — filled packet ready to transmit
 */
void IPC_EncodeTxPacket(Elevator_t *elev, IPC_Packet_t *pkt);

/*
 * IPC_DecodeRxPacket
 * Validates and deserialises a received IPC_Packet_t.
 * Checks header == 0xA5 and recomputes checksum over bytes [0..6].
 * If either check fails, returns 0 and leaves *remote unchanged.
 *
 * IN:  pkt    — pointer to received raw packet
 * OUT: remote — filled with decoded remote elevator state (on success)
 * RET: 1 = valid packet decoded, 0 = corrupt / wrong header
 */
uint8 IPC_DecodeRxPacket(IPC_Packet_t *pkt, RemoteState_t *remote);

/*
 * IPC_IsCommHealthy
 * Returns 1 if a valid packet arrived within IPC_TIMEOUT_MS.
 * Returns 0 on comm fault — caller should fall back to safe mode.
 */
uint8 IPC_IsCommHealthy(void);

/*
 * IPC_GetRemoteState
 * Copies the last successfully decoded remote state into *out.
 * Uses a critical section internally to prevent torn reads.
 *
 * OUT: out — filled with latest remote elevator state
 */
void IPC_GetRemoteState(RemoteState_t *out);

/*
 * IPC_SendTargetFloor  (Master only)
 * Latches a target floor command into the next outgoing packet's
 * flags field so the Slave knows where to move.
 * Sent on the next IPC_Tick call.
 *
 * IN:  floor — FLOOR_1..FLOOR_4
 */
void IPC_SendTargetFloor(uint8 floor);

uint8 IPC_ConsumePendingFloor(void);

#endif /* IPC_H */