/**
 * Timer_Private.h
 *
 *  Created on: 4/12/2026
 *  Author    : AbdallahDarwish
 */

#ifndef TIMER_PRIVATE_H
#define TIMER_PRIVATE_H

#include "Std_Types.h"

/* TIMx register map (General-purpose: TIM2–TIM5)*/
typedef struct {
    volatile uint32 CR1;         /* 0x00 - Control register 1              */
    volatile uint32 CR2;         /* 0x04 - Control register 2              */
    volatile uint32 SMCR;        /* 0x08 - Slave mode control              */
    volatile uint32 DIER;        /* 0x0C - DMA/Interrupt enable            */
    volatile uint32 SR;          /* 0x10 - Status register                 */
    volatile uint32 EGR;         /* 0x14 - Event generation                */
    volatile uint32 CCMR1;       /* 0x18 - Capture/compare mode 1          */
    volatile uint32 CCMR2;       /* 0x1C - Capture/compare mode 2          */
    volatile uint32 CCER;        /* 0x20 - Capture/compare enable          */
    volatile uint32 CNT;         /* 0x24 - Counter                         */
    volatile uint32 PSC;         /* 0x28 - Prescaler                       */
    volatile uint32 ARR;         /* 0x2C - Auto-reload                     */
    volatile uint32 _reserved1;  /* 0x30                                   */
    volatile uint32 CCR1;        /* 0x34 - Capture/compare register 1      */
    volatile uint32 CCR2;        /* 0x38 - Capture/compare register 2      */
    volatile uint32 CCR3;        /* 0x3C - Capture/compare register 3      */
    volatile uint32 CCR4;        /* 0x40 - Capture/compare register 4      */
    volatile uint32 _reserved2;  /* 0x44                                   */
    volatile uint32 DCR;         /* 0x48 - DMA control                     */
    volatile uint32 DMAR;        /* 0x4C - DMA address for full transfer   */
    volatile uint32 OR;          /* 0x50 - Option register (TIM2/TIM5)     */
} TimerType;

#define TIM2_BASE_ADDR   0x40000000UL
#define TIM3_BASE_ADDR   0x40000400UL
#define TIM4_BASE_ADDR   0x40000800UL
#define TIM5_BASE_ADDR   0x40000C00UL

/*CR1 bit positions*/
#define CR1_CEN          0U    /* Counter enable              */
#define CR1_URS          2U    /* Update request source       */
#define CR1_OPM          3U    /* One-pulse mode              */
#define CR1_ARPE         7U    /* Auto-reload preload enable  */

/*DIER bit positions*/
#define DIER_UIE         0U    /* Update interrupt enable     */

/*SR bit positions*/
#define SR_UIF           0U    /* Update interrupt flag       */

/*EGR bit positions*/
#define EGR_UG           0U    /* Update generation           */

/*  CCMR OC mode value: PWM Mode 1 + Preload enable     */
/*  OCxM[2:0] = 110  (PWM mode 1)                       */
/*  OCxPE     = 1    (preload enable)                   */
/*  => bits [6:3] = 0b1101 => byte = 0x68               */
#define CCMR_OC_PWM1_PRELOAD  0x68U

/*  OCxM[2:0] = 011  (Toggle on match)                  */
/*  OCxPE     = 0    (no preload needed)                 */
/*  => bits [6:4] = 0b011 => byte = 0x30                 */
#define CCMR_OC_TOGGLE        0x30U

#endif /* TIMER_PRIVATE_H */
