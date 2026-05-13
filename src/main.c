/**
 * main_fsm_test.c
 *
 * FSM Module Isolation Test
 * -------------------------
 * Tests ALL 5 FSM state transitions on a single STM32F401 board.
 * No SPI, no IPC, no Dispatch — pure FSM + PWM + UART + EXTI + Timer.
 *
 * HOW TO USE:
 *   Press buttons in this order to walk through every transition:
 *
 *   Step 1 → Press PA0  : SetTarget(Floor 3) from Floor 1
 *             Expected   : UART "MOVING_UP", LED full brightness
 *
 *   Step 2 → Press PA1  : Floor 2 sensor fires
 *             Expected   : UART "MOVING_UP F:2" (not target, keeps moving)
 *
 *   Step 3 → Press PA2  : Floor 3 sensor fires (target reached)
 *             Expected   : UART "DOORS_OPEN F:3", LED off
 *
 *   Step 4 → Wait 3 sec : Door timer expires automatically
 *             Expected   : UART "IDLE F:3"
 *
 *   Step 5 → Press PA0  : SetTarget(Floor 3) again → already there
 *             Expected   : UART "DOORS_OPEN" immediately (no movement)
 *
 *   Step 6 → Wait 3 sec : Door timer expires
 *             Expected   : UART "IDLE"
 *
 *   Step 7 → Press PA0, then immediately press PA3 (Emergency)
 *             Expected   : LED off instantly, UART "EMERGENCY"
 *
 *   Step 8 → Press PA4  : Clear emergency
 *             Expected   : UART "IDLE", LED off
 *
 * Pin map (matches Proteus wiring table):
 *   PA0  — Button: SetTarget Floor 3        (EXTI0,  rising, pull-down)
 *   PA1  — Button: Floor 2 sensor           (EXTI1,  rising, pull-down)
 *   PA2  — Button: Floor 3 sensor           (EXTI2,  rising, pull-down)
 *   PA3  — Button: Emergency Stop           (EXTI3,  rising, pull-down)  ← highest NVIC priority
 *   PA4  — Button: Clear Emergency          (EXTI4,  rising, pull-down)
 *   PB6  — Motor LED  (TIM4_CH1, AF2)       ← PWM output
 *   PA9  — UART1 TX   (AF7)                 ← to Virtual Terminal
 *   PA10 — UART1 RX   (AF7)                 ← not used in this test
 *
 * Clocks used:
 *   RCC_GPIOA, RCC_GPIOB, RCC_SYSCFG
 *   RCC_TIM3  (door timer  — owned by FSM via Timer_DelayMsAsync)
 *   RCC_TIM4  (motor PWM   — owned by FSM via Pwm_Init)
 *   RCC_USART1
 *
 * Author: AbdallahDarwish
 */

#include "Rcc.h"
#include "Gpio.h"
#include "Exti.h"
#include "Fsm.h"
#include "Usart.h"
#include "Nvic.h"

/* =========================================================
 * Global elevator instance
 * Must be global so EXTI callbacks can reach it.
 * ========================================================= */
static Elevator_t elev;

/* =========================================================
 * EXTI callbacks
 * Each button simulates one real-world hardware event.
 * Keep them as short as possible — they run inside ISRs.
 * ========================================================= */

/* PA0 — Cabin button: request Floor 3 */
static void Btn_SetTarget3(void)
{
    FSM_SetTarget(&elev, FLOOR_3);
}

/* PA1 — Floor sensor: elevator passed Floor 2 */
static void Btn_Floor2Sensor(void)
{
    FSM_FloorReached(&elev, FLOOR_2);
}

/* PA2 — Floor sensor: elevator reached Floor 3 */
static void Btn_Floor3Sensor(void)
{
    FSM_FloorReached(&elev, FLOOR_3);
}

/* PA3 — Emergency stop (must be highest NVIC priority) */
static void Btn_EmergencyStop(void)
{
    FSM_EmergencyStop(&elev);
    Gpio_WritePin(GPIO_B, 6, HIGH);

}

/* PA4 — Clear emergency and return to IDLE */
static void Btn_ClearEmergency(void)
{
    FSM_ClearEmergency(&elev);
    Gpio_WritePin(GPIO_B, 6, LOW);
}

/* =========================================================
 * main
 * ========================================================= */
