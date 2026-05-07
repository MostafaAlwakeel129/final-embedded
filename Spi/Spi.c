/**
 * Spi.c
 *
 *  Created on: Wed May 24 2023
 *  Author    : Abdullah Darwish
 */
#include "stm32f401xe.h"
#include "Gpio.h"
#include "Spi.h"

void Spi1_Init(uint8 MasterSlave, uint8 ClkPol, uint8 ClkPhase) {

  Gpio_Init(GPIO_B, 3, GPIO_AF , GPIO_PUSH_PULL);
  Gpio_Init(GPIO_B, 4, GPIO_AF , GPIO_PUSH_PULL);
  Gpio_Init(GPIO_B, 5, GPIO_AF , GPIO_PUSH_PULL);

  
  Gpio_SetAF(GPIO_B, 3, GPIO_AF5);
  Gpio_SetAF(GPIO_B, 4, GPIO_AF5);
  Gpio_SetAF(GPIO_B, 5, GPIO_AF5);
  

  SPI1->CR1 |= (0x1 << SPI_CR1_SSM_Pos);
  SPI1->CR1 |= (0x1 << SPI_CR1_SSI_Pos);

  SPI1->CR1 &= ~(1 << SPI_CR1_MSTR_Pos);
  SPI1->CR1 |= (MasterSlave << SPI_CR1_MSTR_Pos);

  SPI1->CR1 &= ~(1 << SPI_CR1_CPOL_Pos);
  SPI1->CR1 |= (ClkPol << SPI_CR1_CPOL_Pos);

  SPI1->CR1 &= ~(1 << SPI_CR1_CPHA_Pos);
  SPI1->CR1 |= (ClkPhase << SPI_CR1_CPHA_Pos);

  /*************************************************************************/
  // Baud Rate
  SPI1->CR1 &= ~(0x7 << SPI_CR1_BR_Pos);
  SPI1->CR1 |= (0x1 << SPI_CR1_BR_Pos);  // 16/4 -> 4MHZ
  /*************************************************************************/

  SPI1->CR1 |= (1 << SPI_CR1_SPE_Pos);
}

uint8 Spi1_TransmitReceiveByte(uint8 TxData, uint8* RxData) {
  if (SPI1->SR & (1 << SPI_SR_TXE_Pos)) {
    SPI1->DR = TxData;
    while (SPI1->SR & (1 << SPI_SR_BSY_Pos));
    *RxData = SPI1->DR;
    return SPI_OK;
  }
  return SPI_NOK;
}
