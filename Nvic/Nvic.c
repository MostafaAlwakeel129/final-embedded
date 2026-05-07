/**
 * Nvic.c
 *
 * Created on: 2026-04-12
 * Author    : AbdallahDarwish
 */

#include "Nvic.h"

typedef struct {
    volatile uint32 NVIC_ISER[8];
    uint32 _r[24];
    volatile uint32 NVIC_ICER[8];
} NvicType;

#define NVIC          ((NvicType*)0xE000E100)


void Nvic_EnableIrq(uint8 IrqNumber) {
    NVIC->NVIC_ISER[IrqNumber / 32] |= (0x01 << (IrqNumber % 32));
}


void Nvic_DisableIrq(uint8 IrqNumber) {
    NVIC->NVIC_ICER[IrqNumber / 32] |= (0x01 << (IrqNumber % 32));
}
