/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file    stm32l4xx_it.c
  * @brief   Interrupt Service Routines.
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
#include "stm32l4xx_it.h"
#include "FreeRTOS.h"
#include "task.h"
/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN TD */

/* USER CODE END TD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */

/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/
/* USER CODE BEGIN PV */
/* DIAGNOSTIC (round 5, Object Detection task-freeze investigation,
 * PROJECT_GUIDE.md Sec 14/15) - these 4 fault handlers were, until now,
 * the default CubeMX-generated empty stubs (just an immediate while(1)
 * trap, no diagnostic capture at all) - meaning a real hardware fault
 * would have looked externally identical to "a task silently stops",
 * exactly the symptom under investigation. g_fault_type identifies which
 * handler fired (1=Hard, 2=MemManage, 3=Bus, 4=Usage). CFSR
 * (Configurable Fault Status Register) and HFSR (HardFault Status
 * Register) are simple, always-safe-to-read Cortex-M registers that
 * together identify exactly what kind of fault occurred (e.g. a
 * stacking fault - the classic stack-overflow signature - vs. a bad
 * memory access, vs. an illegal instruction). MMFAR/BFAR give the
 * faulting address itself, when the corresponding CFSR "valid" bit says
 * it's meaningful. Deliberately NOT attempting to capture the exact
 * faulting PC/LR - that needs a naked/asm-wrapped handler reading the
 * hardware-stacked exception frame, which is real complexity this
 * project's "keep it simple" rule argues against for a diagnostic that
 * doesn't strictly need it; CFSR/HFSR alone are normally enough to tell
 * a stack overflow apart from a bad pointer access. Remove all of this
 * once the investigation concludes. */
volatile uint32_t g_fault_type = 0;
volatile uint32_t g_fault_cfsr = 0;
volatile uint32_t g_fault_hfsr = 0;
volatile uint32_t g_fault_mmfar = 0;
volatile uint32_t g_fault_bfar = 0;
/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
/* USER CODE BEGIN PFP */

/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */

/* USER CODE END 0 */

/* External variables --------------------------------------------------------*/
extern ADC_HandleTypeDef hadc1;
extern ADC_HandleTypeDef hadc2;
extern UART_HandleTypeDef huart2;
/* USER CODE BEGIN EV */

/* USER CODE END EV */

/******************************************************************************/
/*           Cortex-M4 Processor Interruption and Exception Handlers          */
/******************************************************************************/
/**
  * @brief This function handles Non maskable interrupt.
  */
void NMI_Handler(void)
{
  /* USER CODE BEGIN NonMaskableInt_IRQn 0 */

  /* USER CODE END NonMaskableInt_IRQn 0 */
  /* USER CODE BEGIN NonMaskableInt_IRQn 1 */
   while (1)
  {
  }
  /* USER CODE END NonMaskableInt_IRQn 1 */
}

/**
  * @brief This function handles Hard fault interrupt.
  */
void HardFault_Handler(void)
{
  /* USER CODE BEGIN HardFault_IRQn 0 */
  g_fault_type = 1;
  g_fault_cfsr = SCB->CFSR;
  g_fault_hfsr = SCB->HFSR;
  if (SCB->CFSR & SCB_CFSR_MMARVALID_Msk) { g_fault_mmfar = SCB->MMFAR; }
  if (SCB->CFSR & SCB_CFSR_BFARVALID_Msk) { g_fault_bfar = SCB->BFAR; }
  HAL_GPIO_WritePin(RGB_LED_3_GPIO_Port, RGB_LED_3_Pin, GPIO_PIN_SET); /* red - fault indicator, distinct from the heartbeat's blue blink */
  /* USER CODE END HardFault_IRQn 0 */
  while (1)
  {
    /* USER CODE BEGIN W1_HardFault_IRQn 0 */
    /* USER CODE END W1_HardFault_IRQn 0 */
  }
}

/**
  * @brief This function handles Memory management fault.
  */
void MemManage_Handler(void)
{
  /* USER CODE BEGIN MemoryManagement_IRQn 0 */
  g_fault_type = 2;
  g_fault_cfsr = SCB->CFSR;
  g_fault_hfsr = SCB->HFSR;
  if (SCB->CFSR & SCB_CFSR_MMARVALID_Msk) { g_fault_mmfar = SCB->MMFAR; }
  HAL_GPIO_WritePin(RGB_LED_3_GPIO_Port, RGB_LED_3_Pin, GPIO_PIN_SET);
  /* USER CODE END MemoryManagement_IRQn 0 */
  while (1)
  {
    /* USER CODE BEGIN W1_MemoryManagement_IRQn 0 */
    /* USER CODE END W1_MemoryManagement_IRQn 0 */
  }
}

