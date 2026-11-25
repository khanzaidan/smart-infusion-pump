# Smart Infusion Pump firmware map

The existing project contains firmware for three processors. These paths were verified at commit [9df6d1b](https://github.com/khanzaidan/smart-infusion-pump/tree/9df6d1b83114aafc71ec1fdaec96734b6f02972c). The map describes the checked-in source. Board operation and medical performance were outside this review.

| Processor / artifact | Source | What to inspect |
| --- | --- | --- |
| STM32F407 main application | [main.c](../Core/Src/main.c) | Application entry point and peripheral initialization |
| STM32 scheduling | [freertos.c](../Core/Src/freertos.c) | Motor control, safety monitoring, temperature, display, communication and pressure tasks |
| Shared types and RTOS settings | [app_data.h](../Core/Inc/app_data.h) and [FreeRTOSConfig.h](../Core/Inc/FreeRTOSConfig.h) | Application state and kernel configuration |
| Board configuration | [SMART_INFUSION.ioc](../SMART_INFUSION.ioc) | CubeMX project and peripheral configuration |
| STM32 startup and flash memory layout | [Startup assembly](../Core/Startup/startup_stm32f407vgtx.s) and [flash linker script](../STM32F407VGTX_FLASH.ld) | Startup vector and linker memory regions |
| ATmega companion | [Arduino sketch](../SMART_INFUSION_ATmega/SMART_INFUSION_ATmega.ino) | Companion safety-monitor program |
| ESP32 companion | [Arduino sketch](../SMART_INFUSION_ESP32/SMART_INFUSION_ESP32.ino) | Monitoring and communication program |

## Development inputs

The repository includes STM32 HAL/CMSIS drivers, FreeRTOS middleware, and the CubeMX configuration. Each companion sketch is a separate Arduino build target. Preserve the original board revision, configuration and toolchain when reproducing a build. This documentation addition changes no firmware, calibration or control parameters.
