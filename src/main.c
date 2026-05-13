/**
 * main.c  —  Collaborative Dual-Elevator System (Single Hex, Dual Role)
 *
 * Role selection at runtime via PC10:
 *   PC10 pulled LOW (jumper to GND) = MASTER (Dispatcher + Elevator A)
 *   PC10 floating   (internal pull-up, no jumper) = SLAVE  (Elevator B)
 *
 * ── MASTER pin map ────────────────────────────────────────────────────
 *  INPUTS (active-low, internal pull-up):
 *    PC0  EXTI0   Cabin button  Floor 1
 *    PC1  EXTI1   Cabin button  Floor 2
 *    PC2  EXTI2   Cabin button  Floor 3
 *    PC3  EXTI3   Cabin button  Floor 4
 *    PC4  EXTI4   Emergency stop
 *    PC5  EXTI5   Hall call U1  (Floor 1 Up)
 *    PD6  EXTI6   Hall call D2  (Floor 2 Down)
 *    PD7  EXTI7   Hall call U2  (Floor 2 Up)
 *    PD8  EXTI8   Hall call D3  (Floor 3 Down)
 *    PD9  EXTI9   Hall call U3  (Floor 3 Up)
 *    PD10 EXTI10  Hall call D4  (Floor 4 Down)
 *    PD11 EXTI11  Floor sensor  Floor 1
 *    PD12 EXTI12  Floor sensor  Floor 2
 *    PD14 EXTI14  Floor sensor  Floor 3
 *    PD15 EXTI15  Floor sensor  Floor 4
 *
 *  OUTPUTS:
 *    PB6  TIM4_CH1 AF2   Motor LED (PWM)
 *    PD13 GPIO_OUTPUT    Emergency LED
 *
 *  SPI1 Master:
 *    PA4  GPIO_OUTPUT    CS (active-low)
 *    PA5  AF5            SCK
 *    PA6  AF5            MISO
 *    PA7  AF5            MOSI
 *
 *  UART1:  PA9 TX (AF7),  PA10 RX (AF7)
 *  TIM2:   50 ms periodic IPC tick
 *  TIM3:   door timer (owned by FSM)
 *  TIM4:   motor PWM  (owned by FSM)
 *
 * ── SLAVE pin map ─────────────────────────────────────────────────────
 *  INPUTS (active-low, internal pull-up):
 *    PC0  EXTI0   Cabin button  Floor 1
 *    PC1  EXTI1   Cabin button  Floor 2
 *    PC2  EXTI2   Cabin button  Floor 3
 *    PC3  EXTI3   Cabin button  Floor 4
 *    PC4  EXTI4   Emergency stop
 *    PD11 EXTI11  Floor sensor  Floor 1
 *    PD12 EXTI12  Floor sensor  Floor 2
 *    PD14 EXTI14  Floor sensor  Floor 3
 *    PD15 EXTI15  Floor sensor  Floor 4
 *
 *  OUTPUTS:
 *    PB6  TIM4_CH1 AF2   Motor LED (PWM)
 *    PD13 GPIO_OUTPUT    Emergency LED
 *
 *  SPI1 Slave:
 *    PA4  AF5  NSS  (hardware, driven by Master)
 *    PA5  AF5  SCK
 *    PA6  AF5  MISO
 *    PA7  AF5  MOSI
 *
 *  UART1:  PA9 TX (AF7),  PA10 RX (AF7)
 *  TIM3:   door timer (owned by FSM)
 *  TIM4:   motor PWM  (owned by FSM)
 */

#include "Rcc.h"
#include "Gpio.h"
#include "Exti.h"
#include "Nvic.h"
#include "Usart.h"
#include "Spi.h"
#include "Timer.h"
#include "Fsm.h"
#include "Telemetry.h"
#include "Ipc.h"
#include "Dispatch.h"
#include "project.h"

/* =========================================================
 * Role flag — set once at startup by reading PC10
 * ========================================================= */
static uint8 g_isMaster = 0U;

