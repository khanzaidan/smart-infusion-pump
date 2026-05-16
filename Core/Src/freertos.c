/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * File Name          : freertos.c
  * Description        : Code for freertos applications
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2026 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  */
/* USER CODE END Header */

/* Includes ------------------------------------------------------------------*/
#include "FreeRTOS.h"
#include "task.h"
#include "main.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include "app_data.h"
#include "main.h"
#include "string.h"
#include "stdio.h"
#include "stdlib.h"
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */

/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/
/* USER CODE BEGIN Variables */

osMessageQueueId_t motorCommandQueueHandle;
osMessageQueueId_t temperatureQueueHandle;
osMessageQueueId_t flowRateQueueHandle;
osMessageQueueId_t volumeQueueHandle;
osMessageQueueId_t alarmQueueHandle;
osMessageQueueId_t batteryQueueHandle;

/* USER CODE END Variables */

/* Private function prototypes -----------------------------------------------*/
/* USER CODE BEGIN FunctionPrototypes */

/* USER CODE END FunctionPrototypes */

/* Private application code --------------------------------------------------*/
/* USER CODE BEGIN Application */
/* USER CODE BEGIN Application */

/* ============================================
 * AT24C256 EEPROM DRIVER
 * I2C1 — Address 0x50 — 32KB storage
 * ============================================ */
#define EEPROM_ADDR         0xA0
#define EEPROM_WRITE_TIME   5
#define EEPROM_VALID_KEY    0xAB

#define EEPROM_ADDR_FLOW    0x0000
#define EEPROM_ADDR_VOL     0x0004
#define EEPROM_ADDR_SYR     0x0008
#define EEPROM_ADDR_VALID   0x000C

static void EEPROM_WriteByte(
    uint16_t addr, uint8_t data)
{
    uint8_t buf[3];
    buf[0] = (uint8_t)(addr >> 8);
    buf[1] = (uint8_t)(addr & 0xFF);
    buf[2] = data;
    osMutexAcquire(i2c1MutexHandle,
        osWaitForever);
    HAL_I2C_Master_Transmit(&hi2c1,
        EEPROM_ADDR, buf, 3, 20);
    osMutexRelease(i2c1MutexHandle);
    osDelay(EEPROM_WRITE_TIME);
}

static uint8_t EEPROM_ReadByte(
    uint16_t addr)
{
    uint8_t addrBuf[2];
    uint8_t data = 0;
    addrBuf[0] = (uint8_t)(addr >> 8);
    addrBuf[1] = (uint8_t)(addr & 0xFF);
    osMutexAcquire(i2c1MutexHandle,
        osWaitForever);
    HAL_I2C_Master_Transmit(&hi2c1,
        EEPROM_ADDR, addrBuf, 2, 20);
    HAL_I2C_Master_Receive(&hi2c1,
        EEPROM_ADDR, &data, 1, 20);
    osMutexRelease(i2c1MutexHandle);
    return data;
}

static void EEPROM_WriteFloat(
    uint16_t addr, float value)
{
    uint8_t *b = (uint8_t*)&value;
    for(int i = 0; i < 4; i++)
        EEPROM_WriteByte(addr + i, b[i]);
}

static float EEPROM_ReadFloat(
    uint16_t addr)
{
    float value;
    uint8_t *b = (uint8_t*)&value;
    for(int i = 0; i < 4; i++)
        b[i] = EEPROM_ReadByte(addr + i);
    return value;
}

static void EEPROM_SaveSettings(
    float flowRate, float vol,
    uint8_t syringe)
{
    EEPROM_WriteFloat(
        EEPROM_ADDR_FLOW, flowRate);
    EEPROM_WriteFloat(
        EEPROM_ADDR_VOL, vol);
    EEPROM_WriteByte(
        EEPROM_ADDR_SYR, syringe);
    EEPROM_WriteByte(
        EEPROM_ADDR_VALID, EEPROM_VALID_KEY);
}

static uint8_t EEPROM_LoadSettings(
    float *flowRate, float *vol,
    uint8_t *syringe)
{
    uint8_t valid = EEPROM_ReadByte(
        EEPROM_ADDR_VALID);
    if(valid != EEPROM_VALID_KEY)
    {
        *flowRate = 50.0f;
        *vol = 100.0f;
        *syringe = 2;
        return 0;
    }
    *flowRate = EEPROM_ReadFloat(
        EEPROM_ADDR_FLOW);
    *vol = EEPROM_ReadFloat(
        EEPROM_ADDR_VOL);
    *syringe = EEPROM_ReadByte(
        EEPROM_ADDR_SYR);
    return 1;
}
/* ============================================
 * SYRINGE SIZE HELPER FUNCTIONS
 * ============================================ */
static uint32_t getStepsPerML(void)
{
    /* Default 20mL syringe */
    /* TODO: Read SW2 DIP switch when */
    /* GPIO pins are assigned for SW2 */
    return STEPS_PER_ML_20ML;
}

static float getMLPerStep(void)
{
    return 1.0f / (float)getStepsPerML();
}

/* ============================================
 * DS18B20 ONE WIRE DRIVER FUNCTIONS
 * ============================================ */

