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

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include "tusb.h"
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */
// MCP23S17 SPI opcodes (hardware address 0b000)
#define MCP_WRITE_OP    0x40
#define MCP_READ_OP     0x41
// MCP23S17 registers (BANK=0 default)
#define MCP_IODIRA      0x00
#define MCP_IODIRB      0x01
#define MCP_GPPUB       0x0D
#define MCP_OLATA       0x14
#define MCP_GPIOB       0x13
#define SPI_TIMEOUT     2
// MIDI
#define MIDI_CHANNEL    0
#define MIDI_NOTE_BASE  60
/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/

COM_InitTypeDef BspCOMInit;

SPI_HandleTypeDef hspi1;

PCD_HandleTypeDef hpcd_USB_DRD_FS;

/* USER CODE BEGIN PV */
static uint8_t matrix_prev[16] = {0};
static uint8_t mcp_ready = 0;
// Debug: 0=not tested, 1=SPI OK, 0xFF=SPI FAILED
static uint8_t mcp_spi_ok = 0;
// Debug: last raw row read from MCP (visible in debugger)
volatile uint8_t dbg_last_rows = 0;
volatile uint32_t dbg_scan_count = 0;
// Debug: GPIOB value right after mcp_init (should be 0x0F if pull-ups work and no buttons pressed)
volatile uint8_t dbg_init_gpiob = 0;
// Debug: IODIRA read BEFORE init (power-on default = 0xFF). If 0x00 = MISO floating!
volatile uint8_t dbg_pre_init_iodira = 0;
// Debug: GPPUB readback after writing 0x0F (should be 0x0F if SPI writes work)
volatile uint8_t dbg_gppub_readback = 0;
/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
void PeriphCommonClock_Config(void);
static void MX_GPIO_Init(void);
static void MX_USB_PCD_Init(void);
static void MX_SPI1_Init(void);
static void MX_ICACHE_Init(void);
/* USER CODE BEGIN PFP */
void midi_note_on(uint8_t note, uint8_t velocity);
void midi_note_off(uint8_t note);
void mcp_write(uint8_t reg, uint8_t val);
uint8_t mcp_read(uint8_t reg);
void mcp_init(void);
void scan_matrix(void);
/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */
void midi_note_on(uint8_t note, uint8_t velocity)
{
  uint8_t msg[3] = { (uint8_t)(0x90 | MIDI_CHANNEL), note, velocity };
  tud_midi_stream_write(0, msg, 3);
}

void midi_note_off(uint8_t note)
{
  uint8_t msg[3] = { (uint8_t)(0x80 | MIDI_CHANNEL), note, 0 };
  tud_midi_stream_write(0, msg, 3);
}

void mcp_write(uint8_t reg, uint8_t val)
{
  uint8_t tx[3] = { MCP_WRITE_OP, reg, val };
  HAL_GPIO_WritePin(MCP_CS_GPIO_Port, MCP_CS_Pin, GPIO_PIN_RESET);
  HAL_SPI_Transmit(&hspi1, tx, 3, SPI_TIMEOUT);
  HAL_GPIO_WritePin(MCP_CS_GPIO_Port, MCP_CS_Pin, GPIO_PIN_SET);
}

uint8_t mcp_read(uint8_t reg)
{
  uint8_t tx[3] = { MCP_READ_OP, reg, 0x00 };
  uint8_t rx[3] = { 0, 0, 0 };
  HAL_GPIO_WritePin(MCP_CS_GPIO_Port, MCP_CS_Pin, GPIO_PIN_RESET);
  HAL_SPI_TransmitReceive(&hspi1, tx, rx, 3, SPI_TIMEOUT);
  HAL_GPIO_WritePin(MCP_CS_GPIO_Port, MCP_CS_Pin, GPIO_PIN_SET);
  return rx[2];
}