/* =========================================================
 * Shared elevator instance (both roles use this)
 * ========================================================= */
static Elevator_t g_elev;

/* =========================================================
 * Master-only: remote state snapshot updated each IPC tick
 * ========================================================= */
static RemoteState_t g_remote;

/* =========================================================
 * SPI packet buffers (Master)
 * ========================================================= */
static uint8 g_spiTxBuf[SPI_PACKET_LEN];
static uint8 g_spiRxBuf[SPI_PACKET_LEN];

/* =========================================================
 * CS pin helpers (Master only)
 * ========================================================= */
static void CS_Low(void)
{
    Gpio_WritePin(GPIO_A, 4, LOW);
}

static void CS_High(void)
{
    Gpio_WritePin(GPIO_A, 4, HIGH);
}

/* =========================================================
 * SPI transfer complete callback (Master)
 * Runs in ISR context — keep minimal.
 * ========================================================= */
static void Master_SpiDoneCallback(void)
{
    IPC_Packet_t rxPkt;
    uint8 i;

    CS_High();

    /* Copy raw bytes into packet struct */
    for (i = 0U; i < SPI_PACKET_LEN; i++)
    {
        ((uint8 *)&rxPkt)[i] = g_spiRxBuf[i];
    }

    /* Decode — updates internal s_remoteState and health flag */
    IPC_DecodeRxPacket(&rxPkt, &g_remote);
}

/* =========================================================
 * SPI receive complete callback (Slave)
 * Runs in ISR context — keep minimal.
 * ========================================================= */
static void Slave_SpiDoneCallback(void)
{
    uint8 rawBuf[SPI_PACKET_LEN];
    IPC_Packet_t rxPkt;
    IPC_Packet_t txPkt;
    uint8 i;

    /* Read what Master sent */
    Spi1_SlaveGetRxBuffer(rawBuf);
    for (i = 0U; i < SPI_PACKET_LEN; i++)
    {
        ((uint8 *)&rxPkt)[i] = rawBuf[i];
    }

    /* Decode master packet to check for target floor command */
    if (IPC_DecodeRxPacket(&rxPkt, &g_remote) != 0U)
    {
        /*
         * Master encodes target floor for slave in the reserved byte.
         * If non-zero, treat it as a new target assignment.
         */
        if (rxPkt.reserved != 0U)
        {
            FSM_SetTarget(&g_elev, rxPkt.reserved);
        }
    }

    /* Pre-load our state for the next transfer */
    IPC_EncodeTxPacket(&g_elev, &txPkt);
    for (i = 0U; i < SPI_PACKET_LEN; i++)
    {
        rawBuf[i] = ((uint8 *)&txPkt)[i];
    }
    Spi1_SlavePreload(rawBuf, SPI_PACKET_LEN);
}

/* =========================================================
 * IPC tick callback — called from TIM2 ISR every 50 ms
 * Master only: encodes local state, drives CS, starts transfer
 * ========================================================= */
static void Master_IpcTick(void)
{
    IPC_Packet_t txPkt;
    uint8 i;
    uint8 pendingFloor;

    IPC_Tick();   /* increments missed-tick counter */

    /* Encode local elevator state into TX packet */
    IPC_EncodeTxPacket(&g_elev, &txPkt);

    /*
     * Piggyback pending target floor for Slave in reserved byte.
     * IPC_SendTargetFloor() stores it; we consume it here.
     */
    IPC_GetRemoteState(&g_remote);   /* re-use function; pendingFloor is internal */
    /* Access pending floor via the IPC module's internal latch */
    /* (IPC_SendTargetFloor stores to s_pendingFloor; we expose it via reserved) */
    /* We encode it directly: Dispatch calls IPC_SendTargetFloor(floor), which   */
    /* sets s_pendingFloor. We read it back through a dedicated getter below.    */
    pendingFloor = IPC_ConsumePendingFloor();   /* see Ipc addition below */
    txPkt.reserved = pendingFloor;
    txPkt.checksum = (uint8)(txPkt.header ^ txPkt.state ^ txPkt.floor
                             ^ txPkt.direction ^ txPkt.speed
                             ^ txPkt.flags ^ txPkt.reserved);

    for (i = 0U; i < SPI_PACKET_LEN; i++)
    {
        g_spiTxBuf[i] = ((uint8 *)&txPkt)[i];
    }

    CS_Low();
    Spi1_MasterTransferAsync(g_spiTxBuf, g_spiRxBuf,
                             SPI_PACKET_LEN, Master_SpiDoneCallback);
}

