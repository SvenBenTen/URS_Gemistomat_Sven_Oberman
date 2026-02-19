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
#include "i2c.h"
#include "tim.h"
#include "gpio.h"

/* USER CODE BEGIN Includes */
#include "liquidcrystal_i2c.h"
#include <stdio.h>
/* USER CODE END Includes */

/* USER CODE BEGIN PTD */
typedef enum {
  DRINK_SKROPEC = 0,
  DRINK_SPORTSKI,
  DRINK_POLA_POLA,
  DRINK_COUNT
} drink_t;

typedef enum {
  ST_IDLE = 0,
  ST_RUN_WINE,
  ST_RUN_WATER,
  ST_DONE
} system_state_t;
/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */
// Tipke
#define BTN_SELECT_PORT GPIOC
#define BTN_SELECT_PIN  GPIO_PIN_0

#define BTN_START_PORT  GPIOC
#define BTN_START_PIN   GPIO_PIN_1

// Debounce
#define DEBOUNCE_TICKS  20
/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/

/* USER CODE BEGIN PV */
volatile uint32_t g_tick10ms = 0;
volatile uint32_t g_seconds = 0;


system_state_t state = ST_IDLE;

// Ukupni volumen (ml)
uint16_t total_ml = 200;

// Izračunatee količine (ml)
uint16_t target_wine_ml = 0;
uint16_t target_water_ml = 0;

// Protok pumpe
float flow_wine_ml_s  = 16.0f;
float flow_water_ml_s = 16.0f;

volatile uint32_t phase_ticks_left = 0;

// Izbor vrste gemišta
drink_t selected_drink = DRINK_SKROPEC;
uint8_t wine_percent = 20;
uint8_t water_percent = 80;

// START zahtjev
uint8_t start_request = 0;
/* USER CODE END PV */


void SystemClock_Config(void);
/* USER CODE BEGIN PFP */
static inline uint8_t BTN_SELECT_Pressed(void);
static inline uint8_t BTN_START_Pressed(void);
static void update_drink_ratio(void);
static void lcd_show_menu(void);
/* USER CODE END PFP */


/* USER CODE BEGIN 0 */
static inline uint8_t BTN_SELECT_Pressed(void)
{
  return (HAL_GPIO_ReadPin(BTN_SELECT_PORT, BTN_SELECT_PIN) == GPIO_PIN_RESET);
}

static inline uint8_t BTN_START_Pressed(void)
{
  return (HAL_GPIO_ReadPin(BTN_START_PORT, BTN_START_PIN) == GPIO_PIN_RESET);
}

static void update_drink_ratio(void)
{
  switch (selected_drink)
  {
    case DRINK_SKROPEC:
      wine_percent = 20; water_percent = 80;
      break;
    case DRINK_SPORTSKI:
      wine_percent = 30; water_percent = 70;
      break;
    case DRINK_POLA_POLA:
      wine_percent = 50; water_percent = 50;
      break;
    default:
      wine_percent = 30; water_percent = 70;
      break;
  }
}

static void lcd_show_menu(void)
{
  // Prikaz: "Odabir:" + naziv opcije
  HD44780_SetCursor(0,0);
  HD44780_PrintStr("Odabir:        ");

  HD44780_SetCursor(0,1);
  HD44780_PrintStr("> ");

  switch (selected_drink)
  {
    case DRINK_SKROPEC:
      HD44780_PrintStr("Skropec     ");
      break;
    case DRINK_SPORTSKI:
      HD44780_PrintStr("Sportski    ");
      break;
    case DRINK_POLA_POLA:
      HD44780_PrintStr("Pola-pola   ");
      break;
    default:
      HD44780_PrintStr("???         ");
      break;
  }
}

// PUMP / RELAY CONTROL
static inline void PUMP_WINE_ON(void)   { HAL_GPIO_WritePin(GPIOA, GPIO_PIN_8, GPIO_PIN_SET);   }
static inline void PUMP_WINE_OFF(void)  { HAL_GPIO_WritePin(GPIOA, GPIO_PIN_8, GPIO_PIN_RESET); }
static inline void PUMP_WATER_ON(void)  { HAL_GPIO_WritePin(GPIOA, GPIO_PIN_9, GPIO_PIN_SET);   }
static inline void PUMP_WATER_OFF(void) { HAL_GPIO_WritePin(GPIOA, GPIO_PIN_9, GPIO_PIN_RESET); }


static uint32_t seconds_to_ticks(float sec)
{
  if (sec <= 0.0f) return 0;
  float ticks_f = sec * 100.0f; // 100 tickova = 1s
  if (ticks_f < 1.0f) ticks_f = 1.0f;
  return (uint32_t)(ticks_f + 0.5f);
}

static void compute_targets(void)
{
  target_wine_ml  = (uint16_t)((total_ml * wine_percent) / 100);
  target_water_ml = (uint16_t)(total_ml - target_wine_ml);
}

static void stop_all(void)
{
  PUMP_WINE_OFF();
  PUMP_WATER_OFF();
  phase_ticks_left = 0;
  state = ST_IDLE;
  start_request = 0;
}

static void start_cycle(void)
{
  compute_targets();

  float wine_s = (flow_wine_ml_s > 0.01f) ? ((float)target_wine_ml / flow_wine_ml_s) : 0.0f;

  phase_ticks_left = seconds_to_ticks(wine_s);

  PUMP_WATER_OFF();
  PUMP_WINE_ON();
  state = ST_RUN_WINE;

  start_request = 0;
}

/* USER CODE END 0 */

/**
  * @brief  The application entry point.
  * @retval int
  */