/* Send reset pulse and check presence */
static uint8_t DS18B20_Reset(void)
{
    uint8_t presence = 0;

    /* Pull line LOW for 480us */
    GPIO_InitTypeDef GPIO_InitStruct = {0};
    GPIO_InitStruct.Pin = TEMP_OW_Pin;
    GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_OD;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
    HAL_GPIO_Init(TEMP_OW_GPIO_Port, &GPIO_InitStruct);

    HAL_GPIO_WritePin(TEMP_OW_GPIO_Port,
        TEMP_OW_Pin, GPIO_PIN_RESET);
    HAL_Delay(1); /* ~480us minimum */

    /* Release line */
    HAL_GPIO_WritePin(TEMP_OW_GPIO_Port,
        TEMP_OW_Pin, GPIO_PIN_SET);
    HAL_Delay(1);

    /* Check for presence pulse */
    if(HAL_GPIO_ReadPin(TEMP_OW_GPIO_Port,
        TEMP_OW_Pin) == GPIO_PIN_RESET)
    {
        presence = 1;
    }

    HAL_Delay(1);
    return presence;
}

/* Write one bit to one wire bus */
static void DS18B20_WriteBit(uint8_t bit)
{
    HAL_GPIO_WritePin(TEMP_OW_GPIO_Port,
        TEMP_OW_Pin, GPIO_PIN_RESET);

    if(bit)
    {
        /* Write 1 - release within 15us */
        for(volatile int i = 0; i < 100; i++);
        HAL_GPIO_WritePin(TEMP_OW_GPIO_Port,
            TEMP_OW_Pin, GPIO_PIN_SET);
        for(volatile int i = 0; i < 500; i++);
    }
    else
    {
        /* Write 0 - hold low for 60us */
        for(volatile int i = 0; i < 600; i++);
        HAL_GPIO_WritePin(TEMP_OW_GPIO_Port,
            TEMP_OW_Pin, GPIO_PIN_SET);
        for(volatile int i = 0; i < 100; i++);
    }
}

/* Write one byte to one wire bus */
static void DS18B20_WriteByte(uint8_t byte)
{
    for(int i = 0; i < 8; i++)
    {
        DS18B20_WriteBit(byte & (1 << i));
    }
}

/* Read one bit from one wire bus */
static uint8_t DS18B20_ReadBit(void)
{
    uint8_t bit = 0;

    HAL_GPIO_WritePin(TEMP_OW_GPIO_Port,
        TEMP_OW_Pin, GPIO_PIN_RESET);
    for(volatile int i = 0; i < 50; i++);

    HAL_GPIO_WritePin(TEMP_OW_GPIO_Port,
        TEMP_OW_Pin, GPIO_PIN_SET);
    for(volatile int i = 0; i < 50; i++);

    bit = HAL_GPIO_ReadPin(TEMP_OW_GPIO_Port,
        TEMP_OW_Pin);

    for(volatile int i = 0; i < 500; i++);
    return bit;
}

/* Read one byte from one wire bus */
static uint8_t DS18B20_ReadByte(void)
{
    uint8_t byte = 0;
    for(int i = 0; i < 8; i++)
    {
        if(DS18B20_ReadBit())
            byte |= (1 << i);
    }
    return byte;
}

/* Start temperature conversion */
static void DS18B20_StartConversion(void)
{
    DS18B20_Reset();
    DS18B20_WriteByte(0xCC); /* Skip ROM */
    DS18B20_WriteByte(0x44); /* Convert T */
}

/* Read temperature in Celsius */
static float DS18B20_ReadTemperature(void)
{
    uint8_t byte1, byte2;
    int16_t raw;
    float temp;

    DS18B20_Reset();
    DS18B20_WriteByte(0xCC); /* Skip ROM */
    DS18B20_WriteByte(0xBE); /* Read scratchpad */

    byte1 = DS18B20_ReadByte(); /* LSB */
    byte2 = DS18B20_ReadByte(); /* MSB */

    raw = (int16_t)((byte2 << 8) | byte1);
    temp = (float)raw / 16.0f;

    return temp;
}


void vSafetyMonitorTask(void *argument)
{
    AlarmState_t currentAlarm = ALARM_NONE;
    uint32_t wdiLastKick = 0;
    uint32_t buzzerToggleTick = 0;
    uint8_t atmegaMsg[32];

    /* Enable motor relay on startup */
    HAL_GPIO_WritePin(RELAY_CTRL_GPIO_Port,
        RELAY_CTRL_Pin, GPIO_PIN_SET);

    osDelay(500);

    for(;;)
    {
        currentAlarm = ALARM_NONE;

        /* ---- Read ATmega alarm messages ---- */
        memset(atmegaMsg, 0, sizeof(atmegaMsg));
        if(HAL_UART_Receive(&huart3,
            atmegaMsg,
            sizeof(atmegaMsg) - 1,
            5) == HAL_OK)
        {
            if(strstr((char*)atmegaMsg,
                "BUBBLE"))
            {
                currentAlarm = ALARM_BUBBLE;
            }
            else if(strstr((char*)atmegaMsg,
                "LIM1_HIT"))
            {
                currentAlarm = ALARM_LIMIT_HIT;
            }
            else if(strstr((char*)atmegaMsg,
                "LIM2_HIT"))
            {
                currentAlarm = ALARM_LIMIT_HIT;
            }
            else if(strstr((char*)atmegaMsg,
                "WDT_TIMEOUT"))
            {
                currentAlarm = ALARM_WATCHDOG_FAIL;
            }

            /* If new alarm detected */
            if(currentAlarm != ALARM_NONE)
            {
                /* Stop motor immediately */
                osSemaphoreRelease(
                    emergencyStopSemHandle);

                /* Cut relay power */
                HAL_GPIO_WritePin(
                    RELAY_CTRL_GPIO_Port,
                    RELAY_CTRL_Pin,
                    GPIO_PIN_RESET);
            }
        }

        /* ---- Kick watchdog every 500ms ---- */
        if((osKernelGetTickCount() - wdiLastKick)
            >= WDI_KICK_MS)
        {
            HAL_GPIO_TogglePin(
                WDI_SIG_GPIO_Port,
                WDI_SIG_Pin);
            wdiLastKick = osKernelGetTickCount();

            /* Send heartbeat to ATmega */
            osMutexAcquire(uart3MutexHandle,
                osWaitForever);
            HAL_UART_Transmit(&huart3,
                (uint8_t*)"H\r\n", 3, 10);
            osMutexRelease(uart3MutexHandle);
        }

        /* ---- Send alarm to queue ---- */
        osMessageQueuePut(alarmQueueHandle,
            &currentAlarm, 0, 0);

        /* ---- Buzzer control ---- */
        if(currentAlarm == ALARM_NONE)
        {
            HAL_GPIO_WritePin(
                BUZZ_SIG_GPIO_Port,
                BUZZ_SIG_Pin,
                GPIO_PIN_RESET);
        }
        else
        {
            if((osKernelGetTickCount() -
                buzzerToggleTick) >= 300)
            {
                HAL_GPIO_TogglePin(
                    BUZZ_SIG_GPIO_Port,
                    BUZZ_SIG_Pin);
                buzzerToggleTick =
                    osKernelGetTickCount();
            }
        }

        osDelay(100);
    }
}