/* =========================================================
 * 1 ms SysTick — feeds FSM telemetry counter
 * ========================================================= */
void SysTick_Handler(void)
{
    Telemetry_TickMs();
}

/* =========================================================
 * ── SHARED EXTI CALLBACKS ─────────────────────────────────
 * Cabin buttons and emergency are identical on both boards.
 * Floor sensors are wired the same on both boards.
 * ========================================================= */

/* Cabin buttons */
static void Cb_Cabin_F1(void) { FSM_SetTarget(&g_elev, FLOOR_1); }
static void Cb_Cabin_F2(void) { FSM_SetTarget(&g_elev, FLOOR_2); }
static void Cb_Cabin_F3(void) { FSM_SetTarget(&g_elev, FLOOR_3); }
static void Cb_Cabin_F4(void) { FSM_SetTarget(&g_elev, FLOOR_4); }

/* Emergency stop */
static void Cb_Emergency(void)
{
    FSM_EmergencyStop(&g_elev);
    Gpio_WritePin(GPIO_D, 13, HIGH);   /* emergency LED on */
}

static void Cb_Emergency_Clear(void)
{
    FSM_ClearEmergency(&g_elev);
    Gpio_WritePin(GPIO_D, 13, LOW);   /* emergency LED off */
}

/* Floor sensors */
static void Cb_Sensor_F1(void) { FSM_FloorReached(&g_elev, FLOOR_1); }
static void Cb_Sensor_F2(void) { FSM_FloorReached(&g_elev, FLOOR_2); }
static void Cb_Sensor_F3(void) { FSM_FloorReached(&g_elev, FLOOR_3); }
static void Cb_Sensor_F4(void) { FSM_FloorReached(&g_elev, FLOOR_4); }

/* =========================================================
 * ── MASTER-ONLY EXTI CALLBACKS ────────────────────────────
 * Hall calls — registered only when g_isMaster == 1
 * ========================================================= */
static void Cb_Hall_U1(void) { Dispatch_RegisterCall(FLOOR_1, DIR_UP);   }
static void Cb_Hall_D2(void) { Dispatch_RegisterCall(FLOOR_2, DIR_DOWN); }
static void Cb_Hall_U2(void) { Dispatch_RegisterCall(FLOOR_2, DIR_UP);   }
static void Cb_Hall_D3(void) { Dispatch_RegisterCall(FLOOR_3, DIR_DOWN); }
static void Cb_Hall_U3(void) { Dispatch_RegisterCall(FLOOR_3, DIR_UP);   }
static void Cb_Hall_D4(void) { Dispatch_RegisterCall(FLOOR_4, DIR_DOWN); }

/* =========================================================
 * GPIO + EXTI init helpers
 * ========================================================= */
