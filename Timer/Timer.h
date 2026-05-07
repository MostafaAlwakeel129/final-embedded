/**
 * Timer.h
 *
 *  Created on: 4/12/2026
 *  Author    : AbdallahDarwish
 */

#ifndef TIMER_H
#define TIMER_H

#include "Std_Types.h"

/* Timer IDs (General-purpose timers) */
#define TIMER2    2U
#define TIMER3    3U
#define TIMER4    4U
#define TIMER5    5U

#define CH1  1U
#define CH2  2U
#define CH3  3U
#define CH4  4U

typedef void (*TimerCallback)(void);

void Timer_Init(uint8 TimerId, uint16 Prescaler, uint16 AutoReload);

void Timer_Start(uint8 TimerId);

void Timer_Stop(uint8 TimerId);

void Timer_DelayMs(uint8 TimerId, uint32 DelayMs);

void Timer_DelayMsAsync(uint8 TimerId, uint32 DelayMs, TimerCallback Callback);

void Timer_OcToggleInit(uint8 TimerId, uint8 Channel,
                        uint16 Prescaler, uint16 Period);

#endif /* TIMER_H */
