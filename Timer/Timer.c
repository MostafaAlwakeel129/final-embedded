/**
 * Timer.c
 *
 *  Created on: 4/12/2026
 *  Author    : AbdallahDarwish
 */

#include "Timer.h"
#include "Timer_Private.h"
#include "Bit_Math.h"
#include "Nvic.h"

#define NUM_OF_TIMERS 4U

static uint32 Timer_BaseAddresses[NUM_OF_TIMERS] = {TIM2_BASE_ADDR, TIM3_BASE_ADDR, TIM4_BASE_ADDR, TIM5_BASE_ADDR};

/* NVIC IRQ numbers:  TIM2=28, TIM3=29, TIM4=30, TIM5=50 */
static uint8 Timer_NvicIrq[NUM_OF_TIMERS] = {28, 29, 30, 50};

static TimerCallback Timer_Callbacks[NUM_OF_TIMERS] = {0};
static uint8         Timer_IsPeriodic[NUM_OF_TIMERS] = {0U, 0U, 0U, 0U};

static TimerType *Timer_GetPeripheral(uint8 TimerId) {
    return (TimerType *) Timer_BaseAddresses[TimerId - TIMER2];
}

void Timer_Init(uint8 TimerId, uint16 Prescaler, uint16 AutoReload) {
    TimerType *timer = Timer_GetPeripheral(TimerId);

    timer->CR1 = 0; // Reset timer
    timer->PSC = Prescaler;
    timer->ARR = AutoReload;
    timer->CNT = 0;

    SET_BIT(timer->EGR, EGR_UG); // Force update to load PSC & ARR
    timer->SR = 0; // Clear the update flag that UG set
}

void Timer_Start(uint8 TimerId) {
    TimerType *tim = Timer_GetPeripheral(TimerId);
    SET_BIT(tim->CR1, CR1_CEN);
}

void Timer_Stop(uint8 TimerId) {
    TimerType *tim = Timer_GetPeripheral(TimerId);
    CLEAR_BIT(tim->CR1, CR1_CEN);
}

/**
 *  Synchronous delay — blocks until timer overflows
 *  16 MHz HSI  ÷  (PSC+1 = 16000) = 1 kHz  →  1 tick = 1 ms
 */
void Timer_DelayMs(uint8 TimerId, uint32 DelayMs) {
    TimerType *timer =(TimerType *)Timer_BaseAddresses[TimerId - TIMER2];

    timer->CR1 = 0; // Stop & reset
    timer->PSC = 15999U / 3;
    timer->ARR = (uint16) (DelayMs - 1);
    timer->CNT = 0;

    SET_BIT(timer->EGR, EGR_UG); // Load shadow registers
    timer->SR = 0; // Clear UIF caused by UG

    SET_BIT(timer->CR1, CR1_OPM); // One-pulse mode
    SET_BIT(timer->CR1, CR1_CEN); // Start counting

    while (!READ_BIT(timer->SR, SR_UIF)) {
        // Poll – CPU is blocked here
    }

    timer->SR = 0; // Clear UIF
    CLEAR_BIT(timer->CR1, CR1_CEN); // Stop counter
}


void Timer_DelayMsAsync(uint8 TimerId, uint32 DelayMs, TimerCallback Callback) {
    TimerType *timer =(TimerType *)Timer_BaseAddresses[TimerId - TIMER2];
    uint8 index = TimerId - TIMER2;
    uint8 irqNum = Timer_NvicIrq[index];

    Timer_Callbacks[index] = Callback;
    Timer_IsPeriodic[index] = 0U;

    timer->CR1 = 0; // Stop & reset
    timer->PSC = 15999U / 3;
    timer->ARR = (uint16) (DelayMs - 1);
    timer->CNT = 0;

    SET_BIT(timer->EGR, EGR_UG); // Load shadow registers
    timer->SR = 0; // Clear UIF caused by UG

    SET_BIT(timer->CR1, CR1_OPM); // One-pulse mode

    SET_BIT(timer->DIER, DIER_UIE); // Enable update interrupt
    Nvic_EnableIrq(irqNum);

    SET_BIT(timer->CR1, CR1_CEN); // Start counting
}

/**
 *  Output Compare Toggle — pin flips every time CNT == CCRx.
 *  Toggle every 1s:  PSC=15999, ARR=999, CompareValue=0
 */