static void Init_SharedInputs(void)
{
    /* PC0–PC4: cabin buttons + emergency (both boards) */
    Gpio_Init(GPIO_C, 0, GPIO_INPUT, GPIO_PULL_UP);
    Gpio_Init(GPIO_C, 1, GPIO_INPUT, GPIO_PULL_UP);
    Gpio_Init(GPIO_C, 2, GPIO_INPUT, GPIO_PULL_UP);
    Gpio_Init(GPIO_C, 3, GPIO_INPUT, GPIO_PULL_UP);
    Gpio_Init(GPIO_C, 4, GPIO_INPUT, GPIO_PULL_UP);
    Gpio_Init(GPIO_C, 13, GPIO_INPUT, GPIO_PULL_UP);

    /* PD11, PD12, PD14, PD15: floor sensors (both boards) */
    Gpio_Init(GPIO_D, 11, GPIO_INPUT, GPIO_PULL_UP);
    Gpio_Init(GPIO_D, 12, GPIO_INPUT, GPIO_PULL_UP);
    Gpio_Init(GPIO_D, 14, GPIO_INPUT, GPIO_PULL_UP);
    Gpio_Init(GPIO_D, 15, GPIO_INPUT, GPIO_PULL_UP);

    /* EXTI — cabin buttons on Port C, falling edge (active-low) */
    Exti_Init(EXTI_LINE_0, EXTI_PORT_C, EXTI_EDGE_FALLING, Cb_Cabin_F1);
    Exti_Init(EXTI_LINE_1, EXTI_PORT_C, EXTI_EDGE_FALLING, Cb_Cabin_F2);
    Exti_Init(EXTI_LINE_2, EXTI_PORT_C, EXTI_EDGE_FALLING, Cb_Cabin_F3);
    Exti_Init(EXTI_LINE_3, EXTI_PORT_C, EXTI_EDGE_FALLING, Cb_Cabin_F4);
    Exti_Init(EXTI_LINE_4, EXTI_PORT_C, EXTI_EDGE_FALLING, Cb_Emergency);
    Exti_Init(EXTI_LINE_13, EXTI_PORT_C, EXTI_EDGE_FALLING, Cb_Emergency_Clear);

    /* EXTI — floor sensors on Port D, falling edge */
    Exti_Init(EXTI_LINE_11, EXTI_PORT_D, EXTI_EDGE_FALLING, Cb_Sensor_F1);
    Exti_Init(EXTI_LINE_12, EXTI_PORT_D, EXTI_EDGE_FALLING, Cb_Sensor_F2);
    Exti_Init(EXTI_LINE_14, EXTI_PORT_D, EXTI_EDGE_FALLING, Cb_Sensor_F3);
    Exti_Init(EXTI_LINE_15, EXTI_PORT_D, EXTI_EDGE_FALLING, Cb_Sensor_F4);

    /* NVIC priorities — emergency gets highest (0), rest get 3 */
    SetNvicPriority(6,  3U);   /* EXTI0  — cabin F1       */
    SetNvicPriority(7,  3U);   /* EXTI1  — cabin F2       */
    SetNvicPriority(8,  3U);   /* EXTI2  — cabin F3       */
    SetNvicPriority(9,  3U);   /* EXTI3  — cabin F4       */
    SetNvicPriority(10, 0U);   /* EXTI4  — EMERGENCY      */
    SetNvicPriority(23, 3U);   /* EXTI9_5                 */
    SetNvicPriority(40, 3U);   /* EXTI15_10               */

    /* Enable shared lines */
    Exti_Enable(EXTI_LINE_0);
    Exti_Enable(EXTI_LINE_1);
    Exti_Enable(EXTI_LINE_2);
    Exti_Enable(EXTI_LINE_3);
    Exti_Enable(EXTI_LINE_4);
    Exti_Enable(EXTI_LINE_11);
    Exti_Enable(EXTI_LINE_12);
    Exti_Enable(EXTI_LINE_13);
    Exti_Enable(EXTI_LINE_14);
    Exti_Enable(EXTI_LINE_15);
}

