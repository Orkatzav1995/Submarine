/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.c
  * @brief          : Main program body
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
#include "main.h"
#include "cmsis_os.h"
#include "fatfs.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include "protocol.h"
#include "comm_transport_uart.h"
#include "ir.h"
#include "button.h"
#include "buzzer.h"
#include "configuration.h"
#include "log.h"
#include "event.h"
#include "init.h"
#include "monitor.h"
#include "object_detection.h"
#include "keepalive.h"
#include "communication.h"
#include "tx_queue.h"
#include "rtc_sync.h"
#include "task.h" /* DIAGNOSTIC - raw FreeRTOS task API (uxTaskGetStackHighWaterMark, xPortGetFreeHeapSize) for the freeze investigation */
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */

/* Phase 1 (LNC Timestamp/RTC Hardening): a fixed, arbitrary-but-distinctive
 * value written to RTC_BKP_DR0 only after a fully successful RTC
 * synchronization (see RtcSync_ApplyEpoch() below). Confirmed via a
 * repository-wide search that no other project code or CubeMX-generated
 * file uses this backup register for anything else. */
#define RTC_SYNC_MARKER 0x52544301UL

/* Valid RTC_DateTypeDef.Year range (uint8_t offset from 2000, per the
 * STM32 HAL) - bounds which Unix epochs can actually be applied to this
 * RTC. Below 946684800 (2000-01-01) or above 4102444799 (2099-12-31) is
 * not representable and must be rejected by RtcSync_ApplyEpoch(). */
#define RTC_SYNC_MIN_EPOCH 946684800UL
#define RTC_SYNC_MAX_EPOCH 4102444799UL

/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/
ADC_HandleTypeDef hadc1;
ADC_HandleTypeDef hadc2;

RTC_HandleTypeDef hrtc;

SPI_HandleTypeDef hspi1;

TIM_HandleTypeDef htim3;
TIM_HandleTypeDef htim6;

UART_HandleTypeDef huart2;

/* Definitions for defaultTask */
osThreadId_t defaultTaskHandle;
const osThreadAttr_t defaultTask_attributes = {
  .name = "defaultTask",
  .stack_size = 128 * 4,
  .priority = (osPriority_t) osPriorityNormal,
};
/* Definitions for InitTask */
osThreadId_t InitTaskHandle;
const osThreadAttr_t InitTask_attributes = {
  .name = "InitTask",
  .stack_size = 1024 * 4,
  .priority = (osPriority_t) osPriorityLow,
};
/* Definitions for MonitorTask */
osThreadId_t MonitorTaskHandle;
const osThreadAttr_t MonitorTask_attributes = {
  .name = "MonitorTask",
  .stack_size = 1024 * 4,
  .priority = (osPriority_t) osPriorityLow,
};
/* Definitions for ObjectDetection */
osThreadId_t ObjectDetectionHandle;
const osThreadAttr_t ObjectDetection_attributes = {
  .name = "ObjectDetection",
  .stack_size = 1024 * 4,
  .priority = (osPriority_t) osPriorityLow,
};
/* Definitions for CommRxTask */
osThreadId_t CommRxTaskHandle;
const osThreadAttr_t CommRxTask_attributes = {
  .name = "CommRxTask",
  .stack_size = 1024 * 4,
  .priority = (osPriority_t) osPriorityLow,
};
/* Definitions for CommTxTask */
osThreadId_t CommTxTaskHandle;
const osThreadAttr_t CommTxTask_attributes = {
  .name = "CommTxTask",
  .stack_size = 128 * 4,
  .priority = (osPriority_t) osPriorityLow,
};
/* Definitions for KeepAliveTask */
osThreadId_t KeepAliveTaskHandle;
const osThreadAttr_t KeepAliveTask_attributes = {
  .name = "KeepAliveTask",
  .stack_size = 128 * 4,
  .priority = (osPriority_t) osPriorityLow,
};
/* Definitions for WatchdogTask */
osThreadId_t WatchdogTaskHandle;
const osThreadAttr_t WatchdogTask_attributes = {
  .name = "WatchdogTask",
  .stack_size = 128 * 4,
  .priority = (osPriority_t) osPriorityLow,
};
/* USER CODE BEGIN PV */
/* DIAGNOSTIC (round 5, Object Detection task-freeze investigation) - a
 * NEW, temporary 8th task, not one of Sec 7's approved 7. Per explicit
 * user instruction, for this investigation only - remove once concluded.
 * Very low priority, minimal stack, no blocking calls, no shared
 * resources other than one otherwise-unused LED pin (RGB_LED_2/PA8,
 * the blue channel - Event's led.c never drives this channel per its
 * own header comment, so this cannot visually conflict with Event's
 * real red/yellow/green usage). Purpose: if g_heartbeat_count keeps
 * rising while ObjectDetectionTask's own counters freeze, the problem is
 * scoped to ObjectDetectionTask (or something it specifically depends
 * on); if this ALSO freezes at the same time, the problem is system-wide
 * (scheduler/fault/heap/stack corruption). */
osThreadId_t HeartbeatTaskHandle;
const osThreadAttr_t HeartbeatTask_attributes = {
  .name = "HeartbeatTask",
  .stack_size = 128 * 4,
  .priority = (osPriority_t) osPriorityLow,
};
volatile uint32_t g_heartbeat_count = 0;

/* DIAGNOSTIC (round 5) - stack high-water mark (in words remaining, the
 * FreeRTOS convention - NOT bytes) for ObjectDetectionTask and
 * MonitorTask, sampled once per loop iteration in each. A value
 * approaching 0 near the ~5s failure point would indicate a stack
 * overflow is imminent/happening. */
volatile uint32_t g_od_stack_hwm = 0;
volatile uint32_t g_monitor_stack_hwm = 0;

/* DIAGNOSTIC (round 5) - free heap bytes (FreeRTOS heap_4), sampled from
 * both ObjectDetectionTask's and MonitorTask's loops. A sudden drop or
 * implausible plateau near the failure point would suggest heap
 * exhaustion or corruption. */
volatile uint32_t g_free_heap_size = 0;

/* DIAGNOSTIC (round 5) - set by vApplicationStackOverflowHook() (see
 * "USER CODE BEGIN 4" near the end of this file) if FreeRTOS's own
 * stack-overflow detection (just enabled in FreeRTOSConfig.h) ever
 * fires. g_stack_overflow_task_name points at the offending task's own
 * name string (owned by its TCB, safe to read - no task in this project
 * is ever deleted). */
volatile int g_stack_overflow_detected = 0;
volatile char *g_stack_overflow_task_name = 0;

/* Bring-up test counters for the Protocol+Transport loopback test.
   Watch these live in the debugger (Live Expressions / Watch window) -
   this is how we observe test results, since USART2 is reserved for
   protocol traffic only and never carries printf/debug text. */
volatile uint32_t g_frames_decoded_ok = 0;
volatile uint32_t g_frames_decode_error = 0;

/* NOTE: the DHT11/Battery/Light temporary read-and-watch test globals
   that used to live here (g_dht_last_result, g_dht_last_temperature,
   g_dht_last_humidity, g_dht_read_count, g_battery_last_voltage,
   g_light_last_percent) have been REMOVED now that Monitor (monitor.c,
   called from StartTask03/MonitorTask below) is the real, permanent
   caller of DHT_Read()/Battery_ReadVoltage()/Light_ReadPercent() - see
   g_monitor_* below instead. Keeping the old StartTask02 test loop
   calling these too would have made two different tasks call
   DHT_Read() concurrently, which is unsafe (dht.c's bit-banging has no
   reentrancy protection - see dht.c's own file header). All 3 drivers
   were already confirmed working on real hardware before this removal
   (Sec 14/15, PROJECT_GUIDE.md). */

/* TEMPORARY IR sensor hardware test value - same debugger-watch approach
   as above. Also used to determine the sensor's real polarity (currently
   UNCONFIRMED - see ir.h). Remove once the IR driver is confirmed working
   and its polarity is settled. */
volatile IrState g_ir_last_state = IR_NOT_DETECTED;

/* TEMPORARY button hardware test value - same debugger-watch approach as
   above. Also used to determine the button's real polarity (currently
   UNCONFIRMED - see button.h). Remove once the Button driver is
   confirmed working and its polarity is settled. */
volatile ButtonState g_button_last_state = BUTTON_NOT_PRESSED;

/* TEMPORARY buzzer hardware test flag - toggled each loop iteration so the
   buzzer audibly turns on/off every 2s on real hardware. Remove once the
   Buzzer driver is confirmed working. */
volatile uint8_t g_buzzer_test_on = 0;

/* TEMPORARY Configuration hardware test values - same debugger-watch
   approach as above. g_config_loaded_from_flash tells us whether this
   boot found valid data already in flash (1) or had to apply and save
   defaults (0, expected on the very first boot after this firmware is
   flashed). The two temperature values let us eyeball that the loaded
   values look right. Remove once Configuration is confirmed working. */