/**
  * @brief This function handles Prefetch fault, memory access fault.
  */
void BusFault_Handler(void)
{
  /* USER CODE BEGIN BusFault_IRQn 0 */
  g_fault_type = 3;
  g_fault_cfsr = SCB->CFSR;
  g_fault_hfsr = SCB->HFSR;
  if (SCB->CFSR & SCB_CFSR_BFARVALID_Msk) { g_fault_bfar = SCB->BFAR; }
  HAL_GPIO_WritePin(RGB_LED_3_GPIO_Port, RGB_LED_3_Pin, GPIO_PIN_SET);
  /* USER CODE END BusFault_IRQn 0 */
  while (1)
  {
    /* USER CODE BEGIN W1_BusFault_IRQn 0 */
    /* USER CODE END W1_BusFault_IRQn 0 */
  }
}

/**
  * @brief This function handles Undefined instruction or illegal state.
  */
void UsageFault_Handler(void)
{
  /* USER CODE BEGIN UsageFault_IRQn 0 */
  g_fault_type = 4;
  g_fault_cfsr = SCB->CFSR;
  g_fault_hfsr = SCB->HFSR;
  HAL_GPIO_WritePin(RGB_LED_3_GPIO_Port, RGB_LED_3_Pin, GPIO_PIN_SET);
  /* USER CODE END UsageFault_IRQn 0 */
  while (1)
  {
    /* USER CODE BEGIN W1_UsageFault_IRQn 0 */
    /* USER CODE END W1_UsageFault_IRQn 0 */
  }
}

/**
  * @brief This function handles Debug monitor.
  */
void DebugMon_Handler(void)
{
  /* USER CODE BEGIN DebugMonitor_IRQn 0 */

  /* USER CODE END DebugMonitor_IRQn 0 */
  /* USER CODE BEGIN DebugMonitor_IRQn 1 */

  /* USER CODE END DebugMonitor_IRQn 1 */
}

/**
  * @brief This function handles System tick timer.
  */
void SysTick_Handler(void)
{
  /* USER CODE BEGIN SysTick_IRQn 0 */

  /* USER CODE END SysTick_IRQn 0 */
  /* Explicitly clear COUNTFLAG to avoid timing jitter in CMSIS-RTOS V2 */
#if (configUSE_TICKLESS_IDLE == 0)
  (void)SysTick->CTRL;
#endif
HAL_IncTick();
#if (INCLUDE_xTaskGetSchedulerState == 1 )
  if (xTaskGetSchedulerState() != taskSCHEDULER_NOT_STARTED)
  {
#endif /* INCLUDE_xTaskGetSchedulerState */
  xPortSysTickHandler();
#if (INCLUDE_xTaskGetSchedulerState == 1 )
  }
#endif /* INCLUDE_xTaskGetSchedulerState */
  /* USER CODE BEGIN SysTick_IRQn 1 */

  /* USER CODE END SysTick_IRQn 1 */
}

/******************************************************************************/
/* STM32L4xx Peripheral Interrupt Handlers                                    */
/* Add here the Interrupt Handlers for the used peripherals.                  */
/* For the available peripheral interrupt handler names,                      */
/* please refer to the startup file (startup_stm32l4xx.s).                    */
/******************************************************************************/

/**
  * @brief This function handles ADC1 and ADC2 interrupts.
  */
void ADC1_2_IRQHandler(void)
{
  /* USER CODE BEGIN ADC1_2_IRQn 0 */

  /* USER CODE END ADC1_2_IRQn 0 */
  HAL_ADC_IRQHandler(&hadc1);
  HAL_ADC_IRQHandler(&hadc2);
  /* USER CODE BEGIN ADC1_2_IRQn 1 */

  /* USER CODE END ADC1_2_IRQn 1 */
}

/**
  * @brief This function handles USART2 global interrupt.
  */
void USART2_IRQHandler(void)
{
  /* USER CODE BEGIN USART2_IRQn 0 */

  /* USER CODE END USART2_IRQn 0 */
  HAL_UART_IRQHandler(&huart2);
  /* USER CODE BEGIN USART2_IRQn 1 */

  /* USER CODE END USART2_IRQn 1 */
}

/* USER CODE BEGIN 1 */

/* USER CODE END 1 */