void mcp_init(void)
{
  // Read IODIRA BEFORE writing - power-on default is 0xFF (all inputs)
  // 0xFF -> SPI is working correctly
  // 0x00 -> MISO is floating, SPI not communicating!
  // 0xFF with pull-up fix, 0x00 without = confirms MISO pull-up was the issue
  dbg_pre_init_iodira = mcp_read(MCP_IODIRA);

  mcp_write(MCP_IODIRA, 0x00);  // PORTA = all outputs (columns)
  mcp_write(MCP_IODIRB, 0xFF);  // PORTB = all inputs  (rows)
  mcp_write(MCP_GPPUB,  0x0F);  // pull-ups on GPB0-3

  // Read back GPPUB - should be 0x0F if SPI writes are working
  dbg_gppub_readback = mcp_read(MCP_GPPUB);

  mcp_write(MCP_OLATA,  0xFF);  // all columns HIGH (idle)

  // Determine SPI status from pre-init IODIRA read
  // 0xFF = chip responded correctly (default value)
  // anything else = SPI suspect
  if (dbg_pre_init_iodira == 0xFF)
  {
    mcp_spi_ok = 1;  // SPI confirmed working
    // 5 fast blinks = MCP OK
    for (int i = 0; i < 5; i++) {
      HAL_GPIO_WritePin(GPIOB, GPIO_PIN_0, GPIO_PIN_SET);  HAL_Delay(80);
      HAL_GPIO_WritePin(GPIOB, GPIO_PIN_0, GPIO_PIN_RESET); HAL_Delay(80);
    }
  }
  else
  {
    mcp_spi_ok = 0xFF;  // SPI FAILED - check wiring/power!
    // 10 fast blinks = MCP FAILED
    for (int i = 0; i < 10; i++) {
      HAL_GPIO_WritePin(GPIOB, GPIO_PIN_0, GPIO_PIN_SET);  HAL_Delay(40);
      HAL_GPIO_WritePin(GPIOB, GPIO_PIN_0, GPIO_PIN_RESET); HAL_Delay(40);
    }
  }

  mcp_ready = 1;
}