void vMotorControlTask(void *argument)
{
    MotorCommand_t cmd;
    uint32_t stepCount = 0;
    uint32_t targetSteps = 0;
    uint32_t stepDelay = 0;
    uint8_t motorRunning = 0;
    float volumeInfused = 0.0f;
    float flowRate = 0.0f;

    /* Disable motor driver on startup */
    HAL_GPIO_WritePin(STEP_EN_GPIO_Port,
        STEP_EN_Pin, GPIO_PIN_SET);

    /* Set default direction forward */
    HAL_GPIO_WritePin(STEP_DIR_GPIO_Port,
        STEP_DIR_Pin, GPIO_PIN_RESET);

    for(;;)
    {
        /* Wait for motor command - block until received */
        if(osMessageQueueGet(motorCommandQueueHandle,
            &cmd, NULL, 100) == osOK)
        {
            if(cmd.command == MOTOR_CMD_START)
            {
                /* Set direction */
                HAL_GPIO_WritePin(STEP_DIR_GPIO_Port,
                    STEP_DIR_Pin, cmd.direction);

                /* Enable driver */
                HAL_GPIO_WritePin(STEP_EN_GPIO_Port,
                    STEP_EN_Pin, GPIO_PIN_RESET);

                /* Calculate step delay from flow rate */
                /* stepDelay in ms between each step */
                if(cmd.stepFrequencyHz > 0)
                {
                    stepDelay = 1000 /
                        cmd.stepFrequencyHz;
                    if(stepDelay < 1) stepDelay = 1;
                }
                else
                {
                    stepDelay = 10;
                }

                targetSteps = cmd.targetSteps;

                /* Recalculate step delay from flow rate */
                if(cmd.flowRateMLperHr > 0)
                {
                    float stepsPerSec =
                        (cmd.flowRateMLperHr / 3600.0f) *
                        (float)getStepsPerML();
                    if(stepsPerSec > 0)
                    {
                        stepDelay = (uint32_t)(
                            1000.0f / stepsPerSec);
                        if(stepDelay < 1)
                            stepDelay = 1;
                        if(stepDelay > 10000)
                            stepDelay = 10000;
                    }
                }
                stepCount = 0;
                motorRunning = 1;
                flowRate = cmd.flowRateMLperHr;
            }
            else if(cmd.command == MOTOR_CMD_STOP ||
                    cmd.command == MOTOR_CMD_PAUSE)
            {
                motorRunning = 0;
                /* Disable driver */
                HAL_GPIO_WritePin(STEP_EN_GPIO_Port,
                    STEP_EN_Pin, GPIO_PIN_SET);
            }
        }

        /* Check emergency stop semaphore */
        if(osSemaphoreAcquire(emergencyStopSemHandle,
            0) == osOK)
        {
            motorRunning = 0;
            HAL_GPIO_WritePin(STEP_EN_GPIO_Port,
                STEP_EN_Pin, GPIO_PIN_SET);
            HAL_GPIO_WritePin(RELAY_CTRL_GPIO_Port,
                RELAY_CTRL_Pin, GPIO_PIN_RESET);
        }

        /* Generate step pulse if running */
        if(motorRunning && stepCount < targetSteps)
        {
            /* Step pulse HIGH */
            HAL_GPIO_WritePin(STEP_PUL_GPIO_Port,
                STEP_PUL_Pin, GPIO_PIN_SET);

            /* Minimum pulse width 2 microseconds */
            /* At 168MHz each loop ~6ns so 334 loops */
            for(volatile int i = 0; i < 334; i++);

            /* Step pulse LOW */
            HAL_GPIO_WritePin(STEP_PUL_GPIO_Port,
                STEP_PUL_Pin, GPIO_PIN_RESET);

            stepCount++;

            /* Update volume infused */
            /* 1 step = 0.000785 mL for 20mL syringe */
            volumeInfused += getMLPerStep();

            /* Send updated volume to queue */
            osMessageQueuePut(volumeQueueHandle,
                &volumeInfused, 0, 0);

            /* Send flow rate to queue */
            osMessageQueuePut(flowRateQueueHandle,
                &flowRate, 0, 0);

            /* Check if target reached */
            if(stepCount >= targetSteps)
            {
                motorRunning = 0;
                HAL_GPIO_WritePin(STEP_EN_GPIO_Port,
                    STEP_EN_Pin, GPIO_PIN_SET);
            }

            /* Step delay controls speed */
            osDelay(stepDelay);
        }
        else if(!motorRunning)
        {
            osDelay(10);
        }
    }
}

