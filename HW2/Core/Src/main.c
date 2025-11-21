
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
#include "mouse_image.h"
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */
// mouse_image.h'den gelen tanımları kullan
#define IMG_W MOUSE_IMG_WIDTH
#define IMG_H MOUSE_IMG_HEIGHT
#define IMG mouse_image
#define IMG_PIXELS (IMG_W * IMG_H)

// Belleği rahat görüp debug etmek için sadece ilk OUT_N pikseli RAM'de saklıyoruz
#define OUT_N   1024
#define OFFSET  0
/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/
UART_HandleTypeDef huart2;

/* USER CODE BEGIN PV */
// Orijinal görüntüden ilk 20 piksel (kontrol için)
volatile uint8_t first_pixels[20];

// Q1/Q2: Histogramlar
volatile uint32_t hist_original[256];
volatile uint32_t hist_equalized[256];

// Q2: Eşitlenmiş görüntünün ilk OUT_N pikseli
volatile uint8_t img_eq[OUT_N];

// Q3: Low-pass ve high-pass filtre sonuçlarının ilk OUT_N pikseli
volatile uint8_t img_lp[OUT_N];
volatile uint8_t img_hp[OUT_N];

// Q4: Median filtre sonucunun ilk OUT_N pikseli
volatile uint8_t img_med[OUT_N];
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

static void uart_print_first_pixels(void) {
  char msg[32];
  for (int i = 0; i < 20; ++i) {
    int len = snprintf(msg, sizeof(msg), "IMG[%d]=%u\r\n", i, first_pixels[i]);
    if (len > 0) {
      HAL_UART_Transmit(&huart2, (uint8_t*)msg, (uint16_t)len, HAL_MAX_DELAY);
    }
  }
}

// Q1: Histogram hesaplama
static void compute_histogram(const uint8_t* img, int w, int h, uint32_t hist[256]) {
  for (int i = 0; i < 256; ++i) hist[i] = 0;

  for (int y = 0; y < h; ++y) {
    for (int x = 0; x < w; ++x) {
      uint8_t p = img[y * w + x];
      hist[p]++;
    }
  }
}

// Histogramdan LUT oluştur
static void build_equalization_lut(const uint32_t hist[256], int total,
                                   uint8_t lut[256]) {
  uint32_t cumulative = 0;
  uint32_t cdf_min = 0;
  uint32_t total_u = (uint32_t)total;

  for (int i = 0; i < 256; ++i) {
    cumulative += hist[i];
    if (cdf_min == 0 && cumulative != 0) {
      cdf_min = cumulative;
    }

    uint32_t v = 0;
    uint32_t denom = total_u - cdf_min;
    if (denom != 0U) {
      v = (cumulative - cdf_min) * 255u / denom;
    }
    if (v > 255u) v = 255u;
    lut[i] = (uint8_t)v;
  }
}

// Tüm işlemleri tek fonksiyonda çalıştır
static inline void store_window_value(size_t linear_idx, size_t offset,
                                      size_t safeN, volatile uint8_t* buffer,
                                      uint8_t value) {
  if (linear_idx < offset) {
    return;
  }
  size_t pos = linear_idx - offset;
  if (pos < safeN) {
    buffer[pos] = value;
  }
}