volatile int g_config_loaded_from_flash = 0;
volatile float g_config_temp_normal_low = 0.0f;
volatile float g_config_temp_normal_high = 0.0f;

/* TEMPORARY Log hardware test values - same debugger-watch approach as
   above. g_log_mounted tells us whether the SD card mounted successfully
   (this is the very first real use of the SD card/FatFs in this
   project). Remove once Log is confirmed working. */
volatile int g_log_init_called = 0; /* set to 1 as the very first line inside Log_Init() itself - confirms the function was actually reached */
volatile int g_log_mounted = 0;
volatile int g_log_mount_result = 0;

/* RTC hardware test values - the real date/time read from the RTC at
   boot (StartTask02, via HAL_RTC_GetTime()/HAL_RTC_GetDate()) and the
   Unix epoch DateToEpoch() converts it to, which is what's actually
   passed into Init_Start(). g_rtc_year/month/day/hour/minute/second let
   the real RTC reading be eyeballed directly against the wall clock;
   g_rtc_timestamp mirrors the exact value Init received. Replaces the
   old fixed g_log_test_timestamp placeholder now that the RTC is
   enabled - Monitor (see g_monitor_test_timestamp below) still has its
   own, separate, incrementing test clock for its own per-cycle
   timestamps, unaffected by this change. */
volatile uint16_t g_rtc_year = 0;
volatile uint8_t g_rtc_month = 0;
volatile uint8_t g_rtc_day = 0;
volatile uint8_t g_rtc_hour = 0;
volatile uint8_t g_rtc_minute = 0;
volatile uint8_t g_rtc_second = 0;
volatile uint32_t g_rtc_timestamp = 0;

/* Phase 1 (LNC Timestamp/RTC Hardening): true once the RTC has been set
 * from a real CC-supplied epoch since the last boot (via either
 * TAG_SET_RTC_DATETIME or TAG_SYSTEM_TIME_RESPONSE), or restored at boot
 * via the RTC_BKP_DR0 marker. Deliberately kept private to this file -
 * every other module queries it only through RtcSync_IsSynchronized()
 * (rtc_sync.h), never by name. See RtcSync_IsSynchronized()/
 * RtcSync_ApplyEpoch() further down for the only code that reads/writes
 * this variable. */
static volatile uint8_t rtc_synchronized = 0;

/* TEMPORARY Log write-path diagnostics - the FatFs result of the last
   f_open()/f_write()/f_close() call inside Log_Write(), so we can see
   exactly where a write is failing. 0 = FR_OK = success; nonzero = a
   specific FatFs error (see log.h's getter comments for the common
   ones). Remove once Log is confirmed working. */
volatile int g_log_open_result = 0;
volatile int g_log_write_result = 0;
volatile int g_log_close_result = 0;
volatile unsigned int g_log_bytes_written = 0;

/* TEMPORARY Event hardware test values - same debugger-watch approach as
   Log's. g_event_last_frame_length is the byte count Event_On*()
   returned for the most recent test call (0 would mean the CC-frame
   build failed). g_event_alarm_active mirrors Event_IsAlarmActive() so
   the alarm-active / button-silences-it behavior can be watched live.
   Remove once Event is confirmed working. */
volatile uint16_t g_event_last_frame_length = 0;
volatile int g_event_alarm_active = 0;
volatile int g_event_open_result = 0;
volatile int g_event_write_result = 0;
volatile int g_event_close_result = 0;

/* TEMPORARY Monitor hardware test values - same debugger-watch approach
   as every other module. No RTC exists yet, so g_monitor_test_timestamp
   is Monitor's own fixed test clock, incremented by 5s (Monitor's real
   Sec 2.1 cadence) once per real sampling cycle. g_monitor_sample_count
   confirms MonitorTask is actually running; the 4 sensor values + mode
   mirror the MeasurementSample Monitor_Sample() just built, so the
   classification logic can be watched live against Configuration's
   limits. Remove once Monitor is confirmed working. */
volatile uint32_t g_monitor_sample_count = 0;
volatile uint32_t g_monitor_test_timestamp = 1788134400UL; /* 2026-08-31 00:00:00 UTC */
volatile float g_monitor_last_temperature = 0.0f;
volatile float g_monitor_last_humidity = 0.0f;
volatile float g_monitor_last_light = 0.0f;
volatile float g_monitor_last_battery = 0.0f;
volatile uint8_t g_monitor_last_mode = MODE_NORMAL;

/* TEMPORARY Object Detection hardware test values - same debugger-watch
   approach as every other module. No RTC exists yet, so
   g_object_detection_test_timestamp is this task's own fixed test clock
   (see StartTask04's comment for why it increments by 1 every 20ms poll
   rather than tracking real time). g_object_detection_last_state mirrors
   ObjectDetection_GetCurrentState() so the debounced detect/clear state
   can be watched live. Remove once Object Detection is confirmed
   working. */
volatile uint32_t g_object_detection_test_timestamp = 1788134400UL; /* 2026-08-31 00:00:00 UTC */
volatile IrState g_object_detection_last_state = IR_NOT_DETECTED;

/* DIAGNOSTIC (investigating a reported "stuck at DETECTED" hardware
 * issue) - mirrors ObjectDetection_GetRawReading(), i.e. the raw
 * IR_Read() value from the most recent poll, NOT the debounced
 * g_object_detection_last_state above. Watch this with no IR source
 * present, then with a remote pointed at the sensor, then with the
 * remote removed again, to see whether the raw pin is actually stable
 * at idle or chattering on its own. Remove once understood. */
volatile IrState g_object_detection_raw_ir_state = IR_NOT_DETECTED;

/* DIAGNOSTIC (round 2 of the same investigation) - the DIRECT GPIO
 * read result on IR_Pin (PB10), 1 = GPIO_PIN_SET (HIGH), 0 =
 * GPIO_PIN_RESET (LOW), taken independently of ir.c's IR_Read()/IrState
 * conversion (a second, separate HAL_GPIO_ReadPin() call - safe, since a
 * plain digital input has no state/side effects to duplicate). Lets the
 * raw 0/1 level be compared side by side with g_object_detection_raw_ir_state
 * to rule in/out ir.c's HIGH/LOW-to-IrState mapping as a factor. Remove
 * once the IR investigation is concluded. */
volatile uint8_t g_object_detection_raw_gpio_level = 0;

/* DIAGNOSTIC (round 3) - increments unconditionally on every single
 * StartTask04 loop iteration, BEFORE anything else runs that iteration.
 * If this is NOT rising in the debugger (watch it over several seconds,
 * not just one snapshot), the task itself is not looping - a genuine
 * task-execution problem (hang/fault/starvation), not an IR signal
 * problem. If it IS rising steadily (~50/sec, matching the 20ms poll
 * interval) while g_object_detection_raw_gpio_level stays fixed at 1,
 * that proves HAL_GPIO_ReadPin() is genuinely being called every 20ms
 * and genuinely keeps reading HIGH at each sampled instant. Remove once
 * the IR investigation is concluded. */
volatile uint32_t g_object_detection_poll_count = 0;

/* DIAGNOSTIC (round 4) - one counter placed at each step of StartTask04's
 * loop body, in order. Each should equal g_object_detection_poll_count
 * if the loop is completing normally. Whichever one STOPS matching (is
 * lower than the ones before it, and stays there) pinpoints exactly
 * which step the task got stuck on. Remove once the IR investigation is
 * concluded. */
volatile uint32_t g_od_diag_before_poll = 0;          /* step 2: right before ObjectDetection_Poll() */
volatile uint32_t g_od_diag_after_poll = 0;            /* step 3: right after ObjectDetection_Poll() returns */
volatile uint32_t g_od_diag_before_button_check = 0;   /* step 4: right before Event_CheckAlarmButton() */
volatile uint32_t g_od_diag_after_button_check = 0;    /* step 5: right after Event_CheckAlarmButton() returns */
volatile uint32_t g_od_diag_before_delay = 0;          /* step 6: right before osDelay(20) */
volatile uint32_t g_od_diag_after_delay = 0;           /* step 7: right after osDelay(20) returns - if this stops but step 6 doesn't, osDelay() itself never returned */

/* TEMPORARY Keep-Alive hardware test values - same debugger-watch
   approach as every other module. g_keepalive_send_count confirms
   KeepAliveTask is actually running its real 6s cycle;
   g_keepalive_last_timestamp is the fresh RTC-derived value passed into
   KeepAlive_Send() that cycle; g_keepalive_last_frame_length mirrors
   KeepAlive_Send()'s return value (0 would mean Message_BuildKeepAlive()
   failed - should never happen with a 32-byte buffer, but worth
   watching). Remove once Keep-Alive is confirmed working. */
volatile uint32_t g_keepalive_send_count = 0;
volatile uint32_t g_keepalive_last_timestamp = 0;
volatile uint16_t g_keepalive_last_frame_length = 0;

