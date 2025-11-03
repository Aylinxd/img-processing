/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.c
  * @brief          : Main program body
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2025 STMicroelectronics.
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

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include "image_data.h"
#include <math.h>
#include <stddef.h>
#include <stdint.h>
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */
#define IMG_PIXELS (IMG_W * IMG_H)

// RAM'i şişirmeden Memory Window'da görmek için örnek boyut:
#define OUT_N   1024   // ilk 1024 pikseli işle
// Farklı bir bölgeden başlamak istersen offset ver:
#define OFFSET  0      // örn. 512*100 ile 101. satır başı
/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/
UART_HandleTypeDef huart2;

/* USER CODE BEGIN PV */
// Memory Window’da net görmek için volatile:
volatile uint8_t first_pixels[20];     // orijinalden ilk 20 piksel

volatile uint8_t img_neg[OUT_N];       // a) Negative
volatile uint8_t img_th[OUT_N];        // b) Threshold
volatile uint8_t img_gam3[OUT_N];      // c1) Gamma = 3
volatile uint8_t img_gam13[OUT_N];     // c2) Gamma = 1/3
volatile uint8_t img_pw[OUT_N];        // d) Piecewise linear
/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
static void MX_GPIO_Init(void);
static void MX_USART2_UART_Init(void);
/* USER CODE BEGIN PFP */

/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */

static inline uint8_t clamp_u8(int x) {
  if (x < 0)   return 0;
  if (x > 255) return 255;
  return (uint8_t)x;
}

// a) Negative
static void image_negative(const uint8_t* src, uint8_t* dst, size_t n) {
  for (size_t i = 0; i < n; ++i) dst[i] = (uint8_t)(255 - src[i]);
}

// b) Threshold
static void image_threshold(const uint8_t* src, uint8_t* dst, size_t n,
                            uint8_t T, uint8_t lowVal, uint8_t highVal) {
  for (size_t i = 0; i < n; ++i) dst[i] = (src[i] >= T) ? highVal : lowVal;
}

// c) Gamma LUT
static void build_gamma_lut(float gamma, uint8_t lut[256]) {
  for (int v = 0; v < 256; ++v) {
    float x = (float)v / 255.0f;
    float y = powf(x, gamma);
    int   u = (int)lroundf(y * 255.0f);
    lut[v] = (uint8_t)((u < 0) ? 0 : (u > 255 ? 255 : u));
  }
}

static void image_gamma_lut(const uint8_t* src, uint8_t* dst, size_t n,
                            const uint8_t lut[256]) {
  for (size_t i = 0; i < n; ++i) dst[i] = lut[src[i]];
}

// d) Piecewise linear (T altı/üstü ayrı doğrular)
static void image_piecewise_linear(const uint8_t* src, uint8_t* dst, size_t n,
                                   uint8_t T, uint8_t lowMax, uint8_t highMin) {
  float k1 = (T == 0)   ? 0.0f : ((float)lowMax) / (float)T;
  float k2 = (T == 255) ? 0.0f : ((255.0f - (float)highMin) / (255.0f - (float)T));
  for (size_t i = 0; i < n; ++i) {
    uint8_t x = src[i];
    int y = (x <= T)
          ? (int)lroundf(k1 * (float)x)
          : (int)lroundf((float)highMin + k2 * (float)(x - T));
    dst[i] = clamp_u8(y);
  }
}

// Tek yerden çalıştırma fonksiyonu (OUT_N kadar)
static void run_image_ops(void) {
  // Güvenlik: OFFSET + OUT_N, toplam pikselleri aşmasın
  size_t safeN = OUT_N;
  if ((size_t)OFFSET + (size_t)OUT_N > (size_t)IMG_PIXELS) {
    safeN = (size_t)IMG_PIXELS - (size_t)OFFSET;
  }

  // Orijinal piksel örneği (Memory Window doğrulama için)
  for (int i = 0; i < 20; i++) first_pixels[i] = IMG[i];

  // Kaynaktan işlem yapılacak dilim
  const uint8_t* SRC = &IMG[OFFSET];

  // a) Negative
  image_negative(SRC, (uint8_t*)img_neg, safeN);

  // b) Threshold
  const uint8_t T = 128;                // eşiği istersen değiştir
  image_threshold(SRC, (uint8_t*)img_th, safeN, T, 0, 255);

  // c) Gamma (γ=3 ve γ=1/3) LUT ile
  uint8_t lut_g3[256], lut_g13[256];
  build_gamma_lut(3.0f,      lut_g3);
  build_gamma_lut(1.0f/3.0f, lut_g13);
  image_gamma_lut(SRC, (uint8_t*)img_gam3,  safeN, lut_g3);
  image_gamma_lut(SRC, (uint8_t*)img_gam13, safeN, lut_g13);

  // d) Piecewise linear (b’deki T ile tutarlı)
  // Örnek parametreler: alt aralık [0..T] -> [0..100], üst aralık [T..255] -> [180..255]
  image_piecewise_linear(SRC, (uint8_t*)img_pw, safeN, T, 100, 180);
}
/* USER CODE END 0 */

