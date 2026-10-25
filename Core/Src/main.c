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

#include "main.h"
#include <stdio.h>  // snprintf() for building LCD text
#include <math.h>   // atan2f() for the tilt angle

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
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
I2C_HandleTypeDef hi2c2;
TIM_HandleTypeDef htim3;
TIM_HandleTypeDef htim4;
UART_HandleTypeDef huart2;

/* USER CODE BEGIN PV */
uint8_t rx;
volatile uint8_t  new_cmd = 0;      // set in the callback, used in the main loop
char last_cmd[12] = "READY";
uint8_t linked = 0;
int16_t ax, ay, az;
int     tilt = 0;
uint8_t mpu_ok = 0;
uint8_t who = 0;
uint32_t t_mpu = 0;
uint8_t lcd27 = 0, lcd3F = 0;

volatile uint8_t mode = 0;   // 0 = Bluetooth only, 1 = tilt
uint8_t hand_closed = 0;

/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
static void MX_GPIO_Init(void);
static void MX_TIM3_Init(void);
static void MX_TIM4_Init(void);
static void MX_USART2_UART_Init(void);
static void MX_I2C2_Init(void);
/* USER CODE BEGIN PFP */
/* USER CODE END PFP */


/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */

#define LCD_ADDR (0x27 << 1)   // I2C address of the LCD backpack (0x27 here;


static void lcd_nibble(uint8_t n)  // Send one 4-bit nibble; used only during LCD start-up
{
  uint8_t b[2] = { (uint8_t)(n | 0x0C), (uint8_t)(n | 0x08) };   // EN high, then EN low, backlight on
  HAL_I2C_Master_Transmit(&hi2c2, LCD_ADDR, b, 2, 100);
}


static void lcd_send(uint8_t data, uint8_t rs)  // Send a full byte as two nibbles. rs=0 command, rs=1 character
{
  uint8_t hi = data & 0xF0, lo = (data << 4) & 0xF0;
  uint8_t f = 0x08 | rs;
  uint8_t b[4] = { (uint8_t)(hi | f | 0x04), (uint8_t)(hi | f),
                   (uint8_t)(lo | f | 0x04), (uint8_t)(lo | f) };
  HAL_I2C_Master_Transmit(&hi2c2, LCD_ADDR, b, 4, 100);
}


void lcd_init(void)  // Initialise the 16x2 LCD in 4-bit mode (standard HD44780 sequence)
{
  HAL_Delay(50);
  lcd_nibble(0x30); HAL_Delay(5);
  lcd_nibble(0x30); HAL_Delay(1);
  lcd_nibble(0x30); HAL_Delay(1);
  lcd_nibble(0x20); HAL_Delay(1);   // switch to 4-bit mode
  lcd_send(0x28, 0);                // 2 lines, 5x8 font
  lcd_send(0x0C, 0);                // display on, cursor off
  lcd_send(0x06, 0);                // cursor moves right
  lcd_send(0x01, 0); HAL_Delay(2);  // clear
}


void lcd_cursor(uint8_t row, uint8_t col) // Move the cursor to a row (0 or 1) and column (0 to 15)
{
  lcd_send(0x80 | ((row ? 0x40 : 0x00) + col), 0);
}


void lcd_print(const char *s) // Print a string at the current cursor position
{
  while (*s) lcd_send((uint8_t)*s++, 1);
}


#define MPU_ADDR (0x68 << 1) // MPU6050 address with AD0 on GND (0x68)


uint8_t mpu_init(void) // Check the sensor is present, then wake it (it starts in sleep mode)
{
  uint8_t d = 0;
  if (HAL_I2C_IsDeviceReady(&hi2c2, MPU_ADDR, 3, 50) != HAL_OK) return 0;
  HAL_I2C_Mem_Read(&hi2c2, MPU_ADDR, 0x75, 1, &who, 1, 100);   // WHO_AM_I, should be 0x68
  HAL_I2C_Mem_Write(&hi2c2, MPU_ADDR, 0x6B, 1, &d, 1, 100);    // wake up
  return 1;
}


