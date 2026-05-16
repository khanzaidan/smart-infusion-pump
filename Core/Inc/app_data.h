#ifndef INC_APP_DATA_H_
#define INC_APP_DATA_H_

#include "cmsis_os.h"
#include "stdint.h"
#include "stdbool.h"

/* Alarm states */
typedef enum {
    ALARM_NONE          = 0,
    ALARM_BUBBLE        = 1,
    ALARM_OCCLUSION     = 2,
    ALARM_LIMIT_HIT     = 3,
    ALARM_BATTERY_LOW   = 4,
    ALARM_TEMP_HIGH     = 5,
    ALARM_WATCHDOG_FAIL = 6,
    ALARM_MOTOR_STALL   = 7
} AlarmState_t;

/* Motor command types */
typedef enum {
    MOTOR_CMD_START = 0,
    MOTOR_CMD_STOP  = 1,
    MOTOR_CMD_PAUSE = 2
} MotorCmdType_t;

/* Motor command structure */
typedef struct {
    MotorCmdType_t  command;
    uint32_t        targetSteps;
    uint32_t        stepFrequencyHz;
    uint8_t         direction;
    float           flowRateMLperHr;
} MotorCommand_t;

/* Queue handles */
extern osMessageQueueId_t motorCommandQueueHandle;
extern osMessageQueueId_t temperatureQueueHandle;
extern osMessageQueueId_t flowRateQueueHandle;
extern osMessageQueueId_t volumeQueueHandle;
extern osMessageQueueId_t alarmQueueHandle;
extern osMessageQueueId_t batteryQueueHandle;

/* Semaphore and mutex handles */
/* Semaphore and __mutex__ handles */
extern osSemaphoreId_t    emergencyStopSemHandle;
extern osMutexId_t        i2c1MutexHandle;
extern osMutexId_t        i2c2MutexHandle;
extern osMutexId_t        uart3MutexHandle;
/* System constants */
#define STEPS_PER_ML_20ML   1274
#define STEPS_PER_ML_10ML   1985
#define STEPS_PER_ML_5ML    3183
#define STEPS_PER_ML_50ML   509
#define MAX_FLOW_RATE_MLH   1000.0f
#define MIN_FLOW_RATE_MLH   1.0f
#define TEMP_TARGET_C       37.0f
#define OCCLUSION_DELTA_HPA 20.0f
#define WDI_KICK_MS         500

/* Peripheral handles from main.c */
extern I2C_HandleTypeDef hi2c1;
extern I2C_HandleTypeDef hi2c2;
extern TIM_HandleTypeDef htim2;
extern TIM_HandleTypeDef htim3;
extern UART_HandleTypeDef huart1;
extern UART_HandleTypeDef huart3;
extern ADC_HandleTypeDef hadc1;

#endif
