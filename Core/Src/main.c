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
#include "adc.h"
#include "tim.h"
#include "usart.h"
#include "gpio.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include <string.h>
#include <stdio.h>
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

/* USER CODE BEGIN PV */
volatile uint8_t led_auto_control = 1;  // 1=自动控制，0=手动控制
volatile uint32_t led_off_timer = 0;    // 用于led:off后的2秒计时
/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
/* USER CODE BEGIN PFP */

/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */
void HAL_DelayUs(uint32_t us)
{
  uint32_t start = SysTick->VAL;
  uint32_t ticks = us * (SystemCoreClock / 1000000);
  uint32_t elapsed = 0;
  
  while (elapsed < ticks) {
    uint32_t current = SysTick->VAL;
    if (current <= start) {
      elapsed = start - current;
    } else {
      elapsed = start + (SysTick->LOAD - current);
    }
  }
}

// ADC读取函数
uint16_t ADC_Read(void)
{
  HAL_ADC_Start(&hadc1);
  if (HAL_ADC_PollForConversion(&hadc1, 10) == HAL_OK) {
    return HAL_ADC_GetValue(&hadc1);
  }
  return 0;
}

// 手动控制LED打开（最亮）
void LED_ManualOn(void)
{
  led_auto_control = 0;
  PWM_SetDutyCycle(0);  // 最亮
}

// 手动控制LED关闭（最暗），2秒后恢复自动控制
void LED_ManualOff(void)
{
  led_auto_control = 0;
  PWM_SetDutyCycle(65535);  // 最暗
  led_off_timer = HAL_GetTick();  // 记录当前时间
}
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

  /* USER CODE BEGIN SysInit */

  /* USER CODE END SysInit */

  /* Initialize all configured peripherals */
  MX_GPIO_Init();
  MX_ADC1_Init();
  MX_TIM2_Init();
  MX_USART2_UART_Init();
  MX_USART1_UART_Init();
  /* USER CODE BEGIN 2 */
  HAL_TIM_PWM_Start(&htim2, TIM_CHANNEL_1); // 启动PWM
  USART1_StartRx();  // 启动USART1中断接收
  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  while (1)
  {
    /* USER CODE END WHILE */

    /* USER CODE BEGIN 3 */
    // 处理USART1接收到的命令
    USART1_ProcessCommand();
    
    // 检查是否需要恢复自动控制（led:off后2秒）
    if (!led_auto_control && led_off_timer != 0) {
      if (HAL_GetTick() - led_off_timer >= 2000) {
        led_auto_control = 1;
        led_off_timer = 0;
      }
    }
    
    // 1. 读取光照值 (ADC范围0-4095)
    static uint8_t current_level = 0; // 当前档位 (0-4)
    static uint8_t alarm_sent = 0; // 距离报警标志
    static uint8_t dark_sent = 0;    // 光线过暗报警标志
    static uint8_t bright_sent = 0;  // 光线过亮报警标志
    uint16_t light_value = ADC_Read();
    
    // 只有在自动控制模式下才进行PWM自动调节
    if (led_auto_control) {
      // 2. 5个档位 + 滞回（Hysteresis）避免边界抖动
      // 每个档位有50的滞回区间
      switch (current_level) {
        case 0:
          if (light_value > 869) current_level = 1;
          break;
        case 1:
          if (light_value < 769) current_level = 0;
          else if (light_value > 1688) current_level = 2;
          break;
        case 2:
          if (light_value < 1588) current_level = 1;
          else if (light_value > 2507) current_level = 3;
          break;
        case 3:
          if (light_value < 2407) current_level = 2;
          else if (light_value > 3326) current_level = 4;
          break;
        case 4:
          if (light_value < 3226) current_level = 3;
          break;
      }
      
      // 3. 根据档位设置PWM (反转逻辑：暗->亮)
      uint16_t pwm_levels[5] = {65535, 49152, 32768, 16384, 0};
      PWM_SetDutyCycle(pwm_levels[current_level]);
      
      // 4. 光线报警（0档过暗，4档过亮）
      if (current_level == 0 && !dark_sent) {
        USART2_SendDark();
        dark_sent = 1;
        bright_sent = 0; // 清除其他标志
      } else if (current_level == 4 && !bright_sent) {
        USART2_SendBright();
        bright_sent = 1;
        dark_sent = 0; // 清除其他标志
      } else if (current_level > 0 && current_level < 4) {
        // 中间档位，清除两个标志
        dark_sent = 0;
        bright_sent = 0;
      }
    }
    
    // 5. 发送光照值
    USART1_SendLight(light_value);
    
    // 6. HC-SR04测距
    float distance = HCSR04_Measure();
    if (distance > 0) {
      USART1_SendDistance(distance);
      
      // 7. 距离低于5cm时，通过USART2发送报警
      if (distance < 5.0f && !alarm_sent) {
        USART2_SendWarning();
        alarm_sent = 1;
      } 
      // 距离超过5cm时，清除报警标志
      else if (distance >= 5.0f) {
        alarm_sent = 0;
      }
    }
    
    HAL_Delay(2000);
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
  RCC_PeriphCLKInitTypeDef PeriphClkInit = {0};

  /** Initializes the RCC Oscillators according to the specified parameters
  * in the RCC_OscInitTypeDef structure.
  */
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSE;
  RCC_OscInitStruct.HSEState = RCC_HSE_ON;
  RCC_OscInitStruct.HSEPredivValue = RCC_HSE_PREDIV_DIV1;
  RCC_OscInitStruct.HSIState = RCC_HSI_ON;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
  RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSE;
  RCC_OscInitStruct.PLL.PLLMUL = RCC_PLL_MUL9;
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
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV2;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_2) != HAL_OK)
  {
    Error_Handler();
  }
  PeriphClkInit.PeriphClockSelection = RCC_PERIPHCLK_ADC;
  PeriphClkInit.AdcClockSelection = RCC_ADCPCLK2_DIV6;
  if (HAL_RCCEx_PeriphCLKConfig(&PeriphClkInit) != HAL_OK)
  {
    Error_Handler();
  }
}

/* USER CODE BEGIN 4 */

/* USER CODE END 4 */

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
