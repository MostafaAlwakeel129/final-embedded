/**
 * Gpio.c
 *
 *  Created on: 4/15/2025
 *  Author    : AbdallahDarwish
 */


#include "Std_Types.h"
#include "Gpio.h"
#include "Gpio_Private.h"

#define GPIO_REG(PORT_ID, REG_ID)          ((volatile uint32 *) ((PORT_ID) + (REG_ID)))

uint32 addressMap[4] = {GPIOA_BASE_ADDR, GPIOB_BASE_ADDR, GPIOC_BASE_ADDR, GPIOD_BASE_ADDR};

void Gpio_Init(uint8 PortName, uint8 PinNumber, uint8 PinMode, uint8 DefaultState) {

    uint8 addressIndex = PortName - GPIO_A;

    GpioType* gpioDevice = (GpioType*) addressMap[addressIndex];

    gpioDevice->GPIO_MODER  &= ~(0x03 << (PinNumber * 2));
    gpioDevice->GPIO_MODER |= (PinMode << (PinNumber * 2));

    if (PinMode == GPIO_INPUT) {
        gpioDevice->GPIO_PUPDR &= ~(0x03 << (PinNumber * 2));
        gpioDevice->GPIO_PUPDR |= (DefaultState << (PinNumber * 2));
    } else {
        gpioDevice->GPIO_OTYPER &= ~(0x1 << PinNumber);
        gpioDevice->GPIO_OTYPER |= (DefaultState << (PinNumber));
    }

    gpioDevice->GPIO_OSPEEDR |= (0x03 << (PinNumber * 2));

}

uint8 Gpio_WritePin(uint8 PortName, uint8 PinNumber, uint8 Data) {
    uint8 status = NOK;
    uint8 addressIndex = PortName - GPIO_A;
    GpioType* gpioDevice = (GpioType*) addressMap[addressIndex];

    if (((gpioDevice->GPIO_MODER & (0x03 << (PinNumber * 2))) >> (PinNumber * 2)) != GPIO_INPUT) {
        gpioDevice->GPIO_ODR &= ~(0x1 << PinNumber);
        gpioDevice->GPIO_ODR  |= (Data << PinNumber);
        status = OK;
    }
    return status;
}

uint8 Gpio_ReadPin(uint8 PortName, uint8 PinNum) {
    uint8 data = 0;
    uint8 addressIndex = PortName - GPIO_A;
    GpioType* gpioDevice = (GpioType*) addressMap[addressIndex];
    data = (gpioDevice->GPIO_IDR & (0x1 << PinNum)) >> PinNum;
    return data;
}

void Gpio_SetAF(uint8 PortName, uint8 PinNumber, uint8 AF) {
    uint8 addressIndex = PortName - GPIO_A;
    GpioType* gpioDevice = (GpioType*) addressMap[addressIndex];

    if (PinNumber < 8) {
        gpioDevice->GPIO_AFRL &= ~((uint32)0x0F << (PinNumber * 4));
        gpioDevice->GPIO_AFRL |=  ((uint32)AF   << (PinNumber * 4));
    } else {
        uint8 pos = PinNumber - 8;
        gpioDevice->GPIO_AFRH &= ~((uint32)0x0F << (pos * 4));
        gpioDevice->GPIO_AFRH |=  ((uint32)AF   << (pos * 4));
    }
}
