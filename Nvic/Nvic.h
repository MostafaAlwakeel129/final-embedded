/**
 * Nvic.h
 *
 * Created on: 2026-04-12
 * Author    : AbdallahDarwish
 */

#ifndef NVIC_H
#define NVIC_H
#include "Std_Types.h"


void Nvic_EnableIrq(uint8 IrqNumber);


void Nvic_DisableIrq(uint8 IrqNumber);


#endif //NVIC_H
