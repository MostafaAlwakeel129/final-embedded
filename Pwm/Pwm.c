/**
 * Pwm.c
 *
 *  Created on: 4/12/2026
 *  Author    : AbdallahDarwish
 */

#include "Pwm.h"
#include "Timer_Private.h"   /* TimerType struct + base addresses */
#include "Bit_Math.h"
#include "Timer.h"

static uint32 Pwm_BaseAddresses[4] = {TIM2_BASE_ADDR, TIM3_BASE_ADDR, TIM4_BASE_ADDR,TIM5_BASE_ADDR};

#define CCR_REG(TIMER, CHANNEL)  *((volatile uint32 *) (&(TIMER->CCR1) + (CHANNEL - 1)))


void Pwm_Init(uint8 TimerId, uint8 Channel, uint16 Prescaler, uint16 AutoReload) {
    TimerType *timer = (TimerType *)Pwm_BaseAddresses[TimerId - TIMER2];

    /*Time-base*/
    timer->CR1 = 0;
    timer->PSC = Prescaler;
    timer->ARR = AutoReload;
    timer->CNT = 0;

    /* Channel: PWM Mode 1 + output-compare preload */
    /*
     * OCxM[2:0] = 110   → PWM mode 1
     * OCxPE     = 1     → preload enable
     * Byte value        = 0x68
     *
     * Channels 1,2 → CCMR1      Channels 3,4 → CCMR2
     * Channel 1,3  → bits 0-7   Channel 2,4 → bits 8-15
     */
    if (Channel <= 2) {
        uint8 shift = (Channel - 1) * 8;
        timer->CCMR1 &= ~((uint32) 0xFF << shift);
        timer->CCMR1 |= ((uint32) CCMR_OC_PWM1_PRELOAD << shift);
    } else {
        uint8 shift = (Channel - 3) * 8;
        timer->CCMR2 &= ~((uint32) 0xFF << shift);
        timer->CCMR2 |= ((uint32) CCMR_OC_PWM1_PRELOAD << shift);
    }

    /* Enable channel output in CCER (CCxE bit)*/
    SET_BIT(timer->CCER, (Channel - 1) * 4);

    CCR_REG(timer, Channel) = 0;

    /* Auto-reload preload + force update to load shadows ── */
    SET_BIT(timer->CR1, CR1_ARPE);
    SET_BIT(timer->EGR, EGR_UG);
    timer->SR = 0;
}

/**
 *  Fixed-point duty-cycle conversion (no float!)
 *  CCR = (DutyPercent * ARR) / 100
 */
void Pwm_SetDutyPercent(uint8 TimerId, uint8 Channel, uint8 DutyPercent) {
    TimerType *timer  = (TimerType *)Pwm_BaseAddresses[TimerId - TIMER2];

    if (DutyPercent > 100) {
        DutyPercent = 100;
    }

    uint32 arr = timer->ARR;
    uint32 ccr = ((uint32) DutyPercent * arr) / 100UL;

    CCR_REG(timer, Channel) = ccr;
}

void Pwm_Start(uint8 TimerId, uint8 Channel) {
    TimerType *tim = (TimerType *)Pwm_BaseAddresses[TimerId - TIMER2];

    /* Make sure channel output is enabled */
    SET_BIT(tim->CCER, (Channel - 1) * 4);
    /* Start the counter */
    SET_BIT(tim->CR1, CR1_CEN);
}

void Pwm_Stop(uint8 TimerId, uint8 Channel) {
    TimerType *tim = ( TimerType *)Pwm_BaseAddresses[TimerId - TIMER2];

    /* Disable channel output */
    CLEAR_BIT(tim->CCER, (Channel - 1) * 4);

    CLEAR_BIT(tim->CR1, CR1_CEN);
}
