set(INCLUDE_LIST ${INCLUDE_LIST}
        ${ARM_DIR}/arm-none-eabi/include
        ${PROJECT_PATH}/STM32-base/startup
        ${PROJECT_PATH}/STM32-base-STM32Cube/CMSIS/ARM/inc
        ${PROJECT_PATH}/STM32-base-STM32Cube/CMSIS/${SERIES_FOLDER}/inc
        ${PROJECT_PATH}/Exti
        ${PROJECT_PATH}/Gpio
        ${PROJECT_PATH}/include
        ${PROJECT_PATH}/Lib
        ${PROJECT_PATH}/Nvic
        ${PROJECT_PATH}/Pwm
        ${PROJECT_PATH}/RCC
        ${PROJECT_PATH}/RingBuffer
        ${PROJECT_PATH}/Spi
        ${PROJECT_PATH}/Timer
        ${PROJECT_PATH}/Telemetry
        ${PROJECT_PATH}/Usart
        ${PROJECT_PATH}/FSM
        ${PROJECT_PATH}/Project
        ${PROJECT_PATH}/Dispatch
        ${PROJECT_PATH}/IPC

)

if (USE_HAL)
    set(INCLUDE_LIST ${INCLUDE_LIST} ${PROJECT_PATH}/STM32-base-STM32Cube/HAL/${SERIES_FOLDER}/inc)
endif ()