void vTemperatureTask(void *argument)
{
    float currentTemp = 25.0f;
    float targetTemp = TEMP_TARGET_C;
    float pidOutput = 0.0f;
    float integral = 0.0f;
    float prevError = 0.0f;
    float error = 0.0f;
    uint32_t pwmValue = 0;

    /* PID gains - tune after hardware testing */
    float Kp = 15.0f;
    float Ki = 0.05f;
    float Kd = 2.0f;

    /* Start Peltier PWM - TIM2 Channel 1 */
    HAL_TIM_PWM_Start(&htim2, TIM_CHANNEL_1);

    /* Set initial duty to 0 */
    __HAL_TIM_SET_COMPARE(&htim2,
        TIM_CHANNEL_1, 0);

    osDelay(1000); /* Wait for system to stabilize */

    for(;;)
    {
        /* Start DS18B20 conversion */
        DS18B20_StartConversion();

        /* Wait 750ms for conversion to complete */
        osDelay(750);

        /* Read temperature */
        currentTemp = DS18B20_ReadTemperature();

        /* Validate reading - ignore if out of range */
        if(currentTemp > -10.0f &&
           currentTemp < 85.0f)
        {
            /* PID calculation */
            error = targetTemp - currentTemp;

            /* Integral with anti-windup */
            integral += error * 0.75f;
            if(integral > 500.0f)
                integral = 500.0f;
            if(integral < -500.0f)
                integral = -500.0f;

            /* Derivative */
            float derivative = error - prevError;
            prevError = error;

            /* PID output */
            pidOutput = (Kp * error) +
                        (Ki * integral) +
                        (Kd * derivative);

            /* Clamp 0 to 100 percent */
            if(pidOutput > 100.0f)
                pidOutput = 100.0f;
            if(pidOutput < 0.0f)
                pidOutput = 0.0f;

            /* Convert to PWM compare value */
            /* TIM2 period = 999 */
            pwmValue = (uint32_t)(pidOutput *
                999.0f / 100.0f);

            /* Set Peltier PWM duty cycle */
            __HAL_TIM_SET_COMPARE(&htim2,
                TIM_CHANNEL_1, pwmValue);

            /* Send temperature to queue */
            osMessageQueuePut(temperatureQueueHandle,
                &currentTemp, 0, 0);
        }

        /* Check temperature safety limit */
        if(currentTemp > 42.0f)
        {
            /* Temperature too high - alarm */
            AlarmState_t alarm = ALARM_TEMP_HIGH;
            osMessageQueuePut(alarmQueueHandle,
                &alarm, 0, 0);

            /* Give emergency stop semaphore */
            osSemaphoreRelease(
                emergencyStopSemHandle);

            /* Turn off Peltier */
            __HAL_TIM_SET_COMPARE(&htim2,
                TIM_CHANNEL_1, 0);
        }

        /* Fan control based on temperature */
        if(currentTemp > 35.0f)
        {
            HAL_GPIO_WritePin(FAN_CTRL_GPIO_Port,
                FAN_CTRL_Pin, GPIO_PIN_SET);
        }
        else if(currentTemp < 32.0f)
        {
            HAL_GPIO_WritePin(FAN_CTRL_GPIO_Port,
                FAN_CTRL_Pin, GPIO_PIN_RESET);
        }

        osDelay(250);
    }
}

/* ============================================
 * SSD1306 OLED DRIVER FUNCTIONS
 * ============================================ */
#define SSD1306_ADDR        0x78
#define SSD1306_CMD         0x00
#define SSD1306_DATA        0x40
#define SSD1306_WIDTH       128
#define SSD1306_HEIGHT      64
#define SSD1306_PAGES       8

static uint8_t oledBuffer[SSD1306_WIDTH *
    SSD1306_PAGES];

/* Send single command to OLED */
static void SSD1306_SendCmd(uint8_t cmd)
{
    uint8_t buf[2];
    buf[0] = SSD1306_CMD;
    buf[1] = cmd;
    osMutexAcquire(i2c1MutexHandle,
        osWaitForever);
    HAL_I2C_Master_Transmit(&hi2c1,
        SSD1306_ADDR, buf, 2, 10);
    osMutexRelease(i2c1MutexHandle);
}

/* Initialize OLED display */
static void SSD1306_Init(void)
{
    osDelay(100);
    SSD1306_SendCmd(0xAE); /* Display OFF */
    SSD1306_SendCmd(0x20); /* Memory mode */
    SSD1306_SendCmd(0x00); /* Horizontal */
    SSD1306_SendCmd(0xB0); /* Page start */
    SSD1306_SendCmd(0xC8); /* COM scan dir */
    SSD1306_SendCmd(0x00); /* Low col addr */
    SSD1306_SendCmd(0x10); /* High col addr */
    SSD1306_SendCmd(0x40); /* Start line */
    SSD1306_SendCmd(0x81); /* Contrast */
    SSD1306_SendCmd(0xFF); /* Max contrast */
    SSD1306_SendCmd(0xA1); /* Seg remap */
    SSD1306_SendCmd(0xA6); /* Normal display */
    SSD1306_SendCmd(0xA8); /* Multiplex */
    SSD1306_SendCmd(0x3F); /* 64 rows */
    SSD1306_SendCmd(0xA4); /* Output RAM */
    SSD1306_SendCmd(0xD3); /* Display offset */
    SSD1306_SendCmd(0x00); /* No offset */
    SSD1306_SendCmd(0xD5); /* Clock divide */
    SSD1306_SendCmd(0xF0); /* Clock ratio */
    SSD1306_SendCmd(0xD9); /* Pre-charge */
    SSD1306_SendCmd(0x22); /* Pre-charge val */
    SSD1306_SendCmd(0xDA); /* COM pins */
    SSD1306_SendCmd(0x12); /* Alt COM */
    SSD1306_SendCmd(0xDB); /* VCOMH desel */
    SSD1306_SendCmd(0x20); /* 0.77xVcc */
    SSD1306_SendCmd(0x8D); /* Charge pump */
    SSD1306_SendCmd(0x14); /* Enable pump */
    SSD1306_SendCmd(0xAF); /* Display ON */
    osDelay(100);
}

