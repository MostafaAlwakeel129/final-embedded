/**
 * Exti.h
 *
 *  Created on: 5/16/2025
 *  Author    : AbdallahDarwish
 */

#ifndef EXTI_H
#define EXTI_H

#include "Std_Types.h"

/*LineNumber*/
#define EXTI_LINE_0    0U
#define EXTI_LINE_1    1U
#define EXTI_LINE_2    2U
#define EXTI_LINE_3    3U
#define EXTI_LINE_4    4U
#define EXTI_LINE_5    5U
#define EXTI_LINE_6    6U
#define EXTI_LINE_7    7U
#define EXTI_LINE_8    8U
#define EXTI_LINE_9    9U
#define EXTI_LINE_10   10U
#define EXTI_LINE_11   11U
#define EXTI_LINE_12   12U
#define EXTI_LINE_13   13U
#define EXTI_LINE_14   14U
#define EXTI_LINE_15   15U

/*PortName*/
#define EXTI_PORT_A     0U
#define EXTI_PORT_B     1U
#define EXTI_PORT_C     2U
#define EXTI_PORT_D     3U

/*EdgeType*/
#define EXTI_EDGE_RISING     0U
#define EXTI_EDGE_FALLING    1U
#define EXTI_EDGE_BOTH       2U


typedef void (*ExtiCallback)(void);

void Exti_Init(uint8 LineNumber, uint8 PortName, uint8 EdgeType, ExtiCallback Callback);

void Exti_Enable(uint8 LineNumber);

void Exti_Disable(uint8 LineNumber);

#endif //EXTI_H