static void Init_MasterOnlyInputs(void)
{
    /* PC5: hall call U1 */
    Gpio_Init(GPIO_C, 5, GPIO_INPUT, GPIO_PULL_UP);

    /* PD6, PD7, PD8, PD9, PD10: hall calls D2, U2, D3, U3, D4 */
    Gpio_Init(GPIO_D, 6,  GPIO_INPUT, GPIO_PULL_UP);
    Gpio_Init(GPIO_D, 7,  GPIO_INPUT, GPIO_PULL_UP);
    Gpio_Init(GPIO_D, 8,  GPIO_INPUT, GPIO_PULL_UP);
    Gpio_Init(GPIO_D, 9,  GPIO_INPUT, GPIO_PULL_UP);
    Gpio_Init(GPIO_D, 10, GPIO_INPUT, GPIO_PULL_UP);

    Exti_Init(EXTI_LINE_5,  EXTI_PORT_C, EXTI_EDGE_FALLING, Cb_Hall_U1);
    Exti_Init(EXTI_LINE_6,  EXTI_PORT_D, EXTI_EDGE_FALLING, Cb_Hall_D2);
    Exti_Init(EXTI_LINE_7,  EXTI_PORT_D, EXTI_EDGE_FALLING, Cb_Hall_U2);
    Exti_Init(EXTI_LINE_8,  EXTI_PORT_D, EXTI_EDGE_FALLING, Cb_Hall_D3);
    Exti_Init(EXTI_LINE_9,  EXTI_PORT_D, EXTI_EDGE_FALLING, Cb_Hall_U3);
    Exti_Init(EXTI_LINE_10, EXTI_PORT_D, EXTI_EDGE_FALLING, Cb_Hall_D4);

    Exti_Enable(EXTI_LINE_5);
    Exti_Enable(EXTI_LINE_6);
    Exti_Enable(EXTI_LINE_7);
    Exti_Enable(EXTI_LINE_8);
    Exti_Enable(EXTI_LINE_9);
    Exti_Enable(EXTI_LINE_10);
}

static void Init_Outputs(void)
{
    /* Motor LED — AF2 for TIM4_CH1 */
    Gpio_Init(GPIO_B, 6, GPIO_AF, GPIO_PUSH_PULL);
    Gpio_SetAF(GPIO_B, 6, GPIO_AF2);

    /* Emergency LED */
    Gpio_Init(GPIO_D, 13, GPIO_OUTPUT, GPIO_PUSH_PULL);
    Gpio_WritePin(GPIO_D, 13, LOW);
}

static void Init_Uart(void)
{
    /* PA9 TX, PA10 RX — AF7 */
    Gpio_Init(GPIO_A, 9,  GPIO_AF, GPIO_PUSH_PULL);
    Gpio_Init(GPIO_A, 10, GPIO_AF, GPIO_PUSH_PULL);
    Gpio_SetAF(GPIO_A, 9,  GPIO_AF7);
    Gpio_SetAF(GPIO_A, 10, GPIO_AF7);
    Usart1_Init();
}

static void Init_Spi_Master(void)
{
    /* PA4 CS — manual GPIO output, start HIGH (deasserted) */
    Gpio_Init(GPIO_A, 4, GPIO_OUTPUT, GPIO_PUSH_PULL);
    CS_High();

    Spi1_Init(SPI_MASTER, SPI_IDLE_LOW, SPI_SAMPLE_FIRST_TRANSITION);
}

static void Init_Spi_Slave(void)
{
    /* PA4 NSS is configured inside Spi1_Init when MasterSlave==SPI_SLAVE */

    Spi1_Init(SPI_SLAVE, SPI_IDLE_LOW, SPI_SAMPLE_FIRST_TRANSITION);

    /* Pre-load an empty (IDLE at Floor 1) packet so Slave is ready
     * for the very first Master transfer */
    {
        IPC_Packet_t initPkt;
        uint8 buf[SPI_PACKET_LEN];
        uint8 i;
        IPC_EncodeTxPacket(&g_elev, &initPkt);
        for (i = 0U; i < SPI_PACKET_LEN; i++)
        {
            buf[i] = ((uint8 *)&initPkt)[i];
        }
        Spi1_SlavePreload(buf, SPI_PACKET_LEN);
        Spi1_SlaveEnableRx(Slave_SpiDoneCallback);
    }
}

/* =========================================================
 * SysTick setup — 1 ms tick at 16 MHz HSI
 * ========================================================= */
