/**
 * Usart.h
 *
 *  Created on: Wed May 24 2023
 *  Author    : Abdullah Darwish
 */

#ifndef USART_H
#define USART_H

#include "Std_Types.h"
#define Tx_OK    0U
#define Tx_NOK   1U

void Usart1_Init(void);

uint8 Usart1_TransmitByte(uint8 Byte);

uint8 Usart1_ReceiveByte(void);

void Usart1_TransmitString(const char* Str);

/*
 * Non‑blocking read of a single byte.
 * Returns 0 if no data, else 1 and the byte in *data.
 */
uint8 Usart1_GetByte(uint8 *data);

/*
 * Extracts a complete line (terminated by '\n' or '\r') from the ring buffer.
 * The line is stored in 'buffer' with a null terminator (maxLen-1 chars max).
 * Returns NULL if no full line is available.
 * The caller must ensure buffer has at least maxLen bytes.
 */
char* Usart1_ReadLine(char *buffer, uint32 maxLen);

#endif /* USART_H */
