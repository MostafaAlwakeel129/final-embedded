/**
 * Spi.h
 */

#ifndef SPI_H
#define SPI_H

#include "Std_Types.h"

typedef struct 
{
    uint32 CR1;
    uint32 UNUSED;
    uint32 SR;
    uint32 DR;
    uint32 CRCPR;
    uint32 RXCRCR;
    uint32 TXCRCR;
    uint32 I2SCFGR;
    uint32 I2SPR;
} SpiType;

/* Master or Slave */
#define SPI_SLAVE  0
#define SPI_MASTER 1

/* Clock Polarity */
#define SPI_IDLE_LOW  0
#define SPI_IDLE_HIGH 1

/* Clock Phase */
#define SPI_SAMPLE_FIRST_TRANSITION  0
#define SPI_SAMPLE_SECOND_TRANSITION 1

#define SPI_OK     0U
#define SPI_NOK    1U

#define SPI_PACKET_LEN 8U

typedef void (*SpiCallback)(void);

/* Your original Init */
void Spi1_Init(uint8 MasterSlave, uint8 ClkPol, uint8 ClkPhase);

/* Your original blocking function (good for debugging) */
uint8 Spi1_TransmitReceiveByte(uint8 TxData, uint8* RxData);

/* --- NEW NON-BLOCKING FUNCTIONS --- */
uint8 Spi1_MasterTransferAsync(uint8 *txBuf, uint8 *rxBuf, uint8 len, SpiCallback Callback);
void Spi1_SlavePreload(uint8 *txBuf, uint8 len);
void Spi1_SlaveEnableRx(SpiCallback Callback);
void Spi1_SlaveGetRxBuffer(uint8 *out);

#endif /* SPI_H */