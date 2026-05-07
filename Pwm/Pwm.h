/**
 * Pwm.h
 *
 *  Created on: 4/12/2026
 *  Author    : AbdallahDarwish
 */

#ifndef PWM_H
#define PWM_H

#include "Std_Types.h"

/* PWM Channel IDs */
#define PWM_CHANNEL_1   1U
#define PWM_CHANNEL_2   2U
#define PWM_CHANNEL_3   3U
#define PWM_CHANNEL_4   4U

/**
 * @brief  Initialise a timer channel for PWM output.
 *         Configures PSC, ARR, and sets the channel to PWM Mode 1.
 *         Does NOT start the output — call Pwm_Start() afterwards.
 *
 *         NOTE: The GPIO pin must be configured as GPIO_AF and the
 *               correct alternate-function must be set BEFORE calling this.
 *
 * @param  TimerId      TIMER_2 .. TIMER_5  (from Timer.h)
 * @param  Channel      PWM_CHANNEL_1 .. PWM_CHANNEL_4
 * @param  Prescaler    Timer prescaler  (PSC value, divides by PSC+1)
 * @param  AutoReload   Timer auto-reload (ARR value, period = ARR+1 ticks)
 */
void Pwm_Init(uint8 TimerId, uint8 Channel, uint16 Prescaler, uint16 AutoReload);

/**
 * @brief  Set the duty cycle using integer-only (fixed-point) arithmetic.
 *         Maps 0–100 % to 0–ARR without using float.
 *
 *         Formula:  CCRx = (DutyPercent * ARR) / 100
 *         Uses uint32 intermediate to prevent 16-bit overflow.
 *
 * @param  TimerId       TIMER_2 .. TIMER_5
 * @param  Channel       PWM_CHANNEL_1 .. PWM_CHANNEL_4
 * @param  DutyPercent   0 – 100
 */
void Pwm_SetDutyPercent(uint8 TimerId, uint8 Channel, uint8 DutyPercent);

/**
 * @brief  Start PWM output on the given channel.
 */
void Pwm_Start(uint8 TimerId, uint8 Channel);

/**
 * @brief  Stop PWM output on the given channel.
 */
void Pwm_Stop(uint8 TimerId, uint8 Channel);

#endif /* PWM_H */