void Timer_OcToggleInit(uint8 TimerId, uint8 Channel, uint16 Prescaler, uint16 Period) {
    TimerType *timer =(TimerType *)Timer_BaseAddresses[TimerId - TIMER2];

    /* Stop & reset */
    timer->CR1 = 0;

    /* Time-base */
    timer->PSC = Prescaler;
    timer->ARR = Period - 1;
    timer->CNT = 0;

    /* Set channel to OC Toggle mode (OCxM = 011)
     *    Channels 1,2 → CCMR1   |   Channels 3,4 → CCMR2
     *    Channel 1,3 → bits[7:0] |   Channel 2,4 → bits[15:8]
     */
    if (Channel <= 2) {
        uint8 shift = (Channel - 1) * 8;
        timer->CCMR1 &= ~((uint32) 0xFF << shift);
        timer->CCMR1 |= ((uint32) CCMR_OC_TOGGLE << shift);
    } else {
        uint8 shift = (Channel - 3) * 8;
        timer->CCMR2 &= ~((uint32) 0xFF << shift);
        timer->CCMR2 |= ((uint32) CCMR_OC_TOGGLE << shift);
    }

    /* Set compare value (CCRx) */
    volatile uint32 *ccr = &timer->CCR1 + (Channel - 1);
    *ccr = 0;

    /* Enable channel output (CCxE bit in CCER) */
    SET_BIT(timer->CCER, (Channel - 1) * 4);

    /* Load shadow registers & clear flags */
    SET_BIT(timer->EGR, EGR_UG);
    timer->SR = 0;

    /* Start */
    SET_BIT(timer->CR1, CR1_CEN);
}

static void Timer_HandleIrq(uint8 index) {
    TimerType *timer = (TimerType *)Timer_BaseAddresses[index];

    if (READ_BIT(timer->SR, SR_UIF)) {
        timer->SR = 0; /* Clear UIF */

        if (Timer_IsPeriodic[index]) {
            /* Periodic: keep running, just fire callback */
            if (Timer_Callbacks[index] != 0) {
                Timer_Callbacks[index]();
            }
        } else {
            /* One-shot: stop timer and disable UIE before callback */
            CLEAR_BIT(timer->DIER, DIER_UIE);
            CLEAR_BIT(timer->CR1, CR1_CEN);
            if (Timer_Callbacks[index] != 0) {
                Timer_Callbacks[index]();
            }
        }
    }
}

void Timer_StartPeriodic(uint8 TimerId, uint32 DelayMs, TimerCallback Callback)
{
    TimerType *timer =(TimerType *)Timer_BaseAddresses[TimerId - TIMER2];
    uint8 index = TimerId - TIMER2;
    uint8 irqNum = Timer_NvicIrq[index];

    Timer_Callbacks[index] = Callback;
    Timer_IsPeriodic[index] = 1U;

    CLEAR_BIT(timer->CR1, CR1_CEN);
    timer->PSC = (uint16)((16000000UL / 1000UL) - 1UL);
    /* Auto-reload: overflow every DelayMs ticks */
    timer->ARR = (uint16)(DelayMs - 1UL);
    /* Reset counter */
    timer->CNT = 0U;
    /* Clear any pending update flag */
    timer->SR = 0U;
    /* Enable update interrupt */
    timer->DIER = 0x0001U;
    /* Enable counter — NO one-pulse mode (OPM stays 0), ARPE=1 */
    CLEAR_BIT(timer->CR1, CR1_OPM);
    /* Enable auto-reload preload */
    SET_BIT(timer->CR1, CR1_ARPE);
    Nvic_EnableIrq(irqNum);
    /* Start counter */
    SET_BIT(timer->CR1, CR1_CEN);
}

void SysTick_Init(void)
{
    volatile uint32 *SYST_RVR = (volatile uint32 *)0xE000E014;
    volatile uint32 *SYST_CVR = (volatile uint32 *)0xE000E018;
    volatile uint32 *SYST_CSR = (volatile uint32 *)0xE000E010;

    *SYST_RVR = 15999UL;
    *SYST_CVR = 0UL;
    *SYST_CSR = 0x07UL;
}


void TIM2_IRQHandler(void) {
    Timer_HandleIrq(0);
}
void TIM3_IRQHandler(void) {
    Timer_HandleIrq(1);
}
void TIM4_IRQHandler(void) {
    Timer_HandleIrq(2);
}
void TIM5_IRQHandler(void) {
    Timer_HandleIrq(3);
}