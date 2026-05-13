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


static uint8 g_isMaster = 0U;
static Elevator_t g_elev;
static RemoteState_t g_remote;

void SysTick_Handler(void)
{
    Telemetry_TickMs();
}


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


static void Cb_Sensor_F1(void) { FSM_FloorReached(&g_elev, FLOOR_1); }
static void Cb_Sensor_F2(void) { FSM_FloorReached(&g_elev, FLOOR_2); }
static void Cb_Sensor_F3(void) { FSM_FloorReached(&g_elev, FLOOR_3); }
static void Cb_Sensor_F4(void) { FSM_FloorReached(&g_elev, FLOOR_4); }

static void Cb_Hall_U1(void) { Dispatch_RegisterCall(FLOOR_1, DIR_UP);   }
static void Cb_Hall_D2(void) { Dispatch_RegisterCall(FLOOR_2, DIR_DOWN); }
static void Cb_Hall_U2(void) { Dispatch_RegisterCall(FLOOR_2, DIR_UP);   }
static void Cb_Hall_D3(void) { Dispatch_RegisterCall(FLOOR_3, DIR_DOWN); }
static void Cb_Hall_U3(void) { Dispatch_RegisterCall(FLOOR_3, DIR_UP);   }
static void Cb_Hall_D4(void) { Dispatch_RegisterCall(FLOOR_4, DIR_DOWN); }

static void Init_SharedInputs(void)
{
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

void SysTick_Init(void);

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
    IPC_Init(g_isMaster, &g_elev);

    /* ── 6. Role-specific SPI + timer init ───────────────── */
    if (g_isMaster)
    {
        Dispatch_Init();
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