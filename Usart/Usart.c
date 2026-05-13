/**
 * Usart.c
 *
 *  Created on: Wed May 24 2023
 *  Author    : Abdullah Darwish
 */

#include "stm32f401xe.h"
#include "Usart.h"
#include "Gpio.h"
#include "RingBuffer.h"
#include "Std_Types.h"

static RingBufferType usart1_rx_ring;

void Usart1_Init(void) {
    Gpio_Init(GPIO_A, 9, GPIO_AF, GPIO_PUSH_PULL);
    Gpio_Init(GPIO_A, 10, GPIO_AF, GPIO_PUSH_PULL);
    
    Gpio_SetAF(GPIO_A, 9, GPIO_AF7);
    Gpio_SetAF(GPIO_A, 10, GPIO_AF7);

    
    USART1->CR1 &= ~(1 << USART_CR1_M_Pos); // 8-bit word length

    USART1->CR2 &= ~(USART_CR2_STOP_Msk); // 1-stop bit at the end

    USART1->CR1 &= ~(1 << USART_CR1_OVER8_Pos); // 16 over sampling

    USART1->BRR = 0x8B; // Baud Rate 9600

    /* Enable Transmission block */
    USART1->CR1 |= (1 << USART_CR1_TE_Pos);

    // /* Enable Receive block */
    USART1->CR1 |= (1 << USART_CR1_RE_Pos);
    
    
    // RB_Init(&usart1_rx_ring);
    
    // // Enable RX Not Empty interrupt
    // USART1->CR1 |= USART_CR1_RXNEIE;
    
    // Nvic_EnableIrq(USART1_IRQn);
    
    /* Enable USART1 */
    USART1->CR1 |= (1 << USART_CR1_UE_Pos);
}

uint8 Usart1_TransmitByte(uint8 Byte) {
    if (USART1->SR & USART_SR_TXE_Msk) {
        USART1->DR = Byte;
        while (!(USART1->SR & USART_SR_TC_Msk));
        USART1->SR &= ~(USART_SR_TC_Msk); // Clearing TC bit
        return Tx_OK;
    }
    return Tx_NOK;
}

void Usart1_TransmitString(const char *Str) {
    uint32 i = 0;
    uint8 transmitResult = -1;
    while (Str[i] != '\0') {
        transmitResult = Usart1_TransmitByte(Str[i]);
        if (transmitResult == Tx_OK) {
            i++;
        }
    }
}

uint8 Usart1_ReceiveByte(void) {
    while (!(USART1->SR & USART_SR_RXNE_Msk));
    return USART1->DR;
}

/*
 * Non‑blocking read of a single byte.
 * Returns 0 if no data, else 1 and the byte in *data.
 * This is useful for polling.
 */
uint8 Usart1_GetByte(uint8 *data) {
    return RB_Dequeue(&usart1_rx_ring, data);
}

/*
 * Extracts a complete line (terminated by '\n' or '\r') from the ring buffer.
 * The line is stored in 'buffer' with a null terminator (maxLen-1 chars max).
 * Returns NULL if no full line is available.
 * The caller must ensure buffer has at least maxLen bytes.
 */
static uint32 idx = 0;
char* Usart1_ReadLine(char *buffer, uint32 maxLen) {
    uint8 ch;
    
    // Peek until newline or buffer would overflow
    while (RB_Available(&usart1_rx_ring) > 0) {
        ch = RB_Peek(&usart1_rx_ring);
        if (ch == '\n' || ch == '\r') {
            // Found line end, consume it and break
            RB_Dequeue(&usart1_rx_ring, &ch);
            // Optionally, consume the paired \r\n:
            if (ch == '\r') {
                if (RB_Available(&usart1_rx_ring) > 0) {
                    uint8 next = RB_Peek(&usart1_rx_ring);
                    if (next == '\n') {
                        RB_Dequeue(&usart1_rx_ring, &next);
                    }
                }
            } else if (ch == '\n') {
                if (RB_Available(&usart1_rx_ring) > 0) {
                    uint8 next = RB_Peek(&usart1_rx_ring);
                    if (next == '\r') {
                        RB_Dequeue(&usart1_rx_ring, &next);
                    }
                }
            }
            buffer[idx] = '\0';
            idx = 0;
            return buffer;
        } else {
            // Regular character, consume and store
            RB_Dequeue(&usart1_rx_ring, &ch);
            if (idx < (maxLen - 1)) {
                buffer[idx] = ch;
                idx++;
            } else {
                // buffer full, discard remaining chars until newline
                // return NULL to indicate truncation?
                // For simplicity, just break and return NULL.
                return NULL;
            }
        }
    }
    return NULL;  // no complete line yet
}

void USART1_IRQHandler(void) {
    // Check if RXNE interrupt is the source
    if (USART1->SR & USART_SR_RXNE) {
        uint8 byte = USART1->DR;   // reading DR clears RXNE flag
        RB_Enqueue(&usart1_rx_ring, byte);
    }
}