/* Clear display buffer */
static void SSD1306_Clear(void)
{
    memset(oledBuffer, 0x00,
        sizeof(oledBuffer));
}

/* Update display from buffer */
static void SSD1306_Update(void)
{
    SSD1306_SendCmd(0x21); /* Col addr */
    SSD1306_SendCmd(0);
    SSD1306_SendCmd(127);
    SSD1306_SendCmd(0x22); /* Page addr */
    SSD1306_SendCmd(0);
    SSD1306_SendCmd(7);

    uint8_t txBuf[SSD1306_WIDTH + 1];
    txBuf[0] = SSD1306_DATA;

    for(int page = 0; page < SSD1306_PAGES;
        page++)
    {
        memcpy(&txBuf[1],
            &oledBuffer[page * SSD1306_WIDTH],
            SSD1306_WIDTH);
        osMutexAcquire(i2c1MutexHandle,
            osWaitForever);
        HAL_I2C_Master_Transmit(&hi2c1,
            SSD1306_ADDR, txBuf,
            SSD1306_WIDTH + 1, 20);
        osMutexRelease(i2c1MutexHandle);
    }
}

/* 5x7 font - basic ASCII characters */
static const uint8_t font5x7[][5] = {
    {0x00,0x00,0x00,0x00,0x00}, /* space */
    {0x00,0x00,0x5F,0x00,0x00}, /* ! */
    {0x00,0x07,0x00,0x07,0x00}, /* " */
    {0x14,0x7F,0x14,0x7F,0x14}, /* # */
    {0x24,0x2A,0x7F,0x2A,0x12}, /* $ */
    {0x23,0x13,0x08,0x64,0x62}, /* % */
    {0x36,0x49,0x55,0x22,0x50}, /* & */
    {0x00,0x05,0x03,0x00,0x00}, /* ' */
    {0x00,0x1C,0x22,0x41,0x00}, /* ( */
    {0x00,0x41,0x22,0x1C,0x00}, /* ) */
    {0x08,0x2A,0x1C,0x2A,0x08}, /* * */
    {0x08,0x08,0x3E,0x08,0x08}, /* + */
    {0x00,0x50,0x30,0x00,0x00}, /* , */
    {0x08,0x08,0x08,0x08,0x08}, /* - */
    {0x00,0x60,0x60,0x00,0x00}, /* . */
    {0x20,0x10,0x08,0x04,0x02}, /* / */
    {0x3E,0x51,0x49,0x45,0x3E}, /* 0 */
    {0x00,0x42,0x7F,0x40,0x00}, /* 1 */
    {0x42,0x61,0x51,0x49,0x46}, /* 2 */
    {0x21,0x41,0x45,0x4B,0x31}, /* 3 */
    {0x18,0x14,0x12,0x7F,0x10}, /* 4 */
    {0x27,0x45,0x45,0x45,0x39}, /* 5 */
    {0x3C,0x4A,0x49,0x49,0x30}, /* 6 */
    {0x01,0x71,0x09,0x05,0x03}, /* 7 */
    {0x36,0x49,0x49,0x49,0x36}, /* 8 */
    {0x06,0x49,0x49,0x29,0x1E}, /* 9 */
    {0x00,0x36,0x36,0x00,0x00}, /* : */
    {0x00,0x56,0x36,0x00,0x00}, /* ; */
    {0x00,0x08,0x14,0x22,0x41}, /* < */
    {0x14,0x14,0x14,0x14,0x14}, /* = */
    {0x41,0x22,0x14,0x08,0x00}, /* > */
    {0x02,0x01,0x51,0x09,0x06}, /* ? */
    {0x32,0x49,0x79,0x41,0x3E}, /* @ */
    {0x7E,0x11,0x11,0x11,0x7E}, /* A */
    {0x7F,0x49,0x49,0x49,0x36}, /* B */
    {0x3E,0x41,0x41,0x41,0x22}, /* C */
    {0x7F,0x41,0x41,0x22,0x1C}, /* D */
    {0x7F,0x49,0x49,0x49,0x41}, /* E */
    {0x7F,0x09,0x09,0x09,0x01}, /* F */
    {0x3E,0x41,0x41,0x49,0x7A}, /* G */
    {0x7F,0x08,0x08,0x08,0x7F}, /* H */
    {0x00,0x41,0x7F,0x41,0x00}, /* I */
    {0x20,0x40,0x41,0x3F,0x01}, /* J */
    {0x7F,0x08,0x14,0x22,0x41}, /* K */
    {0x7F,0x40,0x40,0x40,0x40}, /* L */
    {0x7F,0x02,0x04,0x02,0x7F}, /* M */
    {0x7F,0x04,0x08,0x10,0x7F}, /* N */
    {0x3E,0x41,0x41,0x41,0x3E}, /* O */
    {0x7F,0x09,0x09,0x09,0x06}, /* P */
    {0x3E,0x41,0x51,0x21,0x5E}, /* Q */
    {0x7F,0x09,0x19,0x29,0x46}, /* R */
    {0x46,0x49,0x49,0x49,0x31}, /* S */
    {0x01,0x01,0x7F,0x01,0x01}, /* T */
    {0x3F,0x40,0x40,0x40,0x3F}, /* U */
    {0x1F,0x20,0x40,0x20,0x1F}, /* V */
    {0x3F,0x40,0x38,0x40,0x3F}, /* W */
    {0x63,0x14,0x08,0x14,0x63}, /* X */
    {0x03,0x04,0x78,0x04,0x03}, /* Y */
    {0x61,0x51,0x49,0x45,0x43}, /* Z */
    {0x00,0x00,0x7F,0x41,0x41}, /* [ */
    {0x02,0x04,0x08,0x10,0x20}, /* \ */
    {0x41,0x41,0x7F,0x00,0x00}, /* ] */
    {0x04,0x02,0x01,0x02,0x04}, /* ^ */
    {0x40,0x40,0x40,0x40,0x40}, /* _ */
    {0x00,0x01,0x02,0x04,0x00}, /* ` */
    {0x20,0x54,0x54,0x54,0x78}, /* a */
    {0x7F,0x48,0x44,0x44,0x38}, /* b */
    {0x38,0x44,0x44,0x44,0x20}, /* c */
    {0x38,0x44,0x44,0x48,0x7F}, /* d */
    {0x38,0x54,0x54,0x54,0x18}, /* e */
    {0x08,0x7E,0x09,0x01,0x02}, /* f */
    {0x08,0x54,0x54,0x54,0x3C}, /* g */
    {0x7F,0x08,0x04,0x04,0x78}, /* h */
    {0x00,0x44,0x7D,0x40,0x00}, /* i */
    {0x20,0x40,0x44,0x3D,0x00}, /* j */
    {0x00,0x7F,0x10,0x28,0x44}, /* k */
    {0x00,0x41,0x7F,0x40,0x00}, /* l */
    {0x7C,0x04,0x18,0x04,0x78}, /* m */
    {0x7C,0x08,0x04,0x04,0x78}, /* n */
    {0x38,0x44,0x44,0x44,0x38}, /* o */
    {0x7C,0x14,0x14,0x14,0x08}, /* p */
    {0x08,0x14,0x14,0x18,0x7C}, /* q */
    {0x7C,0x08,0x04,0x04,0x08}, /* r */
    {0x48,0x54,0x54,0x54,0x20}, /* s */
    {0x04,0x3F,0x44,0x40,0x20}, /* t */
    {0x3C,0x40,0x40,0x40,0x7C}, /* u */
    {0x1C,0x20,0x40,0x20,0x1C}, /* v */
    {0x3C,0x40,0x30,0x40,0x3C}, /* w */
    {0x44,0x28,0x10,0x28,0x44}, /* x */
    {0x0C,0x50,0x50,0x50,0x3C}, /* y */
    {0x44,0x64,0x54,0x4C,0x44}, /* z */
};