int main(void)
{
  HAL_Init();

  /* Configure the system clock */
  SystemClock_Config();

  /* USER CODE BEGIN SysInit */
  SystemCoreClockUpdate();
  /* USER CODE END SysInit */

  MX_GPIO_Init();
  MX_I2C1_Init();
  MX_TIM2_Init();

  /* USER CODE BEGIN 2 */

  update_drink_ratio();

  // LCD init
  //HAL_Delay(50);
  HD44780_Init(2);
  HD44780_Clear();
  PUMP_WINE_OFF();
  PUMP_WATER_OFF();

  // Početni prikaz
  lcd_show_menu();

  // Timer start (nakon LCD init-a)
  HAL_TIM_Base_Start_IT(&htim2);
  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  while (1)
  {

    /* USER CODE END WHILE */
    /* USER CODE BEGIN 3 */
    // Debounce
    static uint32_t last_btn_time = 0;
    static uint8_t sel_prev = 0;
    static uint8_t start_prev = 0;

    uint8_t sel_now = BTN_SELECT_Pressed();
    uint8_t start_now = BTN_START_Pressed();

    if ((g_tick10ms - last_btn_time) >= DEBOUNCE_TICKS)
    {
    	if (sel_now && !sel_prev)
    	{
    	  last_btn_time = g_tick10ms;

    	  if (state == ST_IDLE)
    	  {
    	    selected_drink = (drink_t)((selected_drink + 1) % DRINK_COUNT);
    	    update_drink_ratio();
    	    lcd_show_menu();
    	  }
    	}

    	if (start_now && !start_prev)
    	{
    	  last_btn_time = g_tick10ms;

    	  if (state == ST_IDLE)
    	  {
    	    start_request = 1;
    	  }
    	  else if (state == ST_DONE)
    	  {
    	    state = ST_IDLE;
    	    lcd_show_menu();
    	  }
    	  else
    	  {

    	    stop_all();
    	    lcd_show_menu();
    	  }
    	}
    }

    sel_prev = sel_now;
    start_prev = start_now;

    if (start_request && state == ST_IDLE)
    {
      start_cycle();
    }

    char line[17];

    if (state == ST_IDLE)
    {

    }
    else if (state == ST_RUN_WINE)
    {
      HD44780_SetCursor(0,0);
      HD44780_PrintStr("Tocim VINO     ");

      HD44780_SetCursor(0,1);
      snprintf(line, sizeof(line), "%3uml %3lus    ",
               target_wine_ml, (unsigned long)(phase_ticks_left/100));
      HD44780_PrintStr(line);
    }
    else if (state == ST_RUN_WATER)
    {
      HD44780_SetCursor(0,0);
      HD44780_PrintStr("Tocim VODU     ");

      HD44780_SetCursor(0,1);
      snprintf(line, sizeof(line), "%3uml %3lus    ",
               target_water_ml, (unsigned long)(phase_ticks_left/100));
      HD44780_PrintStr(line);
    }
    else if (state == ST_DONE)
    {
      HD44780_SetCursor(0,0);
      HD44780_PrintStr("Gotovo!        ");
      HD44780_SetCursor(0,1);
      HD44780_PrintStr("START=reset    ");
    }
    /* USER CODE END 3 */
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

  /** Configure the main internal regulator output voltage */
  __HAL_RCC_PWR_CLK_ENABLE();
  __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE3);

  /** Initializes the RCC Oscillators according to the specified parameters */
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSI;
  RCC_OscInitStruct.HSIState = RCC_HSI_ON;
  RCC_OscInitStruct.HSICalibrationValue =RCC_HSICALIBRATION_DEFAULT;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_NONE;
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
  {
    Error_Handler();
  }

  /** Initializes the CPU, AHB and APB buses clocks */
  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_SYSCLK
                              |RCC_CLOCKTYPE_PCLK1|RCC_CLOCKTYPE_PCLK2;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_HSI;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV1;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_0) != HAL_OK)
  {
    Error_Handler();
  }
}

/* USER CODE BEGIN 4 */
void HAL_TIM_PeriodElapsedCallback(TIM_HandleTypeDef *htim)
{
  if (htim->Instance == TIM2)
  {
    // 10ms tick
    g_tick10ms++;

    if (phase_ticks_left > 0)
    {
      phase_ticks_left--;

      if (phase_ticks_left == 0)
      {
        if (state == ST_RUN_WINE)
        {
          // Prebacivanje na vodu
          float water_s = (flow_water_ml_s > 0.01f) ? ((float)target_water_ml / flow_water_ml_s) : 0.0f;
          phase_ticks_left = seconds_to_ticks(water_s);

          PUMP_WINE_OFF();
          PUMP_WATER_ON();
          state = ST_RUN_WATER;
        }
        else if (state == ST_RUN_WATER)
        {
          PUMP_WATER_OFF();
          state = ST_DONE;
        }
      }
    }

    if ((g_tick10ms % 100U) == 0U)
    {
      g_seconds++;
    }
  }
}
/* USER CODE END 4 */

/**
  * @brief  This function is executed in case of error occurrence.
  * @retval None
  */
void Error_Handler(void)
{
  /* USER CODE BEGIN Error_Handler_Debug */
  __disable_irq();
  while (1)
  {
  }
  /* USER CODE END Error_Handler_Debug */
}

#ifdef  USE_FULL_ASSERT
void assert_failed(uint8_t *file, uint32_t line)
{
  /* USER CODE BEGIN 6 */
  /* USER CODE END 6 */
}
#endif /* USE_FULL_ASSERT */