void scan_matrix(void)
{
  // Wait until USB is mounted before sending MIDI
  if (!tud_mounted()) return;

  // Rate-limit scan to every 5 ms
  static uint32_t last_scan = 0;
  uint32_t now = HAL_GetTick();
  if (now - last_scan < 5) return;
  last_scan = now;

  // Drain any incoming MIDI packets
  uint8_t packet[4];
  while (tud_midi_available()) tud_midi_packet_read(packet);

  // Scan each column
  for (uint8_t col = 0; col < 4; col++)
  {
    // Drive this column LOW, all others HIGH
    mcp_write(MCP_OLATA, (uint8_t)(~(1u << col)) & 0x0F);

    // Short settle (~1 us at 32 MHz)
    for (int i = 0; i < 32; i++) __NOP();

    // Read row inputs (active LOW = pressed)
    uint8_t rows = mcp_read(MCP_GPIOB) & 0x0F;
    dbg_last_rows = rows;  // visible in Keil debugger watch window
    dbg_scan_count++;

    // Restore all columns HIGH
    mcp_write(MCP_OLATA, 0xFF);

    for (uint8_t row = 0; row < 4; row++)
    {
      uint8_t btn     = (uint8_t)(row * 4 + col);
      uint8_t pressed = !(rows & (1u << row));

      if (pressed && !matrix_prev[btn])
      {
        midi_note_on((uint8_t)(MIDI_NOTE_BASE + btn), 100);
        HAL_GPIO_WritePin(GPIOB, GPIO_PIN_0, GPIO_PIN_SET);
      }
      else if (!pressed && matrix_prev[btn])
      {
        midi_note_off((uint8_t)(MIDI_NOTE_BASE + btn));
        HAL_GPIO_WritePin(GPIOB, GPIO_PIN_0, GPIO_PIN_RESET);
      }
      matrix_prev[btn] = pressed;
    }
  }
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

  /* Configure the peripherals common clocks */
  PeriphCommonClock_Config();

  /* USER CODE BEGIN SysInit */
  // --- DEBUG LED on PB0 (LED_STATUS) ---
  __HAL_RCC_GPIOB_CLK_ENABLE();
  {
    GPIO_InitTypeDef gi = {0};
    gi.Pin   = GPIO_PIN_0;
    gi.Mode  = GPIO_MODE_OUTPUT_PP;
    gi.Pull  = GPIO_NOPULL;
    gi.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(GPIOB, &gi);
  }
  // 1 blink = past SystemClock_Config
  HAL_GPIO_WritePin(GPIOB, GPIO_PIN_0, GPIO_PIN_SET);
  HAL_Delay(200);
  HAL_GPIO_WritePin(GPIOB, GPIO_PIN_0, GPIO_PIN_RESET);
  HAL_Delay(200);
  /* USER CODE END SysInit */

  /* Initialize all configured peripherals */
  MX_GPIO_Init();
  MX_USB_PCD_Init();
  MX_SPI1_Init();
  MX_ICACHE_Init();
  /* USER CODE BEGIN 2 */
  // 2 blinks = past SPI init
  for (int i = 0; i < 2; i++) {
    HAL_GPIO_WritePin(GPIOB, GPIO_PIN_0, GPIO_PIN_SET);
    HAL_Delay(150);
    HAL_GPIO_WritePin(GPIOB, GPIO_PIN_0, GPIO_PIN_RESET);
    HAL_Delay(150);
  }

  // Init MCP23S17 BEFORE USB starts - safe because USB not running yet
  // LED blinks after this show if SPI works (5 blinks = OK, 10 blinks = FAILED)
  mcp_init();
  dbg_init_gpiob = mcp_read(MCP_GPIOB) & 0x0F;

  // Initialize tinyUSB
  tusb_init();

  // 3 blinks = past tusb_init
  for (int i = 0; i < 3; i++) {
    HAL_GPIO_WritePin(GPIOB, GPIO_PIN_0, GPIO_PIN_SET);
    HAL_Delay(100);
    HAL_GPIO_WritePin(GPIOB, GPIO_PIN_0, GPIO_PIN_RESET);
    HAL_Delay(100);
  }

  /* USER CODE END 2 */

  /* Initialize USER push-button, will be used to trigger an interrupt each time it's pressed.*/
  BSP_PB_Init(BUTTON_USER, BUTTON_MODE_EXTI);

  /* Initialize COM1 port (115200, 8 bits (7-bit data + 1 stop bit), no parity */
  BspCOMInit.BaudRate   = 115200;
  BspCOMInit.WordLength = COM_WORDLENGTH_8B;
  BspCOMInit.StopBits   = COM_STOPBITS_1;
  BspCOMInit.Parity     = COM_PARITY_NONE;
  BspCOMInit.HwFlowCtl  = COM_HWCONTROL_NONE;
  if (BSP_COM_Init(COM1, &BspCOMInit) != BSP_ERROR_NONE)
  {
    Error_Handler();
  }

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  while (1)
  {
    // tinyUSB device task
    tud_task();

    // Scan 4x4 matrix via MCP23S17
    scan_matrix();

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
  __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE3);

  while(!__HAL_PWR_GET_FLAG(PWR_FLAG_VOSRDY)) {}

  /** Initializes the RCC Oscillators according to the specified parameters
  * in the RCC_OscInitTypeDef structure.
  */
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSI48|RCC_OSCILLATORTYPE_HSI;
  RCC_OscInitStruct.HSIState = RCC_HSI_ON;
  RCC_OscInitStruct.HSIDiv = RCC_HSI_DIV2;
  RCC_OscInitStruct.HSICalibrationValue = RCC_HSICALIBRATION_DEFAULT;
  RCC_OscInitStruct.HSI48State = RCC_HSI48_ON;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_NONE;
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
  {
    Error_Handler();
  }

  /** Initializes the CPU, AHB and APB buses clocks
  */
  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_SYSCLK
                              |RCC_CLOCKTYPE_PCLK1|RCC_CLOCKTYPE_PCLK2
                              |RCC_CLOCKTYPE_PCLK3;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_HSI;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV1;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;
  RCC_ClkInitStruct.APB3CLKDivider = RCC_HCLK_DIV1;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_1) != HAL_OK)
  {
    Error_Handler();
  }

  /** Configure the programming delay
  */
  __HAL_FLASH_SET_PROGRAM_DELAY(FLASH_PROGRAMMING_DELAY_0);
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
  hspi1.Init.CRCPolynomial = 0x7;
  hspi1.Init.NSSPMode = SPI_NSS_PULSE_DISABLE;
  hspi1.Init.NSSPolarity = SPI_NSS_POLARITY_LOW;
  hspi1.Init.FifoThreshold = SPI_FIFO_THRESHOLD_01DATA;
  hspi1.Init.MasterSSIdleness = SPI_MASTER_SS_IDLENESS_00CYCLE;
  hspi1.Init.MasterInterDataIdleness = SPI_MASTER_INTERDATA_IDLENESS_00CYCLE;
  hspi1.Init.MasterReceiverAutoSusp = SPI_MASTER_RX_AUTOSUSP_DISABLE;
  hspi1.Init.MasterKeepIOState = SPI_MASTER_KEEP_IO_STATE_DISABLE;
  hspi1.Init.IOSwap = SPI_IO_SWAP_DISABLE;
  hspi1.Init.ReadyMasterManagement = SPI_RDY_MASTER_MANAGEMENT_INTERNALLY;
  hspi1.Init.ReadyPolarity = SPI_RDY_POLARITY_HIGH;
  if (HAL_SPI_Init(&hspi1) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN SPI1_Init 2 */

  /* USER CODE END SPI1_Init 2 */

}