/* Draw character at position */
static void SSD1306_DrawChar(uint8_t x,
    uint8_t page, char c)
{
    if(c < 32 || c > 122) c = 32;
    uint8_t idx = c - 32;

    for(int col = 0; col < 5; col++)
    {
        if((x + col) < SSD1306_WIDTH)
        {
            oledBuffer[page * SSD1306_WIDTH +
                x + col] = font5x7[idx][col];
        }
    }
    /* Space between characters */
    if((x + 5) < SSD1306_WIDTH)
    {
        oledBuffer[page * SSD1306_WIDTH +
            x + 5] = 0x00;
    }
}

/* Draw string at position */
static void SSD1306_DrawString(uint8_t x,
    uint8_t page, const char *str)
{
    while(*str)
    {
        SSD1306_DrawChar(x, page, *str);
        x += 6;
        str++;
        if(x >= SSD1306_WIDTH) break;
    }
}

void vDisplayTask(void *argument)
{
    float temperature = 25.0f;
    float flowRate = 0.0f;
    float volume = 0.0f;
    AlarmState_t alarm = ALARM_NONE;
    char line[22];

    /* Initialize OLED */
    SSD1306_Init();
    SSD1306_Clear();

    /* Show startup screen */
    SSD1306_DrawString(10, 0,
        "SMART INFUSION");
    SSD1306_DrawString(20, 2,
        "WALNUT MEDICAL");
    SSD1306_DrawString(25, 4,
        "Initializing");
    SSD1306_Update();
    osDelay(2000);

    for(;;)
    {
        /* Get latest values from queues */
        osMessageQueueGet(temperatureQueueHandle,
            &temperature, NULL, 0);
        osMessageQueueGet(flowRateQueueHandle,
            &flowRate, NULL, 0);
        osMessageQueueGet(volumeQueueHandle,
            &volume, NULL, 0);
        osMessageQueueGet(alarmQueueHandle,
            &alarm, NULL, 0);

        /* Clear buffer */
        SSD1306_Clear();

        /* Line 1 - Flow rate */
        snprintf(line, sizeof(line),
            "Flow:%5.1f mL/hr", flowRate);
        SSD1306_DrawString(0, 0, line);

        /* Line 2 - Volume infused */
        snprintf(line, sizeof(line),
            "Vol: %6.2f mL", volume);
        SSD1306_DrawString(0, 2, line);

        /* Line 3 - Temperature */
        snprintf(line, sizeof(line),
            "Temp:%5.1f C", temperature);
        SSD1306_DrawString(0, 4, line);

        /* Line 4 - Status or alarm */
        if(alarm == ALARM_NONE)
        {
            SSD1306_DrawString(0, 6,
                "Status:  OK");
        }
        else if(alarm == ALARM_BUBBLE)
        {
            SSD1306_DrawString(0, 6,
                "!! BUBBLE !!");
        }
        else if(alarm == ALARM_OCCLUSION)
        {
            SSD1306_DrawString(0, 6,
                "!! OCCLUSN !!");
        }
        else if(alarm == ALARM_TEMP_HIGH)
        {
            SSD1306_DrawString(0, 6,
                "!! TEMP HI !!");
        }
        else if(alarm == ALARM_LIMIT_HIT)
        {
            SSD1306_DrawString(0, 6,
                "!! LIMIT !!");
        }
        else
        {
            SSD1306_DrawString(0, 6,
                "!! ALARM !!");
        }

        /* Update OLED */
        SSD1306_Update();

        osDelay(200);
    }
}