static void run_image_ops(void) {
  // OFFSET + OUT_N, toplam piksel sayısını aşmasın
  size_t safeN = OUT_N;
  if ((size_t)OFFSET + (size_t)OUT_N > (size_t)IMG_PIXELS) {
    safeN = (size_t)IMG_PIXELS - (size_t)OFFSET;
  }

  // Orijinal görüntünün ilk 20 pikselini kopyala (Memory Window için)
  for (int i = 0; i < 20; ++i) {
    first_pixels[i] = IMG[i];
  }
  uart_print_first_pixels();

  // --- Q1: Orijinal histogram ---
  compute_histogram(IMG, IMG_W, IMG_H, (uint32_t*)hist_original);

  // --- Q2..Q4 işlemleri için hazırlık ---
  uint8_t lut[256];
  build_equalization_lut((uint32_t*)hist_original, IMG_PIXELS, lut);

  uint32_t hist_eq_local[256];
  for (int i = 0; i < 256; ++i) {
    hist_eq_local[i] = 0;
    hist_equalized[i] = 0;
  }

  for (size_t i = 0; i < safeN; ++i) {
    img_eq[i] = 0;
    img_lp[i] = 0;
    img_hp[i] = 0;
    img_med[i] = 0;
  }

  uint8_t row_buf[3][IMG_W];
  size_t linear_idx = 0;

  for (int y = 0; y < IMG_H; ++y) {
    uint8_t* row = row_buf[y % 3];
    for (int x = 0; x < IMG_W; ++x) {
      uint8_t eq_val = lut[IMG[y * IMG_W + x]];
      row[x] = eq_val;
      hist_eq_local[eq_val]++;
      store_window_value(linear_idx, OFFSET, safeN, img_eq, eq_val);
      linear_idx++;
    }

    if (y >= 2) {
      uint8_t* row_top = row_buf[(y - 2) % 3];
      uint8_t* row_mid = row_buf[(y - 1) % 3];
      uint8_t* row_bot = row_buf[y % 3];
      size_t row_base = (size_t)(y - 1) * IMG_W;

      for (int x = 1; x < IMG_W - 1; ++x) {
        int sum_lp =
            row_top[x - 1] + row_top[x] + row_top[x + 1] +
            row_mid[x - 1] + row_mid[x] + row_mid[x + 1] +
            row_bot[x - 1] + row_bot[x] + row_bot[x + 1];
        uint8_t lp_val = (uint8_t)(sum_lp / 9);

        int hp_sum = 0;
        hp_sum += -row_mid[x - 1];
        hp_sum += -row_mid[x + 1];
        hp_sum += -row_top[x];
        hp_sum += -row_bot[x];
        hp_sum += row_mid[x] * 4;
        uint8_t hp_val = clamp_u8(hp_sum);

        uint8_t window[9] = {
            row_top[x - 1], row_top[x], row_top[x + 1],
            row_mid[x - 1], row_mid[x], row_mid[x + 1],
            row_bot[x - 1], row_bot[x], row_bot[x + 1],
        };
        for (int i = 0; i < 9; ++i) {
          for (int j = i + 1; j < 9; ++j) {
            if (window[j] < window[i]) {
              uint8_t tmp = window[i];
              window[i] = window[j];
              window[j] = tmp;
            }
          }
        }
        uint8_t med_val = window[4];

        size_t idx = row_base + (size_t)x;
        store_window_value(idx, OFFSET, safeN, img_lp, lp_val);
        store_window_value(idx, OFFSET, safeN, img_hp, hp_val);
        store_window_value(idx, OFFSET, safeN, img_med, med_val);
      }
    }
  }

  for (int i = 0; i < 256; ++i) {
    hist_equalized[i] = hist_eq_local[i];
  }

  // Burada UART ile değerleri de yazdırmak istersen, HAL_UART_Transmit ile
  // histograma ait birkaç örnek gönderebilirsin.
}

/* USER CODE END 0 */

/**
  * @brief  The application entry point.
  * @retval int
  */
int main(void)
{
  /* MCU Configuration--------------------------------------------------------*/

  /* Reset of all peripherals, Initializes the Flash interface and the Systick. */
  HAL_Init();

  /* Configure the system clock */
  SystemClock_Config();

  /* Initialize all configured peripherals */
  MX_GPIO_Init();
  MX_USART2_UART_Init();
  /* USER CODE BEGIN 2 */
  // Ödevde istenen tüm görüntü işlemleri burada hesaplanıyor
  run_image_ops();
  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  while (1)
  {
    /* USER CODE END WHILE */

    /* USER CODE BEGIN 3 */
    // Breakpoint koyup first_pixels, hist_*, img_eq, img_lp, img_hp, img_med
    // buffer'larını Memory Window'dan inceleyebilirsin.
    HAL_Delay(1000);
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

  /** Initializes the RCC Oscillators according to the specified parameters
  * in the RCC_OscInitTypeDef structure.
  */
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
  /* USER CODE BEGIN Error_Handler_Debug */
  /* User can add his own implementation to report the HAL error return state */
  __disable_irq();
  while (1)
  {
  }
  /* USER CODE END Error_Handler_Debug */
}

#ifdef  USE_FULL_ASSERT
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
     ex: printf("Wrong parameters value: file %s on line %d\r\n", file, line); */
  (void)file;
  (void)line;
  /* USER CODE END 6 */
}
#endif /* USE_FULL_ASSERT */