/**
  * @brief  The application entry point.
  * @retval int
  */
int main(void)
{
  /* MCU Configuration--------------------------------------------------------*/
  HAL_Init();
  SystemClock_Config();

  /* Initialize all configured peripherals */
  MX_GPIO_Init();
  MX_USART2_UART_Init();

  /* USER CODE BEGIN 2 */
  // Tüm dönüşümleri hesapla → Memory Window’dan bakacaksın
  run_image_ops();
  /* USER CODE END 2 */

  /* Infinite loop */
  while (1)
  {
    // İstersen burada breakpoint koyup buffer’ları izleyebilirsin.
    HAL_Delay(1000);
  }
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
  RCC_OscInitStruct.HSICalibrationValue = RCC_HSICALIBRATION_DEFAULT;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
  RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSI;
  RCC_OscInitStruct.PLL.PLLM = 16;
  RCC_OscInitStruct.PLL.PLLN = 336;
  RCC_OscInitStruct.PLL.PLLP = RCC_PLLP_DIV4;
  RCC_OscInitStruct.PLL.PLLQ = 2;
  RCC_OscInitStruct.PLL.PLLR = 2;
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK) {
    Error_Handler();
  }

  /** Initializes the CPU, AHB and APB buses clocks */
  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_SYSCLK
                              |RCC_CLOCKTYPE_PCLK1|RCC_CLOCKTYPE_PCLK2;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV2;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_2) != HAL_OK) {
    Error_Handler();
  }
}

/**
  * @brief USART2 Initialization Function
  * @param None
  * @retval None
  */
static void MX_USART2_UART_Init(void)
{
  huart2.Instance = USART2;
  huart2.Init.BaudRate = 115200;
  huart2.Init.WordLength = UART_WORDLENGTH_8B;
  huart2.Init.StopBits = UART_STOPBITS_1;
  huart2.Init.Parity = UART_PARITY_NONE;
  huart2.Init.Mode = UART_MODE_TX_RX;
  huart2.Init.HwFlowCtl = UART_HWCONTROL_NONE;
  huart2.Init.OverSampling = UART_OVERSAMPLING_16;
  if (HAL_UART_Init(&huart2) != HAL_OK) {
    Error_Handler();
  }
}

/**
  * @brief GPIO Initialization Function
  * @param None
  * @retval None
  */
static void MX_GPIO_Init(void)
{
  GPIO_InitTypeDef GPIO_InitStruct = {0};

  /* GPIO Ports Clock Enable */
  __HAL_RCC_GPIOC_CLK_ENABLE();
  __HAL_RCC_GPIOH_CLK_ENABLE();
  __HAL_RCC_GPIOA_CLK_ENABLE();
  __HAL_RCC_GPIOB_CLK_ENABLE();

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(LD2_GPIO_Port, LD2_Pin, GPIO_PIN_RESET);

  /*Configure GPIO pin : B1_Pin */
  GPIO_InitStruct.Pin = B1_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_IT_FALLING;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  HAL_GPIO_Init(B1_GPIO_Port, &GPIO_InitStruct);

  /*Configure GPIO pin : LD2_Pin */
  GPIO_InitStruct.Pin = LD2_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(LD2_GPIO_Port, &GPIO_InitStruct);
}

/**
  * @brief  This function is executed in case of error occurrence.
  * @retval None
  */
void Error_Handler(void)
{
  __disable_irq();
  while (1) { }
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
  /* User can add his own implementation to report the file name and line number,
     ex: printf("Wrong parameters value: file %s on line %d\r\n") */
}
#endif /* USE_FULL_ASSERT */