void vCommunicationTask(void *argument)
{
    float temperature = 25.0f;
    float flowRate = 0.0f;
    float volume = 0.0f;
    uint8_t battery = 100;
    AlarmState_t alarm = ALARM_NONE;
    char txBuffer[128];
    uint8_t rxBuffer[64];

    osDelay(2000); /* Wait for system ready */

    /* Load saved settings from EEPROM */
    float savedFlow = 50.0f;
    float savedVol = 100.0f;
    uint8_t savedSyringe = 2;
    EEPROM_LoadSettings(&savedFlow,
        &savedVol, &savedSyringe);

    for(;;)
    {
        /* Get latest values */
        osMessageQueueGet(temperatureQueueHandle,
            &temperature, NULL, 0);
        osMessageQueueGet(flowRateQueueHandle,
            &flowRate, NULL, 0);
        osMessageQueueGet(volumeQueueHandle,
            &volume, NULL, 0);

        /* Read MAX17043 battery SOC */
        uint8_t socCmd = 0x04;
        uint8_t socData[2] = {0};
        osMutexAcquire(i2c2MutexHandle,
            osWaitForever);
        HAL_I2C_Master_Transmit(&hi2c2,
            0x36 << 1, &socCmd, 1, 10);
        HAL_I2C_Master_Receive(&hi2c2,
            0x36 << 1, socData, 2, 10);
        osMutexRelease(i2c2MutexHandle);
        battery = socData[0];
        osMessageQueuePut(batteryQueueHandle,
            &battery, 0, 0);

        /* Battery low alarm check */
        if(battery < 20)
        {
            AlarmState_t battAlarm =
                ALARM_BATTERY_LOW;
            osMessageQueuePut(alarmQueueHandle,
                &battAlarm, 0, 0);

            /* Stop motor if critically low */
            if(battery < 10)
            {
                osSemaphoreRelease(
                    emergencyStopSemHandle);
                HAL_GPIO_WritePin(
                    RELAY_CTRL_GPIO_Port,
                    RELAY_CTRL_Pin,
                    GPIO_PIN_RESET);
            }
        }


        osMessageQueueGet(alarmQueueHandle,
            &alarm, NULL, 0);

        /* Build JSON packet */
        snprintf(txBuffer, sizeof(txBuffer),
            "{\"t\":%.1f,\"f\":%.1f,"
            "\"v\":%.2f,\"b\":%d,"
            "\"a\":%d}\r\n",
            temperature,
            flowRate,
            volume,
            battery,
            (int)alarm);

        /* Send to ESP32 via UART1 */
        HAL_UART_Transmit(&huart1,
            (uint8_t*)txBuffer,
            strlen(txBuffer),
            100);

        /* Check for incoming command */
        memset(rxBuffer, 0, sizeof(rxBuffer));
        if(HAL_UART_Receive(&huart1,
            rxBuffer, sizeof(rxBuffer)-1,
            10) == HAL_OK)
        {
            /* Parse command from ESP32 */
            /* Expected format: */
            /* {"cmd":"start","rate":50.0} */
            /* {"cmd":"stop"} */
            /* {"cmd":"pause"} */

        	if(strstr((char*)rxBuffer,
        	    "\"start\""))
        	{
        	    MotorCommand_t cmd;
        	    cmd.command = MOTOR_CMD_START;
        	    cmd.direction = 0;

        	    /* Parse flow rate from JSON */
        	    char *ratePtr = strstr(
        	        (char*)rxBuffer, "\"rate\":");
        	    if(ratePtr)
        	    {
        	        cmd.flowRateMLperHr =
        	            (float)atof(ratePtr + 7);
        	        if(cmd.flowRateMLperHr < 1.0f)
        	            cmd.flowRateMLperHr = 1.0f;
        	        if(cmd.flowRateMLperHr > 1000.0f)
        	            cmd.flowRateMLperHr = 1000.0f;
        	    }
        	    else
        	    {
        	        cmd.flowRateMLperHr = 50.0f;
        	    }

        	    /* Parse target volume from JSON */
        	    float targetVol = 100.0f;
        	    char *volPtr = strstr(
        	        (char*)rxBuffer, "\"vol\":");
        	    if(volPtr)
        	    {
        	        targetVol =
        	            (float)atof(volPtr + 6);
        	        if(targetVol <= 0.0f)
        	            targetVol = 100.0f;
        	    }

        	    /* Calculate target steps */
        	    cmd.targetSteps = (uint32_t)(
        	        targetVol *
        	        (float)getStepsPerML());

        	    /* Calculate step frequency */
        	    float stepsPerSec =
        	        (cmd.flowRateMLperHr / 3600.0f) *
        	        (float)getStepsPerML();
        	    if(stepsPerSec < 1.0f)
        	        stepsPerSec = 1.0f;
        	    cmd.stepFrequencyHz =
        	        (uint32_t)stepsPerSec;

        	    osMessageQueuePut(
        	            motorCommandQueueHandle,
        	            &cmd, 0, 100);

        	        /* Save settings to EEPROM */
        	        EEPROM_SaveSettings(
        	            cmd.flowRateMLperHr,
        	            targetVol, 2);
        	    }
            else if(strstr((char*)rxBuffer,
                "\"stop\""))
            {
                MotorCommand_t cmd;
                cmd.command = MOTOR_CMD_STOP;
                cmd.targetSteps = 0;
                cmd.stepFrequencyHz = 0;
                osMessageQueuePut(
                    motorCommandQueueHandle,
                    &cmd, 0, 100);
            }
            else if(strstr((char*)rxBuffer,
                "\"pause\""))
            {
                MotorCommand_t cmd;
                cmd.command = MOTOR_CMD_PAUSE;
                cmd.targetSteps = 0;
                cmd.stepFrequencyHz = 0;
                osMessageQueuePut(
                    motorCommandQueueHandle,
                    &cmd, 0, 100);
            }
        }

        osDelay(1000);
    }
}

