/**
 * Spi.c
 */
#include "stm32f401xe.h"
#include "Gpio.h"
#include "Spi.h"
#include "Nvic.h"

/* --- Non-Blocking State Variables --- */
static volatile uint8  *Master_TxBuf = 0;
static volatile uint8  *Master_RxBuf = 0;
static volatile uint8   Master_Len = 0;
static volatile uint8   Master_TxIndex = 0;
static volatile uint8   Master_RxIndex = 0;
static volatile uint8   Master_Busy = 0;   
static          SpiCallback Master_Callback = 0;

static volatile uint8  slaveTxShadow[SPI_PACKET_LEN] = {0};
static volatile uint8  slaveRxBuf[SPI_PACKET_LEN] = {0};
static volatile uint8  slaveRxDone[SPI_PACKET_LEN] = {0};
static volatile uint8  slaveTxIndex = 0;
static volatile uint8  slaveRxIndex = 0;
static          SpiCallback Slave_Callback = 0;
static uint8 s_isMaster = 0U;

/* --- Your Original Initialization --- */
void Spi1_Init(uint8 MasterSlave, uint8 ClkPol, uint8 ClkPhase) {
    /* MISO, MOSI, SCK Pins */
    Gpio_Init(GPIO_B, 3, GPIO_AF, GPIO_PUSH_PULL);
    Gpio_Init(GPIO_B, 4, GPIO_AF, GPIO_PUSH_PULL);
    Gpio_Init(GPIO_B, 5, GPIO_AF, GPIO_PUSH_PULL);
    Gpio_SetAF(GPIO_B, 3, GPIO_AF5);
    Gpio_SetAF(GPIO_B, 4, GPIO_AF5);
    Gpio_SetAF(GPIO_B, 5, GPIO_AF5);

    if (MasterSlave == SPI_MASTER) {
        /* Master ignores its own hardware NSS so we can toggle PA4 manually */
        SPI1->CR1 |= (1U << SPI_CR1_SSM_Pos);
        SPI1->CR1 |= (1U << SPI_CR1_SSI_Pos);
    } else {
        /* Slave uses Hardware NSS. Map Slave PA4 to SPI1_NSS (AF5) */
        /* Using direct registers here to guarantee it configures correctly */
        GPIOA->MODER &= ~(3U << (4 * 2));
        GPIOA->MODER |=  (2U << (4 * 2)); /* Alternate Function */
        GPIOA->AFR[0] &= ~(0xFU << (4 * 4));
        GPIOA->AFR[0] |=  (5U << (4 * 4)); /* AF5 */
        
        SPI1->CR1 &= ~(1U << SPI_CR1_SSM_Pos); /* Enable Hardware NSS */
    }

    /* Master/Slave Mode */
    SPI1->CR1 &= ~(1U << SPI_CR1_MSTR_Pos);
    SPI1->CR1 |= (MasterSlave << SPI_CR1_MSTR_Pos);

    /* Clock Polarity & Phase */
    SPI1->CR1 &= ~(1U << SPI_CR1_CPOL_Pos);
    SPI1->CR1 |= (ClkPol << SPI_CR1_CPOL_Pos);

    SPI1->CR1 &= ~(1U << SPI_CR1_CPHA_Pos);
    SPI1->CR1 |= (ClkPhase << SPI_CR1_CPHA_Pos);

    /* Slow Baud Rate for Proteus Interrupts (fPCLK/256) */
    SPI1->CR1 |= (0x7U << SPI_CR1_BR_Pos);

    /* Enable SPI */
    SPI1->CR1 |= (1U << SPI_CR1_SPE_Pos);

    s_isMaster = MasterSlave;
}

/* --- Your Original Blocking Transfer --- */
uint8 Spi1_TransmitReceiveByte(uint8 TxData, uint8* RxData) {
  if (SPI1->SR & (1 << SPI_SR_TXE_Pos)) {
    SPI1->DR = TxData;
    while (SPI1->SR & (1 << SPI_SR_BSY_Pos));
    *RxData = SPI1->DR;
    return SPI_OK;
  }
  return SPI_NOK;
}