void mpu_read(void)   // Read the 3-axis accelerometer and compute wrist tilt in degrees
{
  uint8_t b[6];
  if (HAL_I2C_Mem_Read(&hi2c2, MPU_ADDR, 0x3B, 1, b, 6, 100) == HAL_OK)
  {
    ax = (int16_t)((b[0] << 8) | b[1]);
    ay = (int16_t)((b[2] << 8) | b[3]);
    az = (int16_t)((b[4] << 8) | b[5]);
    tilt = (int)(atan2f((float)ay, (float)az) * 57.2958f);   // -180..180 degrees
  }
}


void lcd_update(void) // Show the Bluetooth status (line 1) and last command (line 2)
{
  char buf[17];
  lcd_cursor(0, 0);
  lcd_print(linked ? "BT:OK            " : "BT:waiting       ");
  snprintf(buf, sizeof(buf), "Last: %-10s", last_cmd);
  lcd_cursor(1, 0);
  lcd_print(buf);
}

#define OPEN_ANGLE    0       // Servo angles and pulse limits (timer ticks are 1 us)
#define CLOSE_ANGLE   180
#define SERVO_COUNT   5
#define SERVO_MIN_US  500     // pulse at 0 degrees
#define SERVO_MAX_US  2500    // pulse at 180 degrees

void setFinger(uint8_t id, uint8_t angle) // Move one finger (id 0 to 4) to an angle (0 to 180 degrees)
{
  if (angle > 180) angle = 180;
  uint32_t pulse = SERVO_MIN_US +
                   ((uint32_t)angle * (SERVO_MAX_US - SERVO_MIN_US)) / 180;
  // Fingers 0 to 3 use TIM4 (PB6 to PB9); finger 4 uses TIM3 (PB4)
	switch (id) {
    case 0: __HAL_TIM_SET_COMPARE(&htim4, TIM_CHANNEL_1, pulse); break;
    case 1: __HAL_TIM_SET_COMPARE(&htim4, TIM_CHANNEL_2, pulse); break;
    case 2: __HAL_TIM_SET_COMPARE(&htim4, TIM_CHANNEL_3, pulse); break;
    case 3: __HAL_TIM_SET_COMPARE(&htim4, TIM_CHANNEL_4, pulse); break;
    case 4: __HAL_TIM_SET_COMPARE(&htim3, TIM_CHANNEL_1, pulse); break;
  }
}