static void SysTick_Init(void)
{
    /* SysTick reload for 1 ms: 16000000 / 1000 - 1 = 15999 */
    volatile uint32 *SYST_RVR = (volatile uint32 *)0xE000E014;
    volatile uint32 *SYST_CVR = (volatile uint32 *)0xE000E018;
    volatile uint32 *SYST_CSR = (volatile uint32 *)0xE000E010;

    *SYST_RVR = 15999UL;
    *SYST_CVR = 0UL;
    /* CLKSOURCE=1 (processor clock), TICKINT=1, ENABLE=1 */
    *SYST_CSR = 0x07UL;
}

/* =========================================================
 * main
 * ========================================================= */
int main(void)
{
    /* ── 1. Enable all required clocks ───────────────────── */
    Rcc_Enable(RCC_GPIOA);    /* SPI, UART pins              */
    Rcc_Enable(RCC_GPIOB);    /* Motor LED PB6               */
    Rcc_Enable(RCC_GPIOC);    /* Cabin buttons, hall U1,     */
                              /* role-strap PC10             */
    Rcc_Enable(RCC_GPIOD);    /* Hall calls, sensors,        */
                              /* emergency LED PD13          */
    Rcc_Enable(RCC_SYSCFG);   /* EXTI routing                */
    Rcc_Enable(RCC_SPI1);     /* SPI1                        */
    Rcc_Enable(RCC_TIM2);     /* IPC periodic tick (Master)  */
    Rcc_Enable(RCC_TIM3);     /* Door timer (FSM)            */
    Rcc_Enable(RCC_TIM4);     /* Motor PWM (FSM)             */
    Rcc_Enable(RCC_USART1);   /* Telemetry UART              */

    /* ── 2. Read role-select strap on PC10 ──────────────── */
    /*    Configure as input with pull-up first              */
    Gpio_Init(GPIO_C, 10, GPIO_INPUT, GPIO_PULL_UP);
    /*    Small delay to let pin settle                      */
    {
        volatile uint32 d = 10000U;
        while (d--) {}
    }
    /*    LOW  = jumper fitted  = MASTER                     */
    /*    HIGH = no jumper      = SLAVE                      */
    g_isMaster = (Gpio_ReadPin(GPIO_C, 10) == 0U) ? 1U : 0U;

    /* ── 3. Common hardware init ─────────────────────────── */
    Init_Outputs();
    Init_Uart();

    if (g_isMaster)
    {
        Usart1_TransmitString("=== MASTER BOOT ===\r\n");
    }
    else
    {
        Usart1_TransmitString("=== SLAVE BOOT ===\r\n");
    }

    /* ── 4. FSM init (both roles) ────────────────────────── */
    FSM_Init(&g_elev);

    /* ── 5. IPC init ─────────────────────────────────────── */
    IPC_Init();

    /* ── 6. Role-specific SPI + timer init ───────────────── */
    if (g_isMaster)
    {
        Init_Spi_Master();

        /* TIM2: 50 ms periodic IPC tick */
        Timer_StartPeriodic(TIMER2, 6U, Master_IpcTick);

        /* Dispatch system */
        Dispatch_Init();
    }
    else
    {
        Init_Spi_Slave();
        /* Slave has no IPC timer — it reacts to Master transfers */
    }

    /* ── 7. Input GPIO + EXTI init ───────────────────────── */
    Init_SharedInputs();

    if (g_isMaster)
    {
        Init_MasterOnlyInputs();
    }

    /* ── 8. SysTick for FSM telemetry counter ────────────── */
    SysTick_Init();

    /* ── 9. Main loop ────────────────────────────────────── */
    while (1)
    {
        /* Run local elevator FSM */
        FSM_Update(&g_elev);
        Telemetry_Update(&g_elev);
    
        /* Master: run dispatch algorithm every loop iteration.
         * Dispatch checks IPC health internally and falls back
         * to taking all calls if Slave is unreachable.        */
        if (g_isMaster)
        {
            IPC_GetRemoteState(&g_remote);
            Dispatch_RunAlgorithm(&g_elev, &g_remote);
        }
    }
}