/* ============================================
 * MPRLS PRESSURE SENSOR DRIVER
 * ============================================ */
#define MPRLS_ADDR          0x18
#define MPRLS_CMD_START     0xAA
#define MPRLS_OUTPUT_MAX    0xE66666
#define MPRLS_OUTPUT_MIN    0x19999A
#define MPRLS_PRES_MAX      25.0f
#define MPRLS_PRES_MIN      0.0f

static float MPRLS_ReadPressure(void)
{
    uint8_t cmd[3] = {MPRLS_CMD_START,
        0x00, 0x00};
    uint8_t data[7] = {0};
    float pressure = 0.0f;
    uint32_t rawData = 0;

    /* Send measurement command */
    osMutexAcquire(i2c2MutexHandle,
        osWaitForever);
    HAL_I2C_Master_Transmit(&hi2c2,
        MPRLS_ADDR << 1, cmd, 3, 10);
    osMutexRelease(i2c2MutexHandle);

    /* Wait for conversion */
    osDelay(10);

    /* Read 7 bytes of data */
    osMutexAcquire(i2c2MutexHandle,
        osWaitForever);
    HAL_I2C_Master_Receive(&hi2c2,
        MPRLS_ADDR << 1, data, 7, 20);
    osMutexRelease(i2c2MutexHandle);

    /* Check status byte */
    if((data[0] & 0x20) || (data[0] & 0x08))
    {
        return -1.0f; /* Error */
    }

    /* Extract 24-bit raw pressure */
    rawData = ((uint32_t)data[3] << 16) |
              ((uint32_t)data[4] << 8) |
               (uint32_t)data[5];

    /* Convert to hPa */
    pressure = (((float)rawData -
        (float)MPRLS_OUTPUT_MIN) *
        (MPRLS_PRES_MAX - MPRLS_PRES_MIN)) /
        ((float)MPRLS_OUTPUT_MAX -
        (float)MPRLS_OUTPUT_MIN) +
        MPRLS_PRES_MIN;

    /* Convert PSI to hPa */
    pressure = pressure * 68.9476f;

    return pressure;
}

void vPressureTask(void *argument)
{
    float pressure = 0.0f;
    float baseline = 0.0f;
    float emaAlpha = 0.02f;
    uint8_t baselineReady = 0;
    uint8_t sampleCount = 0;
    float pressureSum = 0.0f;
    AlarmState_t alarm = ALARM_NONE;

    osDelay(3000); /* Wait for system ready */

    /* Establish baseline over 10 samples */
    for(int i = 0; i < 10; i++)
    {
        float p = MPRLS_ReadPressure();
        if(p > 0.0f)
        {
            pressureSum += p;
            sampleCount++;
        }
        osDelay(200);
    }

    if(sampleCount > 0)
    {
        baseline = pressureSum / sampleCount;
        baselineReady = 1;
    }

    for(;;)
    {
        pressure = MPRLS_ReadPressure();

        if(pressure > 0.0f && baselineReady)
        {
            /* Update baseline with EMA filter */
            baseline = (emaAlpha * pressure) +
                ((1.0f - emaAlpha) * baseline);

            float delta = pressure - baseline;

            /* Check occlusion threshold */
            if(delta > OCCLUSION_DELTA_HPA)
            {
                alarm = ALARM_OCCLUSION;

                /* Send alarm */
                osMessageQueuePut(
                    alarmQueueHandle,
                    &alarm, 0, 0);

                /* Stop motor immediately */
                osSemaphoreRelease(
                    emergencyStopSemHandle);

                char msg[] = "OCC\r\n";
                osMutexAcquire(uart3MutexHandle,
                    osWaitForever);
                HAL_UART_Transmit(&huart3,
                    (uint8_t*)msg,
                    strlen(msg), 50);
                osMutexRelease(uart3MutexHandle);
            }
            else
            {
                alarm = ALARM_NONE;
                osMessageQueuePut(
                    alarmQueueHandle,
                    &alarm, 0, 0);
            }
        }
        else if(!baselineReady)
        {
            /* Keep trying to establish baseline */
            if(pressure > 0.0f)
            {
                baseline = pressure;
                baselineReady = 1;
            }
        }

        osDelay(500);
    }
}
/* USER CODE END Application */

