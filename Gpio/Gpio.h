/**
 * Gpio.h
 *
 *  Created on: 4/15/2025
 *  Author    : AbdallahDarwish
 */

#ifndef GPIO_H
#define GPIO_H

#include "Std_Types.h"


/*PortName*/
#define GPIO_A     'A'
#define GPIO_B     'B'
#define GPIO_C     'C'
#define GPIO_D     'D'

/*PinMode*/
#define GPIO_INPUT  0x00
#define GPIO_OUTPUT 0x01
#define GPIO_AF     0x02
#define GPIO_ANALOG 0x03

/*DefaultState*/
#define GPIO_PUSH_PULL  0x00
#define GPIO_OPEN_DRAIN 0x01

#define GPIO_NO_PULL_DOWN 0x00
#define GPIO_PULL_UP      0x01
#define GPIO_PULL_DOWN    0x02

/*Data*/
#define LOW      0x0
#define HIGH     0x1


#define OK  0x0
#define NOK 0x1

/*AF Numbers (for use with Gpio_SetAF)*/
#define GPIO_AF0   0x00
#define GPIO_AF1   0x01
#define GPIO_AF2   0x02
#define GPIO_AF3   0x03
#define GPIO_AF4   0x04
#define GPIO_AF5   0x05
#define GPIO_AF6   0x06
#define GPIO_AF7   0x07
#define GPIO_AF8   0x08
#define GPIO_AF9   0x09
#define GPIO_AF10  0x0A
#define GPIO_AF11  0x0B
#define GPIO_AF12  0x0C
#define GPIO_AF13  0x0D
#define GPIO_AF14  0x0E
#define GPIO_AF15  0x0F

void Gpio_Init(uint8 PortName, uint8 PinNumber, uint8 PinMode, uint8 DefaultState);

uint8 Gpio_WritePin(uint8 PortName, uint8 PinNumber, uint8 Data);

uint8 Gpio_ReadPin(uint8 PortName, uint8 PinNumber);

void Gpio_SetAF(uint8 PortName, uint8 PinNumber, uint8 AF);

#endif //GPIO_H