int main(void)
{
    /* --- 1. Enable clocks -------------------------------- */
    Rcc_Enable(RCC_GPIOA);    /* Buttons, UART pins          */
    Rcc_Enable(RCC_GPIOB);    /* Motor LED pin (PB6)         */
    Rcc_Enable(RCC_SYSCFG);   /* Required for EXTI routing   */
    Rcc_Enable(RCC_TIM3);     /* Door timer (used by FSM)    */
    Rcc_Enable(RCC_TIM4);     /* Motor PWM  (used by FSM)    */
    Rcc_Enable(RCC_USART1);   /* Telemetry UART              */

    /* --- 2. Configure GPIO pins -------------------------- */

    /* Motor LED: PB6 → TIM4_CH1 (AF2 on STM32F401)
     * Datasheet Table 9: TIM4_CH1 = PB6, AF2               */
    Gpio_Init(GPIO_B, 6, GPIO_OUTPUT, GPIO_PUSH_PULL);
    //Gpio_SetAF(GPIO_B, 6, GPIO_AF2);

    /* UART1: PA9 TX, PA10 RX → AF7                         */
    Gpio_Init(GPIO_A, 9,  GPIO_AF, GPIO_PUSH_PULL);
    Gpio_Init(GPIO_A, 10, GPIO_AF, GPIO_PUSH_PULL);
    Gpio_SetAF(GPIO_A, 9,  GPIO_AF7);
    Gpio_SetAF(GPIO_A, 10, GPIO_AF7);

    /* Button inputs: pull-down so pin reads 0 when open,
     * 1 when pressed (button connects pin to VCC)           */
    Gpio_Init(GPIO_A, 0, GPIO_INPUT, GPIO_PULL_DOWN); /* SetTarget3     */
    Gpio_Init(GPIO_A, 1, GPIO_INPUT, GPIO_PULL_DOWN); /* Floor2 sensor  */
    Gpio_Init(GPIO_A, 2, GPIO_INPUT, GPIO_PULL_DOWN); /* Floor3 sensor  */
    Gpio_Init(GPIO_A, 3, GPIO_INPUT, GPIO_PULL_DOWN); /* Emergency stop */
    Gpio_Init(GPIO_A, 4, GPIO_INPUT, GPIO_PULL_DOWN); /* Clear emerg.   */

    /* --- 3. Initialise UART ------------------------------ */
    Usart1_Init();
    Usart1_TransmitString("=== FSM TEST START ===\r\n");
    Usart1_TransmitString("Elevator at Floor 1, IDLE.\r\n");
    Usart1_TransmitString("PA0=SetTarget3  PA1=F2sensor  PA2=F3sensor\r\n");
    Usart1_TransmitString("PA3=Emergency   PA4=ClearEmerg\r\n\r\n");

    /* --- 4. Configure EXTI lines ------------------------- */

    /* Route all 5 lines to Port A */
    Exti_Init(EXTI_LINE_0, EXTI_PORT_A, EXTI_EDGE_RISING, Btn_SetTarget3);
    Exti_Init(EXTI_LINE_1, EXTI_PORT_A, EXTI_EDGE_RISING, Btn_Floor2Sensor);
    Exti_Init(EXTI_LINE_2, EXTI_PORT_A, EXTI_EDGE_RISING, Btn_Floor3Sensor);
    Exti_Init(EXTI_LINE_3, EXTI_PORT_A, EXTI_EDGE_RISING, Btn_EmergencyStop);
    Exti_Init(EXTI_LINE_4, EXTI_PORT_A, EXTI_EDGE_RISING, Btn_ClearEmergency);

    /*
     * Set NVIC priorities BEFORE enabling the lines.
     *
     * EXTI line → IRQ number (STM32F401 reference manual, Table 38):
     *   EXTI0   → IRQ  6
     *   EXTI1   → IRQ  7
     *   EXTI2   → IRQ  8
     *   EXTI3   → IRQ  9   ← Emergency — priority 0 (highest)
     *   EXTI4   → IRQ 10
     */
    SetNvicPriority(6,  3U);   /* SetTarget3    — low priority  */
    SetNvicPriority(7,  3U);   /* Floor2 sensor — low priority  */
    SetNvicPriority(8,  3U);   /* Floor3 sensor — low priority  */
    SetNvicPriority(9,  0U);   /* Emergency     — HIGHEST       */
    SetNvicPriority(10, 3U);   /* ClearEmerg    — low priority  */

    /* Enable all 5 EXTI lines (unmask + NVIC enable) */
    Exti_Enable(EXTI_LINE_0);
    Exti_Enable(EXTI_LINE_1);
    Exti_Enable(EXTI_LINE_2);
    Exti_Enable(EXTI_LINE_3);
    Exti_Enable(EXTI_LINE_4);

    /* --- 5. Initialise FSM ------------------------------- */
    /*
     * FSM_Init sets elevator to IDLE at Floor 1,
     * initialises PWM on TIM4_CH1 (PB6),
     * and stores s_elevPtr for the door-timer trampoline.
     * It does NOT register EXTI callbacks — we did that above.
     */
    FSM_Init(&elev);

    /* --- 6. Main loop ------------------------------------ */
    /*
     * FSM_Update() — checks all flags set by ISRs and drives
     *                state transitions. Never blocks.
     *
     * FSM_TickMs() — in a real system this belongs in a 1ms
     *                SysTick ISR. Here it is called every loop
     *                iteration, which is fast enough to keep
     *                the 500ms telemetry roughly accurate for
     *                testing purposes. Move to SysTick for
     *                production code.
     */
    while (1)
    {
        FSM_Update(&elev);
        FSM_TickMs();
    }
}