/* TEMPORARY Communication dispatch hardware test values - same
   debugger-watch approach as every other module. Mirrors
   communication.c's own counters/last-tag getter after every dispatched
   frame, so a real CC-sent command's effect can be watched live (e.g.
   send a "set temp normal range" command and watch
   g_dispatch_set_limit_count go up). Remove once dispatch is confirmed
   working. */
volatile uint8_t g_dispatch_last_tag = 0;
volatile uint32_t g_dispatch_set_limit_count = 0;
volatile uint32_t g_dispatch_system_time_request_count = 0;
volatile uint32_t g_dispatch_deferred_count = 0;
volatile uint32_t g_dispatch_unknown_tag_count = 0;
/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
void PeriphCommonClock_Config(void);
static void MX_GPIO_Init(void);
static void MX_SPI1_Init(void);
static void MX_USART2_UART_Init(void);
static void MX_TIM6_Init(void);
static void MX_ADC1_Init(void);
static void MX_ADC2_Init(void);
static void MX_TIM3_Init(void);
static void MX_RTC_Init(void);
void StartDefaultTask(void *argument);
void StartTask02(void *argument);
void StartTask03(void *argument);
void StartTask04(void *argument);
void StartTask05(void *argument);
void StartTask06(void *argument);
void StartTask07(void *argument);
void StartTask08(void *argument);

/* USER CODE BEGIN PFP */
void StartHeartbeatTask(void *argument); /* DIAGNOSTIC - round 5, see its attributes/globals in USER CODE BEGIN PV */
/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */

/* USER CODE END 0 */

/**
  * @brief  The application entry point.
  * @retval int
  */
int main(void)
{

  /* USER CODE BEGIN 1 */

  /* USER CODE END 1 */

  /* MCU Configuration--------------------------------------------------------*/

  /* Reset of all peripherals, Initializes the Flash interface and the Systick. */
  HAL_Init();

  /* USER CODE BEGIN Init */

  /* USER CODE END Init */

  /* Configure the system clock */
  SystemClock_Config();

  /* Configure the peripherals common clocks */
  PeriphCommonClock_Config();

  /* USER CODE BEGIN SysInit */

  /* USER CODE END SysInit */

  /* Initialize all configured peripherals */
  MX_GPIO_Init();
  MX_SPI1_Init();
  MX_USART2_UART_Init();
  MX_FATFS_Init();
  MX_TIM6_Init();
  MX_ADC1_Init();
  MX_ADC2_Init();
  MX_TIM3_Init();
  MX_RTC_Init();
  /* USER CODE BEGIN 2 */

  /* USER CODE END 2 */

  /* Init scheduler */
  osKernelInitialize();

  /* USER CODE BEGIN RTOS_MUTEX */
  /* add mutexes, ... */
  /* USER CODE END RTOS_MUTEX */

  /* USER CODE BEGIN RTOS_SEMAPHORES */
  /* add semaphores, ... */
  /* USER CODE END RTOS_SEMAPHORES */

  /* USER CODE BEGIN RTOS_TIMERS */
  /* start timers, add new ones, ... */
  /* USER CODE END RTOS_TIMERS */

  /* USER CODE BEGIN RTOS_QUEUES */
  /* Sec 7: the 3 statically-allocated TX-priority queues (see
     tx_queue.c for why static, not heap-backed) - must exist before
     CommTxTask/KeepAliveTask/etc. start using them, so created here,
     before the scheduler starts. This marker is CubeMX's own
     regeneration-safe spot for exactly this ("add queues, ..."). */
  TxQueue_Init();
  /* USER CODE END RTOS_QUEUES */

  /* Create the thread(s) */
  /* creation of defaultTask */
  defaultTaskHandle = osThreadNew(StartDefaultTask, NULL, &defaultTask_attributes);

  /* creation of InitTask */
  InitTaskHandle = osThreadNew(StartTask02, NULL, &InitTask_attributes);

  /* creation of MonitorTask */
  MonitorTaskHandle = osThreadNew(StartTask03, NULL, &MonitorTask_attributes);

  /* creation of ObjectDetection */
  ObjectDetectionHandle = osThreadNew(StartTask04, NULL, &ObjectDetection_attributes);

  /* creation of CommRxTask */
  CommRxTaskHandle = osThreadNew(StartTask05, NULL, &CommRxTask_attributes);

  /* creation of CommTxTask */
  CommTxTaskHandle = osThreadNew(StartTask06, NULL, &CommTxTask_attributes);

  /* creation of KeepAliveTask */
  KeepAliveTaskHandle = osThreadNew(StartTask07, NULL, &KeepAliveTask_attributes);

  /* creation of WatchdogTask */
  WatchdogTaskHandle = osThreadNew(StartTask08, NULL, &WatchdogTask_attributes);

  /* USER CODE BEGIN RTOS_THREADS */
  /* DIAGNOSTIC (round 5) - temporary 8th task, see its attributes/globals
     in USER CODE BEGIN PV for why. Remove once the investigation concludes. */
  HeartbeatTaskHandle = osThreadNew(StartHeartbeatTask, NULL, &HeartbeatTask_attributes);
  /* USER CODE END RTOS_THREADS */

  /* USER CODE BEGIN RTOS_EVENTS */
  /* add events, ... */
  /* USER CODE END RTOS_EVENTS */

  /* Start scheduler */
  osKernelStart();

  /* We should never get here as control is now taken by the scheduler */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  while (1)
  {
    /* USER CODE END WHILE */

    /* USER CODE BEGIN 3 */
  }
  /* USER CODE END 3 */
}

/**
  * @brief System Clock Configuration
  * @retval None
  */
void SystemClock_Config(void)
{
  RCC_OscInitTypeDef RCC_OscInitStruct = {0};
  RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

  /** Configure the main internal regulator output voltage
  */
  if (HAL_PWREx_ControlVoltageScaling(PWR_REGULATOR_VOLTAGE_SCALE1) != HAL_OK)
  {
    Error_Handler();
  }

  /** Initializes the RCC Oscillators according to the specified parameters
  * in the RCC_OscInitTypeDef structure.
  */
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSI|RCC_OSCILLATORTYPE_LSI;
  RCC_OscInitStruct.HSIState = RCC_HSI_ON;
  RCC_OscInitStruct.HSICalibrationValue = RCC_HSICALIBRATION_DEFAULT;
  RCC_OscInitStruct.LSIState = RCC_LSI_ON;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
  RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSI;
  RCC_OscInitStruct.PLL.PLLM = 1;
  RCC_OscInitStruct.PLL.PLLN = 10;
  RCC_OscInitStruct.PLL.PLLP = RCC_PLLP_DIV7;
  RCC_OscInitStruct.PLL.PLLQ = RCC_PLLQ_DIV2;
  RCC_OscInitStruct.PLL.PLLR = RCC_PLLR_DIV2;
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
  {
    Error_Handler();
  }

  /** Initializes the CPU, AHB and APB buses clocks
  */
  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_SYSCLK
                              |RCC_CLOCKTYPE_PCLK1|RCC_CLOCKTYPE_PCLK2;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV1;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_4) != HAL_OK)
  {
    Error_Handler();
  }
}

/**
  * @brief Peripherals Common Clock Configuration
  * @retval None
  */
void PeriphCommonClock_Config(void)
{
  RCC_PeriphCLKInitTypeDef PeriphClkInit = {0};

  /** Initializes the peripherals clock
  */
  PeriphClkInit.PeriphClockSelection = RCC_PERIPHCLK_ADC;
  PeriphClkInit.AdcClockSelection = RCC_ADCCLKSOURCE_PLLSAI1;
  PeriphClkInit.PLLSAI1.PLLSAI1Source = RCC_PLLSOURCE_HSI;
  PeriphClkInit.PLLSAI1.PLLSAI1M = 1;
  PeriphClkInit.PLLSAI1.PLLSAI1N = 8;
  PeriphClkInit.PLLSAI1.PLLSAI1P = RCC_PLLP_DIV7;
  PeriphClkInit.PLLSAI1.PLLSAI1Q = RCC_PLLQ_DIV2;
  PeriphClkInit.PLLSAI1.PLLSAI1R = RCC_PLLR_DIV2;
  PeriphClkInit.PLLSAI1.PLLSAI1ClockOut = RCC_PLLSAI1_ADC1CLK;
  if (HAL_RCCEx_PeriphCLKConfig(&PeriphClkInit) != HAL_OK)
  {
    Error_Handler();
  }
}

/**
  * @brief ADC1 Initialization Function
  * @param None
  * @retval None
  */
