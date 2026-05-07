/**
 * Rcc_Private.h
 *
 *  Created on: Sun Mar 26 2023
 *  Author    : Abdullah Darwish
 */

#ifndef RCC_PRIVATE_H
#define RCC_PRIVATE_H
#include "Std_Types.h"

#define RCC_BASE_ADDR       0x40023800
#define RCC_CR              (*(volatile uint32 *)(RCC_BASE_ADDR + 0x00UL))
#define RCC_PLLCFGR         (*(volatile uint32 *)(RCC_BASE_ADDR + 0x04UL))
#define RCC_CFGR            (*(volatile uint32 *)(RCC_BASE_ADDR + 0x08UL))
#define RCC_CIR             (*(volatile uint32 *)(RCC_BASE_ADDR + 0x0CUL))
#define RCC_AHB1RSTR        (*(volatile uint32 *)(RCC_BASE_ADDR + 0x10UL))
#define RCC_AHB2RSTR        (*(volatile uint32 *)(RCC_BASE_ADDR + 0x14UL))
#define RCC_AHB3RSTR        (*(volatile uint32 *)(RCC_BASE_ADDR + 0x18UL))
#define RCC_APB1RSTR        (*(volatile uint32 *)(RCC_BASE_ADDR + 0x20UL))
#define RCC_APB2RSTR        (*(volatile uint32 *)(RCC_BASE_ADDR + 0x24UL))
#define RCC_AHB1ENR         (*(volatile uint32 *)(RCC_BASE_ADDR + 0x30UL))
#define RCC_AHB2ENR         (*(volatile uint32 *)(RCC_BASE_ADDR + 0x34UL))
#define RCC_AHB3ENR         (*(volatile uint32 *)(RCC_BASE_ADDR + 0x38UL))
#define RCC_APB1ENR         (*(volatile uint32 *)(RCC_BASE_ADDR + 0x40UL))
#define RCC_APB2ENR         (*(volatile uint32 *)(RCC_BASE_ADDR + 0x44UL))
#define RCC_AHB1LPENR       (*(volatile uint32 *)(RCC_BASE_ADDR + 0x50UL))
#define RCC_AHB2LPENR       (*(volatile uint32 *)(RCC_BASE_ADDR + 0x54UL))
#define RCC_AHB3LPENR       (*(volatile uint32 *)(RCC_BASE_ADDR + 0x58UL))
#define RCC_APB1LPENR       (*(volatile uint32 *)(RCC_BASE_ADDR + 0x60UL))
#define RCC_APB2LPENR       (*(volatile uint32 *)(RCC_BASE_ADDR + 0x64UL))
#define RCC_BDCR            (*(volatile uint32 *)(RCC_BASE_ADDR + 0x70UL))
#define RCC_CSR             (*(volatile uint32 *)(RCC_BASE_ADDR + 0x74UL))
#define RCC_SSCGR           (*(volatile uint32 *)(RCC_BASE_ADDR + 0x80UL))
#define RCC_PLLI2SCFGR      (*(volatile uint32 *)(RCC_BASE_ADDR + 0x84UL))
#define RCC_DCKCFGR         (*(volatile uint32 *)(RCC_BASE_ADDR + 0x8CUL))


#endif /* RCC_PRIVATE_H */