/**
  * @brief USB Initialization Function
  * @param None
  * @retval None
  */
static void MX_USB_PCD_Init(void)
{

  /* USER CODE BEGIN USB_Init 0 */

  /* USER CODE END USB_Init 0 */

  /* USER CODE BEGIN USB_Init 1 */

  /* USER CODE END USB_Init 1 */
  hpcd_USB_DRD_FS.Instance = USB_DRD_FS;
  hpcd_USB_DRD_FS.Init.dev_endpoints = 8;
  hpcd_USB_DRD_FS.Init.speed = USBD_FS_SPEED;
  hpcd_USB_DRD_FS.Init.phy_itface = PCD_PHY_EMBEDDED;
  hpcd_USB_DRD_FS.Init.Sof_enable = DISABLE;
  hpcd_USB_DRD_FS.Init.low_power_enable = DISABLE;
  hpcd_USB_DRD_FS.Init.lpm_enable = DISABLE;
  hpcd_USB_DRD_FS.Init.battery_charging_enable = DISABLE;
  hpcd_USB_DRD_FS.Init.vbus_sensing_enable = DISABLE;
  hpcd_USB_DRD_FS.Init.bulk_doublebuffer_enable = DISABLE;
  hpcd_USB_DRD_FS.Init.iso_singlebuffer_enable = DISABLE;
  if (HAL_PCD_Init(&hpcd_USB_DRD_FS) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN USB_Init 2 */

  /* USER CODE END USB_Init 2 */

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
  __HAL_RCC_GPIOC_CLK_ENABLE();
  __HAL_RCC_GPIOB_CLK_ENABLE();

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(MCP_CS_GPIO_Port, MCP_CS_Pin, GPIO_PIN_SET);

  /*Configure GPIO pin : PC4 */
  GPIO_InitStruct.Pin = GPIO_PIN_4;
  GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  HAL_GPIO_Init(GPIOC, &GPIO_InitStruct);

  /*Configure GPIO pin : MCP_CS_Pin */
  GPIO_InitStruct.Pin = MCP_CS_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(MCP_CS_GPIO_Port, &GPIO_InitStruct);

  /* USER CODE BEGIN MX_GPIO_Init_2 */

  /* USER CODE END MX_GPIO_Init_2 */
}

/* USER CODE BEGIN 4 */

/* USER CODE END 4 */

/**
  * @brief Peripherals Common Clock Configuration
  * @retval None
  */
void PeriphCommonClock_Config(void)
{
  RCC_PeriphCLKInitTypeDef PeriphClkInitStruct = {0};
  PeriphClkInitStruct.PeriphClockSelection = RCC_PERIPHCLK_CKPER;
  PeriphClkInitStruct.CkperClockSelection = RCC_CLKPSOURCE_HSI;
  if (HAL_RCCEx_PeriphCLKConfig(&PeriphClkInitStruct) != HAL_OK)
  {
    Error_Handler();
  }
}

/**
  * @brief ICACHE Initialization Function
  * @retval None
  */
static void MX_ICACHE_Init(void)
{
  // ICACHE not enabled in HAL config - skipped
  (void)0;
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