static void MX_ADC1_Init(void)
{

  /* USER CODE BEGIN ADC1_Init 0 */

  /* USER CODE END ADC1_Init 0 */

  ADC_MultiModeTypeDef multimode = {0};
  ADC_ChannelConfTypeDef sConfig = {0};

  /* USER CODE BEGIN ADC1_Init 1 */

  /* USER CODE END ADC1_Init 1 */

  /** Common config
  */
  hadc1.Instance = ADC1;
  hadc1.Init.ClockPrescaler = ADC_CLOCK_ASYNC_DIV1;
  hadc1.Init.Resolution = ADC_RESOLUTION_12B;
  hadc1.Init.DataAlign = ADC_DATAALIGN_RIGHT;
  hadc1.Init.ScanConvMode = ADC_SCAN_DISABLE;
  hadc1.Init.EOCSelection = ADC_EOC_SINGLE_CONV;
  hadc1.Init.LowPowerAutoWait = DISABLE;
  hadc1.Init.ContinuousConvMode = DISABLE;
  hadc1.Init.NbrOfConversion = 1;
  hadc1.Init.DiscontinuousConvMode = DISABLE;
  hadc1.Init.ExternalTrigConv = ADC_SOFTWARE_START;
  hadc1.Init.ExternalTrigConvEdge = ADC_EXTERNALTRIGCONVEDGE_NONE;
  hadc1.Init.DMAContinuousRequests = DISABLE;
  hadc1.Init.Overrun = ADC_OVR_DATA_PRESERVED;
  hadc1.Init.OversamplingMode = DISABLE;
  if (HAL_ADC_Init(&hadc1) != HAL_OK)
  {
    Error_Handler();
  }

  /** Configure the ADC multi-mode
  */
  multimode.Mode = ADC_MODE_INDEPENDENT;
  if (HAL_ADCEx_MultiModeConfigChannel(&hadc1, &multimode) != HAL_OK)
  {
    Error_Handler();
  }

  /** Configure Regular Channel
  */
  sConfig.Channel = ADC_CHANNEL_5;
  sConfig.Rank = ADC_REGULAR_RANK_1;
  sConfig.SamplingTime = ADC_SAMPLETIME_2CYCLES_5;
  sConfig.SingleDiff = ADC_SINGLE_ENDED;
  sConfig.OffsetNumber = ADC_OFFSET_NONE;
  sConfig.Offset = 0;
  if (HAL_ADC_ConfigChannel(&hadc1, &sConfig) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN ADC1_Init 2 */

  /* USER CODE END ADC1_Init 2 */

}

/**
  * @brief ADC2 Initialization Function
  * @param None
  * @retval None
  */
static void MX_ADC2_Init(void)
{

  /* USER CODE BEGIN ADC2_Init 0 */

  /* USER CODE END ADC2_Init 0 */

  ADC_ChannelConfTypeDef sConfig = {0};

  /* USER CODE BEGIN ADC2_Init 1 */

  /* USER CODE END ADC2_Init 1 */

  /** Common config
  */
  hadc2.Instance = ADC2;
  hadc2.Init.ClockPrescaler = ADC_CLOCK_ASYNC_DIV1;
  hadc2.Init.Resolution = ADC_RESOLUTION_8B;
  hadc2.Init.DataAlign = ADC_DATAALIGN_RIGHT;
  hadc2.Init.ScanConvMode = ADC_SCAN_DISABLE;
  hadc2.Init.EOCSelection = ADC_EOC_SINGLE_CONV;
  hadc2.Init.LowPowerAutoWait = DISABLE;
  hadc2.Init.ContinuousConvMode = DISABLE;
  hadc2.Init.NbrOfConversion = 1;
  hadc2.Init.DiscontinuousConvMode = DISABLE;
  hadc2.Init.ExternalTrigConv = ADC_SOFTWARE_START;
  hadc2.Init.ExternalTrigConvEdge = ADC_EXTERNALTRIGCONVEDGE_NONE;
  hadc2.Init.DMAContinuousRequests = DISABLE;
  hadc2.Init.Overrun = ADC_OVR_DATA_PRESERVED;
  hadc2.Init.OversamplingMode = DISABLE;
  if (HAL_ADC_Init(&hadc2) != HAL_OK)
  {
    Error_Handler();
  }

  /** Configure Regular Channel
  */
  sConfig.Channel = ADC_CHANNEL_6;
  sConfig.Rank = ADC_REGULAR_RANK_1;
  sConfig.SamplingTime = ADC_SAMPLETIME_2CYCLES_5;
  sConfig.SingleDiff = ADC_SINGLE_ENDED;
  sConfig.OffsetNumber = ADC_OFFSET_NONE;
  sConfig.Offset = 0;
  if (HAL_ADC_ConfigChannel(&hadc2, &sConfig) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN ADC2_Init 2 */

  /* USER CODE END ADC2_Init 2 */

}

/**
  * @brief RTC Initialization Function
  * @param None
  * @retval None
  */
static void MX_RTC_Init(void)
{

  /* USER CODE BEGIN RTC_Init 0 */

  /* USER CODE END RTC_Init 0 */

  RTC_TimeTypeDef sTime = {0};
  RTC_DateTypeDef sDate = {0};

  /* USER CODE BEGIN RTC_Init 1 */

  /* USER CODE END RTC_Init 1 */

  /** Initialize RTC Only
  */
  hrtc.Instance = RTC;
  hrtc.Init.HourFormat = RTC_HOURFORMAT_24;
  hrtc.Init.AsynchPrediv = 127;
  hrtc.Init.SynchPrediv = 255;
  hrtc.Init.OutPut = RTC_OUTPUT_DISABLE;
  hrtc.Init.OutPutRemap = RTC_OUTPUT_REMAP_NONE;
  hrtc.Init.OutPutPolarity = RTC_OUTPUT_POLARITY_HIGH;
  hrtc.Init.OutPutType = RTC_OUTPUT_TYPE_OPENDRAIN;
  if (HAL_RTC_Init(&hrtc) != HAL_OK)
  {
    Error_Handler();
  }

  /* USER CODE BEGIN Check_RTC_BKUP */
  /* Phase 1 (LNC Timestamp/RTC Hardening): RTC_BKP_DR0 lives in the same
   * backup domain as the RTC calendar itself - it survives a software
   * reset and, as long as VBAT is maintained, a power cycle. If a
   * previous successful sync left our marker there, the RTC's current
   * value was carried over too, so the fixed default below is skipped
   * and the retained value is trusted immediately. If the marker is
   * missing (first-ever boot, or the backup domain lost power - both
   * look identical from software, by design, see PROJECT_GUIDE.md), the
   * default set below runs and the LNC stays unsynchronized until a
   * real sync happens. */
  if (HAL_RTCEx_BKUPRead(&hrtc, RTC_BKP_DR0) == RTC_SYNC_MARKER)
  {
    rtc_synchronized = 1;
  }
  /* USER CODE END Check_RTC_BKUP */

  /** Initialize RTC and set the Time and Date
  */
  sTime.Hours = 0x10;
  sTime.Minutes = 0x31;
  sTime.Seconds = 0x0;
  sTime.DayLightSaving = RTC_DAYLIGHTSAVING_NONE;
  sTime.StoreOperation = RTC_STOREOPERATION_RESET;
  if (HAL_RTC_SetTime(&hrtc, &sTime, RTC_FORMAT_BCD) != HAL_OK)
  {
    Error_Handler();
  }
  sDate.WeekDay = RTC_WEEKDAY_MONDAY;
  sDate.Month = RTC_MONTH_JANUARY;
  sDate.Date = 0x1;
  sDate.Year = 0x0;

  if (HAL_RTC_SetDate(&hrtc, &sDate, RTC_FORMAT_BCD) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN RTC_Init 2 */

  /* USER CODE END RTC_Init 2 */

}

/**
  * @brief SPI1 Initialization Function
  * @param None
  * @retval None
  */
static void MX_SPI1_Init(void)
{

  /* USER CODE BEGIN SPI1_Init 0 */

  /* USER CODE END SPI1_Init 0 */

  /* USER CODE BEGIN SPI1_Init 1 */

  /* USER CODE END SPI1_Init 1 */
  /* SPI1 parameter configuration*/
  hspi1.Instance = SPI1;
  hspi1.Init.Mode = SPI_MODE_MASTER;
  hspi1.Init.Direction = SPI_DIRECTION_2LINES;
  hspi1.Init.DataSize = SPI_DATASIZE_8BIT;
  hspi1.Init.CLKPolarity = SPI_POLARITY_LOW;
  hspi1.Init.CLKPhase = SPI_PHASE_1EDGE;
  hspi1.Init.NSS = SPI_NSS_SOFT;
  hspi1.Init.BaudRatePrescaler = SPI_BAUDRATEPRESCALER_128;
  hspi1.Init.FirstBit = SPI_FIRSTBIT_MSB;
  hspi1.Init.TIMode = SPI_TIMODE_DISABLE;
  hspi1.Init.CRCCalculation = SPI_CRCCALCULATION_DISABLE;
  hspi1.Init.CRCPolynomial = 7;
  hspi1.Init.CRCLength = SPI_CRC_LENGTH_DATASIZE;
  hspi1.Init.NSSPMode = SPI_NSS_PULSE_ENABLE;
  if (HAL_SPI_Init(&hspi1) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN SPI1_Init 2 */

  /* USER CODE END SPI1_Init 2 */

}

/**
  * @brief TIM3 Initialization Function
  * @param None
  * @retval None
  */
static void MX_TIM3_Init(void)
{

  /* USER CODE BEGIN TIM3_Init 0 */

  /* USER CODE END TIM3_Init 0 */

  TIM_MasterConfigTypeDef sMasterConfig = {0};
  TIM_OC_InitTypeDef sConfigOC = {0};

  /* USER CODE BEGIN TIM3_Init 1 */

  /* USER CODE END TIM3_Init 1 */
  htim3.Instance = TIM3;
  htim3.Init.Prescaler = 399;
  htim3.Init.CounterMode = TIM_COUNTERMODE_UP;
  htim3.Init.Period = 100;
  htim3.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
  htim3.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_DISABLE;
  if (HAL_TIM_PWM_Init(&htim3) != HAL_OK)
  {
    Error_Handler();
  }
  sMasterConfig.MasterOutputTrigger = TIM_TRGO_RESET;
  sMasterConfig.MasterSlaveMode = TIM_MASTERSLAVEMODE_DISABLE;
  if (HAL_TIMEx_MasterConfigSynchronization(&htim3, &sMasterConfig) != HAL_OK)
  {
    Error_Handler();
  }
  sConfigOC.OCMode = TIM_OCMODE_PWM1;
  sConfigOC.Pulse = 0;
  sConfigOC.OCPolarity = TIM_OCPOLARITY_HIGH;
  sConfigOC.OCFastMode = TIM_OCFAST_DISABLE;
  if (HAL_TIM_PWM_ConfigChannel(&htim3, &sConfigOC, TIM_CHANNEL_1) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN TIM3_Init 2 */

  /* USER CODE END TIM3_Init 2 */
  HAL_TIM_MspPostInit(&htim3);

}

/**
  * @brief TIM6 Initialization Function
  * @param None
  * @retval None
  */
static void MX_TIM6_Init(void)
{

  /* USER CODE BEGIN TIM6_Init 0 */

  /* USER CODE END TIM6_Init 0 */

  TIM_MasterConfigTypeDef sMasterConfig = {0};

  /* USER CODE BEGIN TIM6_Init 1 */

  /* USER CODE END TIM6_Init 1 */
  htim6.Instance = TIM6;
  htim6.Init.Prescaler = 79;
  htim6.Init.CounterMode = TIM_COUNTERMODE_UP;
  htim6.Init.Period = 999;
  htim6.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_DISABLE;
  if (HAL_TIM_Base_Init(&htim6) != HAL_OK)
  {
    Error_Handler();
  }
  sMasterConfig.MasterOutputTrigger = TIM_TRGO_RESET;
  sMasterConfig.MasterSlaveMode = TIM_MASTERSLAVEMODE_DISABLE;
  if (HAL_TIMEx_MasterConfigSynchronization(&htim6, &sMasterConfig) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN TIM6_Init 2 */

  /* USER CODE END TIM6_Init 2 */

}

/**
  * @brief USART2 Initialization Function
  * @param None
  * @retval None
  */
static void MX_USART2_UART_Init(void)
{

  /* USER CODE BEGIN USART2_Init 0 */

  /* USER CODE END USART2_Init 0 */

  /* USER CODE BEGIN USART2_Init 1 */

  /* USER CODE END USART2_Init 1 */
  huart2.Instance = USART2;
  huart2.Init.BaudRate = 115200;
  huart2.Init.WordLength = UART_WORDLENGTH_8B;
  huart2.Init.StopBits = UART_STOPBITS_1;
  huart2.Init.Parity = UART_PARITY_NONE;
  huart2.Init.Mode = UART_MODE_TX_RX;
  huart2.Init.HwFlowCtl = UART_HWCONTROL_NONE;
  huart2.Init.OverSampling = UART_OVERSAMPLING_16;
  huart2.Init.OneBitSampling = UART_ONE_BIT_SAMPLE_DISABLE;
  huart2.AdvancedInit.AdvFeatureInit = UART_ADVFEATURE_NO_INIT;
  if (HAL_UART_Init(&huart2) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN USART2_Init 2 */

  /* USER CODE END USART2_Init 2 */

}

/**
  * @brief GPIO Initialization Function
  * @param None
  * @retval None
  */
static void MX_GPIO_Init(void)
{
  GPIO_InitTypeDef GPIO_InitStruct = {0};
  /* USER CODE BEGIN MX_GPIO_Init_1 */

  /* USER CODE END MX_GPIO_Init_1 */

  /* GPIO Ports Clock Enable */
  __HAL_RCC_GPIOA_CLK_ENABLE();
  __HAL_RCC_GPIOB_CLK_ENABLE();
  __HAL_RCC_GPIOC_CLK_ENABLE();

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(RGB_LED_3_GPIO_Port, RGB_LED_3_Pin, GPIO_PIN_RESET);

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(GPIOA, RGB_LED_2_Pin|RGB_LED_1_Pin, GPIO_PIN_RESET);

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(GPIOB, DHT_Pin|SD_CS_Pin, GPIO_PIN_RESET);

  /*Configure GPIO pin : IR_Pin */
  GPIO_InitStruct.Pin = IR_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  HAL_GPIO_Init(IR_GPIO_Port, &GPIO_InitStruct);

  /*Configure GPIO pin : RGB_LED_3_Pin */
  GPIO_InitStruct.Pin = RGB_LED_3_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(RGB_LED_3_GPIO_Port, &GPIO_InitStruct);

  /*Configure GPIO pins : RGB_LED_2_Pin RGB_LED_1_Pin */
  GPIO_InitStruct.Pin = RGB_LED_2_Pin|RGB_LED_1_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

  /*Configure GPIO pin : Button_Pin */
  GPIO_InitStruct.Pin = Button_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  HAL_GPIO_Init(Button_GPIO_Port, &GPIO_InitStruct);

  /*Configure GPIO pin : DHT_Pin */
  GPIO_InitStruct.Pin = DHT_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_OD;
  GPIO_InitStruct.Pull = GPIO_PULLUP;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(DHT_GPIO_Port, &GPIO_InitStruct);

  /*Configure GPIO pin : SD_CS_Pin */
  GPIO_InitStruct.Pin = SD_CS_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(SD_CS_GPIO_Port, &GPIO_InitStruct);

  /* USER CODE BEGIN MX_GPIO_Init_2 */

  /* USER CODE END MX_GPIO_Init_2 */
}

/* USER CODE BEGIN 4 */
int _write(int file, char *ptr, int len)
{
    HAL_UART_Transmit(&huart2, (uint8_t *)ptr, len, HAL_MAX_DELAY);
    return len;
}

/* Is "year" a leap year? Same Gregorian rule as log.c's private
 * IsLeapYear() - duplicated here rather than shared, for the same
 * reason log.c's own EpochToDate()/IsLeapYear() were duplicated into
 * event.c: log.c's versions are static/private and Log is finished/
 * verified/do-not-touch. */
static int IsLeapYearForRtc(uint16_t year)
{
    if (year % 400 == 0) return 1;
    if (year % 100 == 0) return 0;
    return (year % 4 == 0);
}

/* Converts a calendar date/time (as just read from the hardware RTC)
 * into a Unix epoch timestamp (seconds since 1970-01-01 UTC) - the
 * exact inverse of log.c's private EpochToDate(), written the same way
 * (simple counting loops, no library date functions) for the same
 * beginner-friendly readability. month is 1-12, day is 1-31. */
static uint32_t DateToEpoch(uint16_t year, uint8_t month, uint8_t day, uint8_t hour, uint8_t minute, uint8_t second)
{
    static const uint8_t daysInMonth[12] = { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };
    uint32_t days = 0;
    uint16_t y;
    uint8_t m;

    for (y = 1970; y < year; y++)
    {
        days += IsLeapYearForRtc(y) ? 366 : 365;
    }

    for (m = 0; m < (month - 1); m++)
    {
        uint8_t dim = daysInMonth[m];
        if (m == 1 && IsLeapYearForRtc(year)) /* February */
        {
            dim = 29;
        }
        days += dim;
    }

    days += (day - 1);

    return (days * 86400UL) + ((uint32_t)hour * 3600UL) + ((uint32_t)minute * 60UL) + second;
}

/* Phase 1 (LNC Timestamp/RTC Hardening): the exact inverse of DateToEpoch()
 * above - converts a Unix epoch timestamp back into calendar fields, so a
 * CC-supplied epoch (TAG_SET_RTC_DATETIME / TAG_SYSTEM_TIME_RESPONSE) can
 * be applied to the RTC. Same "simple counting loop, no library date
 * functions" style as DateToEpoch()/log.c's own EpochToDate() - not
 * reused from log.c because that one is private/date-only (no time-of-day)
 * and log.c is explicitly do-not-touch (PROJECT_GUIDE.md). Mathematically
 * valid for the full uint32_t range (1970-01-01 through 2106-02-07), but
 * callers must separately bound-check against RTC_SYNC_MIN_EPOCH/
 * RTC_SYNC_MAX_EPOCH first - RTC_DateTypeDef.Year is a uint8_t offset from
 * 2000, so only 2000-2099 can actually be written to the RTC hardware. */
static void EpochToCalendar(uint32_t epochSeconds, uint16_t *outYear, uint8_t *outMonth,
                             uint8_t *outDay, uint8_t *outHour, uint8_t *outMinute, uint8_t *outSecond)
{
    static const uint8_t daysInMonth[12] = { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };
    uint32_t remaining = epochSeconds;
    uint32_t daysSinceEpoch;
    uint16_t year = 1970;
    uint8_t month = 0;

    *outSecond = (uint8_t)(remaining % 60UL); remaining /= 60UL;
    *outMinute = (uint8_t)(remaining % 60UL); remaining /= 60UL;
    *outHour   = (uint8_t)(remaining % 24UL); remaining /= 24UL;
    daysSinceEpoch = remaining;

    for (;;)
    {
        uint16_t daysInThisYear = IsLeapYearForRtc(year) ? 366 : 365;
        if (daysSinceEpoch < daysInThisYear)
        {
            break;
        }
        daysSinceEpoch -= daysInThisYear;
        year++;
    }
    *outYear = year;

    for (;;)
    {
        uint8_t dim = daysInMonth[month];
        if (month == 1 && IsLeapYearForRtc(year)) /* February */
        {
            dim = 29;
        }
        if (daysSinceEpoch < dim)
        {
            break;
        }
        daysSinceEpoch -= dim;
        month++;
    }
    *outMonth = (uint8_t)(month + 1);
    *outDay = (uint8_t)(daysSinceEpoch + 1);
}

/* Phase 1 (LNC Timestamp/RTC Hardening): the only code anywhere that reads
 * "rtc_synchronized" - every other module queries this function instead
 * (rtc_sync.h), never the variable itself. */
int RtcSync_IsSynchronized(void)
{
    return rtc_synchronized ? 1 : 0;
}

/* Phase 1 (LNC Timestamp/RTC Hardening): the single shared path both
 * TAG_SET_RTC_DATETIME and TAG_SYSTEM_TIME_RESPONSE use to actually apply
 * a CC-supplied epoch to the RTC (communication.c calls this - see its
 * own two dispatch cases). Validates the epoch is representable by this
 * RTC's hardware (RTC_DateTypeDef.Year is a uint8_t offset from 2000),
 * converts it, sets the RTC, and - only on full success - writes the
 * backup marker and marks synchronized. On any failure, returns 0 without
 * writing the marker or changing rtc_synchronized, so a partially-applied
 * value is never trusted, this boot or after a future reboot. */
int RtcSync_ApplyEpoch(uint32_t epochSeconds)
{
    RTC_TimeTypeDef sTime = {0};
    RTC_DateTypeDef sDate = {0};
    uint16_t year;
    uint8_t month, day, hour, minute, second;

    if (epochSeconds < RTC_SYNC_MIN_EPOCH || epochSeconds > RTC_SYNC_MAX_EPOCH)
    {
        return 0;
    }

    EpochToCalendar(epochSeconds, &year, &month, &day, &hour, &minute, &second);

    sTime.Hours = hour;
    sTime.Minutes = minute;
    sTime.Seconds = second;
    sTime.DayLightSaving = RTC_DAYLIGHTSAVING_NONE;
    sTime.StoreOperation = RTC_STOREOPERATION_RESET;
    if (HAL_RTC_SetTime(&hrtc, &sTime, RTC_FORMAT_BIN) != HAL_OK)
    {
        return 0;
    }

    /* WeekDay is never actually read anywhere in this project (DateToEpoch/
     * EpochToCalendar don't use it, nothing downstream queries it) - a
     * fixed placeholder matches MX_RTC_Init()'s own existing precedent
     * exactly (it also hardcodes RTC_WEEKDAY_MONDAY regardless of the
     * real date). HAL_RTC_SetDate() asserts WeekDay is a valid enum value
     * (stm32l4xx_hal_rtc.c), so this isn't optional - leaving it at 0
     * would fail that check if full asserts are ever enabled. */
    sDate.WeekDay = RTC_WEEKDAY_MONDAY;
    sDate.Month = month;
    sDate.Date = day;
    sDate.Year = (uint8_t)(year - 2000);
    if (HAL_RTC_SetDate(&hrtc, &sDate, RTC_FORMAT_BIN) != HAL_OK)
    {
        return 0;
    }

    HAL_RTCEx_BKUPWrite(&hrtc, RTC_BKP_DR0, RTC_SYNC_MARKER);
    rtc_synchronized = 1;
    return 1;
}

/* DIAGNOSTIC (round 5, Object Detection freeze investigation) - see the
 * globals/attributes in USER CODE BEGIN PV for the full explanation.
 * Toggles RGB_LED_2 (PA8, blue channel - unused by Event) directly via
 * raw HAL, bypassing led.c entirely, so this can't be confused with or
 * interfere with Event's own LED usage. No blocking calls. */
void StartHeartbeatTask(void *argument)
{
    for (;;)
    {
        g_heartbeat_count++;
        HAL_GPIO_TogglePin(RGB_LED_2_GPIO_Port, RGB_LED_2_Pin);
        osDelay(150);
    }
}

/* DIAGNOSTIC (round 5) - required by FreeRTOS once
 * configCHECK_FOR_STACK_OVERFLOW is enabled (FreeRTOSConfig.h). Called
 * from within the RTOS kernel itself if a task's stack overflow is
 * detected - captures which task via the globals declared in USER CODE
 * BEGIN PV, then deliberately traps in a while(1) instead of letting
 * execution continue in an already-corrupted state. */
void vApplicationStackOverflowHook(TaskHandle_t xTask, char *pcTaskName)
{
    (void)xTask;
    g_stack_overflow_detected = 1;
    g_stack_overflow_task_name = pcTaskName;
    for (;;)
    {
    }
}
/* USER CODE END 4 */

/* USER CODE BEGIN Header_StartDefaultTask */
/**
  * @brief  Function implementing the defaultTask thread.
  * @param  argument: Not used
  * @retval None
  */
/* USER CODE END Header_StartDefaultTask */
void StartDefaultTask(void *argument)
{
  /* USER CODE BEGIN 5 */
  /* Infinite loop */
  for(;;)
  {

    osDelay(1);
  }
  /* USER CODE END 5 */
}

/* USER CODE BEGIN Header_StartTask02 */
/**
* @brief Function implementing the InitTask thread.
* @param argument: Not used
* @retval None
*/
/* USER CODE END Header_StartTask02 */
void StartTask02(void *argument)
{
  /* USER CODE BEGIN StartTask02 */
  /* Read the real boot time from the hardware RTC and convert it to a
     Unix epoch timestamp for Init_Start() - this is the real time
     source Init_Start()'s timestamp parameter was always designed to
     accept (see init.h's header comment), now that RTC is enabled.
     HAL_RTC_GetTime() must be called before HAL_RTC_GetDate() even
     though only the date is used here - this is a real STM32 HAL
     requirement (reading the time unlocks the date shadow registers),
     not a style choice; skipping it would risk reading a stale date. */
  {
      RTC_TimeTypeDef rtcTime;
      RTC_DateTypeDef rtcDate;

      HAL_RTC_GetTime(&hrtc, &rtcTime, RTC_FORMAT_BIN);
      HAL_RTC_GetDate(&hrtc, &rtcDate, RTC_FORMAT_BIN);

      g_rtc_year = 2000 + rtcDate.Year; /* HAL stores Year as an offset from 2000 */
      g_rtc_month = rtcDate.Month;
      g_rtc_day = rtcDate.Date;
      g_rtc_hour = rtcTime.Hours;
      g_rtc_minute = rtcTime.Minutes;
      g_rtc_second = rtcTime.Seconds;
      g_rtc_timestamp = DateToEpoch(g_rtc_year, g_rtc_month, g_rtc_day, g_rtc_hour, g_rtc_minute, g_rtc_second);
  }

  /* Init module (Sec 2.7) now owns the one-shot boot sequence: it calls
     Configuration_Init()/Log_Init()/Event_Init(), in that order, then
     reports a startup event to Event - see init.c. */
  Init_Start(g_rtc_timestamp);

  /* Mirror Configuration's/Log's/Event's own state into the existing
     debugger-watch globals below - these are all getters on each
     module's own internal state, unaffected by Init now owning the
     call site above. Remove once Init/Configuration/Log/Event are all
     confirmed working together. */
  g_config_loaded_from_flash = Config_WasLoadedFromFlash();
  {
      float tempNormalLow, tempNormalHigh;
      Config_GetTempNormalRange(&tempNormalLow, &tempNormalHigh);
      g_config_temp_normal_low = tempNormalLow;
      g_config_temp_normal_high = tempNormalHigh;
  }
  g_log_mount_result = Log_GetLastMountResult();
  g_log_mounted = (g_log_mount_result == 0); /* 0 == FR_OK */
  g_event_last_frame_length = Init_GetLastStartupFrameLength();
  g_event_open_result = Event_GetLastOpenResult();
  g_event_write_result = Event_GetLastWriteResult();
  g_event_close_result = Event_GetLastCloseResult();

  /* Infinite loop - what's left here now is only what hasn't been
     claimed by a real module/task yet: IR and Button are still only
     driver-level (no Object Detection Application module exists yet),
     and the Buzzer toggle is that driver's own original bring-up test.
     DHT/Battery/Light/Log-write/mode-transition-cycle test code that
     used to live here has been REMOVED - Monitor (StartTask03 below) is
     now the real, permanent caller of all of that. */
  for(;;)
  {
    /* TEMPORARY: IR sensor hardware test - one pin read per loop, stored
       in g_ir_last_state so it can be watched live in the debugger while
       placing/removing an object in front of the sensor. This is also how
       we will confirm the sensor's real polarity (see ir.h). */
    g_ir_last_state = IR_Read();

    /* TEMPORARY: button hardware test - one pin read per loop, stored in
       g_button_last_state so it can be watched live in the debugger while
       pressing/releasing the button. This is also how we will confirm the
       button's real polarity (see button.h). */
    g_button_last_state = Button_Read();

    /* NOTE: Event_CheckAlarmButton() used to be polled here (every 2s).
       It has MOVED to StartTask04/ObjectDetectionTask's 20ms loop below,
       per this session's decision - alarm-silencing is far more
       responsive there. g_button_last_state above is unaffected (still
       read here every 2s purely for its own debugger-watch/polarity-
       confirmation purpose). */

    /* TEMPORARY: buzzer hardware test - toggle the buzzer on/off each
       loop iteration so it can be heard turning on and off every 2s on
       real hardware. */
    if (g_buzzer_test_on)
    {
        Buzzer_Off();
        g_buzzer_test_on = 0;
    }
    else
    {
     //   Buzzer_On();
        g_buzzer_test_on = 1;
    }

    osDelay(2000);
  }
  /* USER CODE END StartTask02 */
}

/* USER CODE BEGIN Header_StartTask03 */
/**
* @brief Function implementing the MonitorTask thread.
* @param argument: Not used
* @retval None
*/
/* USER CODE END Header_StartTask03 */
void StartTask03(void *argument)
{
  /* USER CODE BEGIN StartTask03 */
  /* This is Monitor's real implementation (Sec 2.1), not temporary test
     code - MonitorTask is the task Sec 7's design always intended to run
     it. Monitor_Init() resets Monitor's internal state and starts the
     DHT11 driver's timer (moved here from StartTask02 - Monitor is now
     the sole owner/caller of DHT_Read()). */
  Monitor_Init();

  /* Infinite loop - one sampling cycle every 5 seconds (Sec 2.1). The
     osDelay(5000) runs BEFORE the first sample, not after: InitTask
     (StartTask02) calls Configuration_Init()/Log_Init()/Event_Init() at
     boot, and both tasks currently share the same priority
     (osPriorityLow) with no explicit startup-order guarantee between
     them (Sec 7's frozen RTOS design has no synchronization primitive
     for this). A 5s head start gives those quick, synchronous calls
     ample time to finish before Monitor's first real Config_Get*()/
     Log_Write()/Event_* call - a simple, pragmatic mitigation, not a
     proper fix; revisit with a real synchronization primitive if this
     ever proves insufficient. */
  for(;;)
  {
    osDelay(5000);

    {
      MeasurementSample sample;
      RTC_TimeTypeDef rtcTime;
      RTC_DateTypeDef rtcDate;
      uint32_t timestamp;

      /* Phase 1 (LNC Timestamp/RTC Hardening): real RTC read, same
         pattern already proven in CommRxTask/KeepAliveTask - g_monitor_
         test_timestamp is no longer used for the production timestamp
         (see PROJECT_GUIDE.md). HAL_RTC_GetTime() must be called before
         HAL_RTC_GetDate() even though only the date is used elsewhere -
         a real STM32 HAL requirement, not a style choice. */
      HAL_RTC_GetTime(&hrtc, &rtcTime, RTC_FORMAT_BIN);
      HAL_RTC_GetDate(&hrtc, &rtcDate, RTC_FORMAT_BIN);
      timestamp = DateToEpoch(2000 + rtcDate.Year, rtcDate.Month, rtcDate.Date,
                               rtcTime.Hours, rtcTime.Minutes, rtcTime.Seconds);

      Monitor_Sample(timestamp, &sample);

      g_monitor_last_temperature = sample.temperature;
      g_monitor_last_humidity = sample.humidity;
      g_monitor_last_light = sample.light;
      g_monitor_last_battery = sample.battery;
      g_monitor_last_mode = sample.mode;
      g_monitor_sample_count++;

      /* Log's diagnostics reflect whichever call to Log_Write() happened
         most recently - that's now this one, since Monitor_Sample()
         calls Log_Write() internally every cycle. */
      g_log_open_result = Log_GetLastOpenResult();
      g_log_write_result = Log_GetLastWriteResult();
      g_log_close_result = Log_GetLastCloseResult();
      g_log_bytes_written = Log_GetLastBytesWritten();

      /* Event's diagnostics only change on a cycle where Monitor_Sample()
         actually detected a mode change and called Event_OnModeTransition()
         internally - otherwise these simply keep reflecting the last time
         that happened, which is still a valid "last known" value to watch. */
      g_event_alarm_active = Event_IsAlarmActive();
      g_event_open_result = Event_GetLastOpenResult();
      g_event_write_result = Event_GetLastWriteResult();
      g_event_close_result = Event_GetLastCloseResult();

      /* DIAGNOSTIC (round 5) - stack high-water mark (words remaining)
         and free heap, sampled every cycle so they can be correlated
         against ObjectDetectionTask's own copies at the ~5s freeze point. */
      g_monitor_stack_hwm = (uint32_t)uxTaskGetStackHighWaterMark(NULL);
      g_free_heap_size = (uint32_t)xPortGetFreeHeapSize();
    }
  }
  /* USER CODE END StartTask03 */
}

/* USER CODE BEGIN Header_StartTask04 */
/**
* @brief Function implementing the ObjectDetection thread.
* @param argument: Not used
* @retval None
*/
/* USER CODE END Header_StartTask04 */
void StartTask04(void *argument)
{
  /* USER CODE BEGIN StartTask04 */
  /* This is Object Detection's real implementation (Sec 2.2), not
     temporary test code - ObjectDetectionTask is the task Sec 7's design
     always intended to run it (priority 3, continuous poll). */
  ObjectDetection_Init();

  /* Infinite loop - one poll every IR_POLL_INTERVAL_MS (20ms). No RTC
     exists yet, so g_object_detection_test_timestamp is this task's own
     fixed test clock - it only actually matters on the rare poll where
     the state changes and Event_OnObjectDetection() is called, so
     incrementing it by 1 "second" every poll (not truly 1:1 with real
     time) is a harmless test-only simplification, same spirit as every
     other module's fake test clock. */
  for(;;)
  {
    RTC_TimeTypeDef rtcTime;
    RTC_DateTypeDef rtcDate;
    uint32_t timestamp;

    g_object_detection_poll_count++; /* DIAGNOSTIC step 1 - counts every loop entry, unconditionally, first thing */

    /* Phase 1 (LNC Timestamp/RTC Hardening): real RTC read, same pattern
       already proven in CommRxTask/KeepAliveTask/MonitorTask -
       g_object_detection_test_timestamp is no longer used for the
       production timestamp (see PROJECT_GUIDE.md). A fast, non-blocking
       pair of register reads every 20ms poll, same reasoning CommRxTask's
       own comment already documents for its own per-frame RTC read. */
    HAL_RTC_GetTime(&hrtc, &rtcTime, RTC_FORMAT_BIN);
    HAL_RTC_GetDate(&hrtc, &rtcDate, RTC_FORMAT_BIN);
    timestamp = DateToEpoch(2000 + rtcDate.Year, rtcDate.Month, rtcDate.Date,
                             rtcTime.Hours, rtcTime.Minutes, rtcTime.Seconds);

    g_od_diag_before_poll = g_object_detection_poll_count; /* DIAGNOSTIC step 2 */
    ObjectDetection_Poll(timestamp);
    g_od_diag_after_poll = g_object_detection_poll_count; /* DIAGNOSTIC step 3 */

    g_object_detection_last_state = ObjectDetection_GetCurrentState();
    g_object_detection_raw_ir_state = ObjectDetection_GetRawReading(); /* DIAGNOSTIC - see PV comment */
    g_object_detection_raw_gpio_level = (HAL_GPIO_ReadPin(IR_GPIO_Port, IR_Pin) == GPIO_PIN_SET) ? 1 : 0; /* DIAGNOSTIC - direct GPIO read, bypasses ir.c entirely */

    /* Sec 2.3's "button press stops alarm" - moved here from
       StartTask02's 2s loop, per this session's decision: checking every
       20ms (this task's own cadence) makes silencing an active alarm far
       more responsive. Harmless to call every iteration; it only does
       anything if Event's alarm is currently active. */
    g_od_diag_before_button_check = g_object_detection_poll_count; /* DIAGNOSTIC step 4 */
    Event_CheckAlarmButton();
    g_od_diag_after_button_check = g_object_detection_poll_count; /* DIAGNOSTIC step 5 */

    g_od_diag_before_delay = g_object_detection_poll_count; /* DIAGNOSTIC step 6 */
    osDelay(IR_POLL_INTERVAL_MS);
    g_od_diag_after_delay = g_object_detection_poll_count; /* DIAGNOSTIC step 7 */

    /* DIAGNOSTIC (round 5) - stack high-water mark (words remaining) and
       free heap, sampled every cycle so they can be correlated against
       MonitorTask's own copies right at the ~5s freeze point. */
    g_od_stack_hwm = (uint32_t)uxTaskGetStackHighWaterMark(NULL);
    g_free_heap_size = (uint32_t)xPortGetFreeHeapSize();
  }
  /* USER CODE END StartTask04 */
}

/* USER CODE BEGIN Header_StartTask05 */
/**
* @brief Function implementing the CommRxTask thread.
* @param argument: Not used
* @retval None
*/
/* USER CODE END Header_StartTask05 */
void StartTask05(void *argument)
{
  /* USER CODE BEGIN StartTask05 */
  static ProtocolDecoder decoder;
  static ProtocolFrame decoded_frame;
  ProtocolDecodeStatus status;
  uint8_t received_byte;

  Protocol_DecoderInit(&decoder);

  /* Infinite loop */
  for(;;)
  {
    if (Transport_UART_ReceiveByte(&received_byte, 20) == 1)
    {
      status = Protocol_FeedByte(&decoder, received_byte, &decoded_frame);

      if (status == PROTOCOL_DECODE_FRAME_READY)
      {
        g_frames_decoded_ok++;

        /* Sec 2.5: act on the decoded command. A fresh RTC read here
           (only when a frame actually arrived, not every 20ms idle poll)
           is a fast, non-blocking pair of register reads - it does not
           risk starving KeepAliveTask the way a real delay would.
           DateToEpoch() is the same static helper StartTask02/StartTask07
           already use - no new helper needed, same translation unit. */
        {
          RTC_TimeTypeDef rtcTime;
          RTC_DateTypeDef rtcDate;
          uint32_t timestamp;

          HAL_RTC_GetTime(&hrtc, &rtcTime, RTC_FORMAT_BIN);
          HAL_RTC_GetDate(&hrtc, &rtcDate, RTC_FORMAT_BIN);
          timestamp = DateToEpoch(2000 + rtcDate.Year, rtcDate.Month, rtcDate.Date,
                                   rtcTime.Hours, rtcTime.Minutes, rtcTime.Seconds);

          Communication_Dispatch(&decoded_frame, timestamp);

          g_dispatch_last_tag = Communication_GetLastDispatchedTag();
          g_dispatch_set_limit_count = Communication_GetSetLimitCount();
          g_dispatch_system_time_request_count = Communication_GetSystemTimeRequestCount();
          g_dispatch_deferred_count = Communication_GetDeferredCount();
          g_dispatch_unknown_tag_count = Communication_GetUnknownTagCount();
        }
      }
      else if (status == PROTOCOL_DECODE_ERROR)
      {
        g_frames_decode_error++;
      }
    }
    else
    {
      /* No byte arrived within the timeout. This osDelay() is required,
         not optional: without it, this task would never truly block,
         and KeepAliveTask (lower priority) would be starved of CPU time.
         See comm_transport_uart.h / PROJECT_GUIDE.md section 7. */
      osDelay(1);
    }
  }
  /* USER CODE END StartTask05 */
}

/* USER CODE BEGIN Header_StartTask06 */
/**
* @brief Function implementing the CommTxTask thread.
* @param argument: Not used
* @retval None
*/
/* USER CODE END Header_StartTask06 */
void StartTask06(void *argument)
{
  /* USER CODE BEGIN StartTask06 */
  /* Sec 7: CommTxTask is now the ONLY task that ever calls
     Transport_UART_Send() - every sender (Keep-Alive, Event's callers,
     Communication's system-time reply) hands its already-built frame to
     tx_queue.c instead of sending directly. Each loop iteration checks
     all 3 queues in strict priority order (TxQueue_DrainOne()) and
     sends whatever it finds; if all 3 are empty, osDelay(1) yields -
     same "don't busy-spin, don't starve lower-priority tasks" discipline
     CommRxTask already uses on its own idle path (Sec 7).

     (The fixed-tag-0x01 bring-up test frame that lived here has served
     its purpose - it proved Transport+Protocol work together on real
     hardware - and is removed now that real senders exist.) */
  uint8_t frame_buffer[TX_QUEUE_MAX_ITEM_SIZE];
  uint16_t frame_length;

  /* Infinite loop */
  for(;;)
  {
    frame_length = TxQueue_DrainOne(frame_buffer, sizeof(frame_buffer));

    if (frame_length > 0)
    {
      Transport_UART_Send(frame_buffer, frame_length, 100);
    }
    else
    {
      osDelay(1);
    }
  }
  /* USER CODE END StartTask06 */
}

/* USER CODE BEGIN Header_StartTask07 */
/**
* @brief Function implementing the KeepAliveTask thread.
* @param argument: Not used
* @retval None
*/
/* USER CODE END Header_StartTask07 */
void StartTask07(void *argument)
{
  /* USER CODE BEGIN StartTask07 */
  /* Sec 2.8: every 6 seconds, send a keepalive to the CC containing a
     fresh timestamp plus Monitor's latest measurement data and mode.
     Reads the RTC directly here, the same pattern StartTask02/InitTask
     already established for Init_Start()'s timestamp - no Application
     module in this project reads the RTC internally, the caller always
     supplies a plain uint32_t timestamp (see keepalive.h). HAL_RTC_GetTime()
     must be called before HAL_RTC_GetDate() even though only the date
     feeds into DateToEpoch() below along with the time fields - this is
     a real STM32 HAL requirement (reading the time unlocks the date
     shadow registers), not a style choice. DateToEpoch() is the same
     static helper StartTask02 already uses - no new helper needed, same
     translation unit. */
  for(;;)
  {
    RTC_TimeTypeDef rtcTime;
    RTC_DateTypeDef rtcDate;
    uint32_t timestamp;

    HAL_RTC_GetTime(&hrtc, &rtcTime, RTC_FORMAT_BIN);
    HAL_RTC_GetDate(&hrtc, &rtcDate, RTC_FORMAT_BIN);
    timestamp = DateToEpoch(2000 + rtcDate.Year, rtcDate.Month, rtcDate.Date,
                             rtcTime.Hours, rtcTime.Minutes, rtcTime.Seconds);

    g_keepalive_last_frame_length = KeepAlive_Send(timestamp);
    g_keepalive_last_timestamp = timestamp;
    g_keepalive_send_count++;

    osDelay(6000);
  }
  /* USER CODE END StartTask07 */
}

/* USER CODE BEGIN Header_StartTask08 */
/**
* @brief Function implementing the WatchdogTask thread.
* @param argument: Not used
* @retval None
*/
/* USER CODE END Header_StartTask08 */
void StartTask08(void *argument)
{
  /* USER CODE BEGIN StartTask08 */
  /* IWDG is windowed (Prescaler=4, Reload=4095, Window=999 - NOT changed
     here, see PROJECT_GUIDE.md): counter clock is LSI(~32kHz)/4 = 8kHz,
     so the full timeout is ~512ms, and a refresh is only ACCEPTED between
     ~387ms and ~512ms after the previous one (too early resets it just
     like too late). 450ms is the midpoint of that window, giving roughly
     +-62ms of margin either side. Delay first, then refresh: the very
     first refresh must also land inside the window measured from boot,
     not happen immediately at task start. */
  for(;;)
  {
    osDelay(450);
 //   HAL_IWDG_Refresh(&hiwdg);
  }
  /* USER CODE END StartTask08 */
}

/**
  * @brief  This function is executed in case of error occurrence.
  * @retval None
  */
void Error_Handler(void)
{
  /* USER CODE BEGIN Error_Handler_Debug */
  /* User can add his own implementation to report the HAL error return state */
  __disable_irq();
  while (1)
  {
  }
  /* USER CODE END Error_Handler_Debug */
}
#ifdef USE_FULL_ASSERT
/**
  * @brief  Reports the name of the source file and the source line number
  *         where the assert_param error has occurred.
  * @param  file: pointer to the source file name
  * @param  line: assert_param error line source number
  * @retval None
  */
void assert_failed(uint8_t *file, uint32_t line)
{
  /* USER CODE BEGIN 6 */
  /* User can add his own implementation to report the file name and line number,
     ex: printf("Wrong parameters value: file %s on line %d\r\n", file, line) */
  /* USER CODE END 6 */
}
#endif /* USE_FULL_ASSERT */