void setAll(uint8_t angle)    // Move all five fingers to the same angle
{
  for (int i = 0; i < SERVO_COUNT; i++) setFinger(i, angle);
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
  MX_TIM3_Init();
  MX_TIM4_Init();
  MX_USART2_UART_Init();
  MX_I2C2_Init();
	
  /* USER CODE BEGIN 2 */
	
	// Start PWM on all five servo channels
	HAL_TIM_PWM_Start(&htim4, TIM_CHANNEL_1);
  HAL_TIM_PWM_Start(&htim4, TIM_CHANNEL_2);
  HAL_TIM_PWM_Start(&htim4, TIM_CHANNEL_3);
  HAL_TIM_PWM_Start(&htim4, TIM_CHANNEL_4);
  HAL_TIM_PWM_Start(&htim3, TIM_CHANNEL_1);
	
	
	// Start with the hand open
  setAll(OPEN_ANGLE);
	
	// Start interrupt-driven reception of one Bluetooth byte
	HAL_UART_Receive_IT(&huart2, &rx, 1);
   
	// Debug: see which LCD address responds (watch lcd27 / lcd3F)
	lcd27 = (HAL_I2C_IsDeviceReady(&hi2c2, 0x27 << 1, 3, 50) == HAL_OK);
  lcd3F = (HAL_I2C_IsDeviceReady(&hi2c2, 0x3F << 1, 3, 50) == HAL_OK);

  // Show the start-up screen for one second, then the status screen
  lcd_init();
  lcd_cursor(0, 0); lcd_print("Bionic Hand     ");
  lcd_cursor(1, 0); lcd_print("Starting...     ");
  HAL_Delay(1000);
  lcd_update();          // shows BT:waiting / Last: READY


  // Detect the MPU6050; mpu_ok stays 0 if it is missing
  mpu_ok = mpu_init();


  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  while (1)
  {
    /* USER CODE END WHILE */

    /* USER CODE BEGIN 3 */
		
	 // Every 100 ms: read the sensor, apply tilt control if enabled, update the LCD
   if (mpu_ok && HAL_GetTick() - t_mpu > 100){
   t_mpu = HAL_GetTick();
   mpu_read();

   if (mode == 1)
  {
    if (tilt > 45 && !hand_closed)      { setAll(CLOSE_ANGLE); hand_closed = 1; }
    else if (tilt < 10 && hand_closed)  { setAll(OPEN_ANGLE);  hand_closed = 0; }
  }

  char m[17];
  snprintf(m, sizeof(m), "%s%4d %s      ", mode ? "T:" : "M:", tilt, hand_closed ? "CLOSE" : "OPEN");
  lcd_cursor(1, 0);
  lcd_print(m);
  }	
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
  __HAL_RCC_PWR_CLK_ENABLE();
  __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE2);

  /** Initializes the RCC Oscillators according to the specified parameters
  * in the RCC_OscInitTypeDef structure.
  */
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSE;
  RCC_OscInitStruct.HSEState = RCC_HSE_ON;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
  RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSE;
  RCC_OscInitStruct.PLL.PLLM = 25;
  RCC_OscInitStruct.PLL.PLLN = 336;
  RCC_OscInitStruct.PLL.PLLP = RCC_PLLP_DIV4;
  RCC_OscInitStruct.PLL.PLLQ = 4;
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
  * @brief I2C2 Initialization Function
  * @param None
  * @retval None
  */
static void MX_I2C2_Init(void)
{

  /* USER CODE BEGIN I2C2_Init 0 */

  /* USER CODE END I2C2_Init 0 */

  /* USER CODE BEGIN I2C2_Init 1 */

  /* USER CODE END I2C2_Init 1 */
  hi2c2.Instance = I2C2;
  hi2c2.Init.ClockSpeed = 100000;
  hi2c2.Init.DutyCycle = I2C_DUTYCYCLE_2;
  hi2c2.Init.OwnAddress1 = 0;
  hi2c2.Init.AddressingMode = I2C_ADDRESSINGMODE_7BIT;
  hi2c2.Init.DualAddressMode = I2C_DUALADDRESS_DISABLE;
  hi2c2.Init.OwnAddress2 = 0;
  hi2c2.Init.GeneralCallMode = I2C_GENERALCALL_DISABLE;
  hi2c2.Init.NoStretchMode = I2C_NOSTRETCH_DISABLE;
  if (HAL_I2C_Init(&hi2c2) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN I2C2_Init 2 */
  /* USER CODE END I2C2_Init 2 */

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
  htim3.Init.Prescaler = 83;
  htim3.Init.CounterMode = TIM_COUNTERMODE_UP;
  htim3.Init.Period = 19999;
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
  sConfigOC.Pulse = 1500;
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
  * @brief TIM4 Initialization Function
  * @param None
  * @retval None
  */
static void MX_TIM4_Init(void)
{

  /* USER CODE BEGIN TIM4_Init 0 */
  /* USER CODE END TIM4_Init 0 */

  TIM_MasterConfigTypeDef sMasterConfig = {0};
  TIM_OC_InitTypeDef sConfigOC = {0};

  /* USER CODE BEGIN TIM4_Init 1 */
  /* USER CODE END TIM4_Init 1 */
  htim4.Instance = TIM4;
  htim4.Init.Prescaler = 83;
  htim4.Init.CounterMode = TIM_COUNTERMODE_UP;
  htim4.Init.Period = 19999;
  htim4.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
  htim4.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_DISABLE;
  if (HAL_TIM_PWM_Init(&htim4) != HAL_OK)
  {
    Error_Handler();
  }
  sMasterConfig.MasterOutputTrigger = TIM_TRGO_RESET;
  sMasterConfig.MasterSlaveMode = TIM_MASTERSLAVEMODE_DISABLE;
  if (HAL_TIMEx_MasterConfigSynchronization(&htim4, &sMasterConfig) != HAL_OK)
  {
    Error_Handler();
  }
  sConfigOC.OCMode = TIM_OCMODE_PWM1;
  sConfigOC.Pulse = 1500;
  sConfigOC.OCPolarity = TIM_OCPOLARITY_HIGH;
  sConfigOC.OCFastMode = TIM_OCFAST_DISABLE;
  if (HAL_TIM_PWM_ConfigChannel(&htim4, &sConfigOC, TIM_CHANNEL_1) != HAL_OK)
  {
    Error_Handler();
  }
  if (HAL_TIM_PWM_ConfigChannel(&htim4, &sConfigOC, TIM_CHANNEL_2) != HAL_OK)
  {
    Error_Handler();
  }
  if (HAL_TIM_PWM_ConfigChannel(&htim4, &sConfigOC, TIM_CHANNEL_3) != HAL_OK)
  {
    Error_Handler();
  }
  if (HAL_TIM_PWM_ConfigChannel(&htim4, &sConfigOC, TIM_CHANNEL_4) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN TIM4_Init 2 */

  /* USER CODE END TIM4_Init 2 */
  HAL_TIM_MspPostInit(&htim4);

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
  huart2.Init.BaudRate = 9600;
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
  /* USER CODE BEGIN MX_GPIO_Init_1 */
  /* USER CODE END MX_GPIO_Init_1 */

  /* GPIO Ports Clock Enable */
  __HAL_RCC_GPIOH_CLK_ENABLE();
  __HAL_RCC_GPIOA_CLK_ENABLE();
  __HAL_RCC_GPIOB_CLK_ENABLE();

  /*Configure GPIO pin : PA4 */
  GPIO_InitStruct.Pin = GPIO_PIN_4;
  GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
  GPIO_InitStruct.Pull = GPIO_PULLUP;
  HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

  /* USER CODE BEGIN MX_GPIO_Init_2 */
  /* USER CODE END MX_GPIO_Init_2 */
}

/* USER CODE BEGIN 4 */


void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart)
{
  if (huart->Instance == USART2)
  {
		linked = 1;
    new_cmd = 1;
    if      (rx == 'C') snprintf(last_cmd, sizeof(last_cmd), "CLOSE");
    else if (rx == 'O') snprintf(last_cmd, sizeof(last_cmd), "OPEN");
    else if (rx >= '1' && rx <= '5') snprintf(last_cmd, sizeof(last_cmd), "FINGER %c", rx);
    else                snprintf(last_cmd, sizeof(last_cmd), "%c ?", rx);
		
    if (rx == 'C') setAll(CLOSE_ANGLE);
    if (rx == 'O') setAll(OPEN_ANGLE);

    if (rx == '1') setFinger(0, CLOSE_ANGLE);
    if (rx == '2') setFinger(1, CLOSE_ANGLE);
    if (rx == '3') setFinger(2, CLOSE_ANGLE);
    if (rx == '4') setFinger(3, CLOSE_ANGLE);
    if (rx == '5') setFinger(4, CLOSE_ANGLE);
		
		if (rx == 'T') { mode = 1; snprintf(last_cmd, sizeof(last_cmd), "TILT MODE"); }
    if (rx == 'M') { mode = 0; snprintf(last_cmd, sizeof(last_cmd), "MANUAL"); }

		
		// Echo the byte back so the phone app can confirm it.
    HAL_UART_Transmit(&huart2, &rx, 1, 10);   // echo
    HAL_UART_Receive_IT(&huart2, &rx, 1);
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