/* --- NEW: Master Non-Blocking Function --- */
uint8 Spi1_MasterTransferAsync(uint8 *txBuf, uint8 *rxBuf, uint8 len, SpiCallback Callback) {
    if (Master_Busy) return SPI_NOK;   

    Master_TxBuf = txBuf;
    Master_RxBuf = rxBuf;
    Master_Len = len;
    Master_TxIndex = 0;
    Master_RxIndex = 0;
    Master_Callback = Callback;
    Master_Busy = 1;

    /* Enable SPI Interrupt in NVIC (Position 35) */
    Nvic_EnableIrq(35U);

    /* Enable RX Interrupt */
    SPI1->CR2 |= SPI_CR2_RXNEIE;

    /* Kick off the first byte to trigger TXE */
    Master_TxIndex = 1;
    SPI1->DR = txBuf[0];          
    SPI1->CR2 |= SPI_CR2_TXEIE;


    return SPI_OK;
}

/* --- NEW: Slave Non-Blocking Functions --- */
void Spi1_SlavePreload(uint8 *txBuf, uint8 len) {
    __disable_irq();
    for (uint8 i = 0; i < len; i++) {
        slaveTxShadow[i] = txBuf[i];
    }
    slaveTxIndex = 1;         
    slaveRxIndex = 0;
    
    /* Load first byte immediately so it's ready when master clocks */
    SPI1->DR = slaveTxShadow[0];

    /* Enable TX interrupt to queue the rest */
    SPI1->CR2 |= SPI_CR2_TXEIE;
    __enable_irq();
}

void Spi1_SlaveEnableRx(SpiCallback Callback) {
    Slave_Callback = Callback;
    Nvic_EnableIrq(35U);
    SPI1->CR2 |= SPI_CR2_RXNEIE;
}

void Spi1_SlaveGetRxBuffer(uint8 *out) {
    __disable_irq();
    for (uint8 i = 0; i < SPI_PACKET_LEN; i++) {
        out[i] = slaveRxDone[i];
    }
    __enable_irq();
}

/* --- NEW: The Interrupt Service Routine --- */
void SPI1_IRQHandler(void) {
    uint32 sr = SPI1->SR;

    if (s_isMaster) {
        if (Master_Busy) {
            if ((sr & SPI_SR_TXE) && (SPI1->CR2 & SPI_CR2_TXEIE)) {
                if (Master_TxIndex < Master_Len) {
                    SPI1->DR = Master_TxBuf[Master_TxIndex];
                    Master_TxIndex++;
                } else {
                    SPI1->CR2 &= ~SPI_CR2_TXEIE;
                }
            }
            if (sr & SPI_SR_RXNE) {
                Master_RxBuf[Master_RxIndex] = (uint8)SPI1->DR;
                Master_RxIndex++;
                if (Master_RxIndex >= Master_Len) {
                    SPI1->CR2 &= ~(SPI_CR2_RXNEIE | SPI_CR2_TXEIE);
                    Master_Busy = 0;
                    if (Master_Callback) Master_Callback();
                }
            }
        }
    } else {
        if ((sr & SPI_SR_TXE) && (SPI1->CR2 & SPI_CR2_TXEIE)) {
            if (slaveTxIndex < SPI_PACKET_LEN) {
                SPI1->DR = slaveTxShadow[slaveTxIndex];
                slaveTxIndex++;
            } else {
                SPI1->CR2 &= ~SPI_CR2_TXEIE;
            }
        }
        if (sr & SPI_SR_RXNE) {
            slaveRxBuf[slaveRxIndex] = (uint8)SPI1->DR;
            slaveRxIndex++;
            if (slaveRxIndex >= SPI_PACKET_LEN) {
                for (uint8 i = 0; i < SPI_PACKET_LEN; i++) {
                    slaveRxDone[i] = slaveRxBuf[i];
                }
                slaveRxIndex = 0;
                if (Slave_Callback) Slave_Callback();
            }
        }
    }
}