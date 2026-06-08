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

// ADC Potentiometers (Fase 2 & Uitbreiding)
#define NUM_POTS       8    // <-- Aangepast van 2 naar 8
#define MIDI_CC_START  16   // Eerste CC nummer (dit wordt CC16 t/m CC23)
#define HYSTERESIS     4    // Jitter filter
#define DEADZONE_LOW   20   // Nog forser verhoogd: alles hieronder is gegarandeerd 0
#define DEADZONE_HIGH  240  // Ruwe waarde (0-255) erboven wordt 255
/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/

COM_InitTypeDef BspCOMInit;
ADC_HandleTypeDef hadc1;
DMA_NodeTypeDef Node_GPDMA1_Channel0;
DMA_QListTypeDef List_GPDMA1_Channel0;
DMA_HandleTypeDef handle_GPDMA1_Channel0;

SPI_HandleTypeDef hspi1;

TIM_HandleTypeDef htim3;
TIM_HandleTypeDef htim6;
DMA_HandleTypeDef handle_GPDMA1_Channel1;

PCD_HandleTypeDef hpcd_USB_DRD_FS;

/* USER CODE BEGIN PV */
/* Previous button state for each of the 16 matrix keys (used for edge detection) */
static uint8_t matrix_prev[16] = {0};

/* ADC Buffer - automatically updated by DMA */
volatile uint32_t adc_buffer[16]; // Extra groot en 32-bit gemaakt om RAM overflow & random MIDI fouten the voorkomen!
uint8_t last_midi_value[NUM_POTS] = {0};

/* WS2812 / SK6812 LED variables */
#define MAX_LED 16
#define USE_BRIGHTNESS 0 

uint8_t LED_Data[MAX_LED][4];
uint8_t LED_Mod[MAX_LED][4];

uint16_t pwmData[(24*MAX_LED)+50];
volatile int datasentflag = 1;
/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
void PeriphCommonClock_Config(void);
static void MX_GPIO_Init(void);
static void MX_GPDMA1_Init(void);
static void MX_SPI1_Init(void);
static void MX_ADC1_Init(void);
static void MX_TIM6_Init(void);
static void MX_USB_PCD_Init(void);
static void MX_TIM3_Init(void);
/* USER CODE BEGIN PFP */
void midi_note_on(uint8_t note, uint8_t velocity);
void midi_note_off(uint8_t note);
void mcp_write(uint8_t reg, uint8_t val);
uint8_t mcp_read(uint8_t reg);
void mcp_init(void);
void scan_matrix(void);

void ADC_Start(void);
void process_potentiometers(void);

void Set_LED(int LEDnum, int Red, int Green, int Blue);
void WS2812_Send(void);
/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */

void ADC_Start(void) {
    HAL_TIM_Base_Start(&htim6);
    /* OPMERKING: GPDMA streamt naar adc_buffer. Cast is uint32_t omdat de HAL functie dit vereist. 
       Maar onze buffer was 8-bit, terwijl DMA misschien HalfWord is in CubeMX?
       Als dit fout gaat, controleer DMA breedte voor ADC! */
    HAL_ADC_Start_DMA(&hadc1, (uint32_t*)adc_buffer, NUM_POTS);
}

void process_potentiometers(void) {
    for (int i = 0; i < NUM_POTS; i++) {
        uint16_t raw_val = adc_buffer[i] & 0xFFFF; // Mask alles boven 16-bit af voor de zekerheid

        // Pas deadzones en mapping toe om te zorgen dat elke slider altijd perfect van 0 tot 255 gaat
        if (raw_val <= DEADZONE_LOW) {
            raw_val = 0;
        } else if (raw_val >= DEADZONE_HIGH) {
            raw_val = 255;
        } else {
            raw_val = ((raw_val - DEADZONE_LOW) * 255) / (DEADZONE_HIGH - DEADZONE_LOW);
        }

        uint8_t new_value = raw_val >> 1; // 8-bit naar 7-bit (0 tot 127)

        // Als we in the dode zones zitten forceer update om zeker op 0 te vallen initialisatie.
        int16_t diff = (int16_t)new_value - (int16_t)last_midi_value[i];        
        if (diff < 0) diff = -diff;

        // Als we verschil detecteren *OF* de potmeter staat effectief en stabiel op 0 en had hier nog niet naartoe geupdate
        if (diff >= HYSTERESIS || (new_value == 0 && last_midi_value[i] != 0)) {
            
            // kleine safety check: door de hysteresis kon een waarde "1" of "2" genegeerd zijn in vorige stap
            if (new_value != last_midi_value[i]){
                if (tud_midi_mounted()) {
                    uint8_t msg[3] = { 0xB0, MIDI_CC_START + i, new_value };        
                    tud_midi_stream_write(0, msg, 3);
                }
                last_midi_value[i] = new_value;
            }
        }
    }
}

/* Set LED color values (Red, Green, Blue from 0-255) */
void Set_LED(int LEDnum, int Red, int Green, int Blue)
{
    LED_Data[LEDnum][0] = LEDnum;
    LED_Data[LEDnum][1] = Green; // SK6812 is GRB or RGB, depending on exactly which variant, GRB is most common
    LED_Data[LEDnum][2] = Red;
    LED_Data[LEDnum][3] = Blue;
}

/* Generate PWM data and start DMA transfer */
void WS2812_Send(void)
{
    uint32_t indx = 0;
    uint32_t color;

    for (int i = 0; i < MAX_LED; i++)
    {
#if USE_BRIGHTNESS
        color = ((LED_Mod[i][1] << 16) | (LED_Mod[i][2] << 8) | (LED_Mod[i][3]));
#else
        color = ((LED_Data[i][1] << 16) | (LED_Data[i][2] << 8) | (LED_Data[i][3]));
#endif
        for (int j = 23; j >= 0; j--)
        {
            // Omdat ARR 312 is:
            // "1" = 2/3 * 312 ≈ 208
            // "0" = 1/3 * 312 ≈ 104
            if (color & (1 << j)) {
                pwmData[indx] = 208;  
            } else {
                pwmData[indx] = 104;  
            }
            indx++;
        }
    }

    // Voeg de RESET code (minimaal 50us low = 50 0'en bij 800kHz is safe) toe
    for (int i = 0; i < 50; i++)
    {
        pwmData[indx] = 0;
        indx++;
    }

    // Start de DMA, zet de flag dicht totdat de PulseFinished callback hem weer open zet
    datasentflag = 0;
    HAL_TIM_PWM_Start_DMA(&htim3, TIM_CHANNEL_1, (uint32_t *)pwmData, indx);
    
    // Optioneel: wachten tot de data verstuurd is (blokkerend, maar handig voor de eerste test)
    while (!datasentflag){}
}

/* Send a MIDI Note On message on the configured channel */
void midi_note_on(uint8_t note, uint8_t velocity)
{
  uint8_t msg[3] = { (uint8_t)(0x90 | MIDI_CHANNEL), note, velocity };
  tud_midi_stream_write(0, msg, 3);
}

/* Send a MIDI Note Off message on the configured channel */
void midi_note_off(uint8_t note)
{
  uint8_t msg[3] = { (uint8_t)(0x80 | MIDI_CHANNEL), note, 0 };
  tud_midi_stream_write(0, msg, 3);
}

/* Write a single register on the MCP23S17 via SPI */
void mcp_write(uint8_t reg, uint8_t val)
{
  uint8_t tx[3] = { MCP_WRITE_OP, reg, val };
  HAL_GPIO_WritePin(MCP_CS_GPIO_Port, MCP_CS_Pin, GPIO_PIN_RESET);
  HAL_SPI_Transmit(&hspi1, tx, 3, SPI_TIMEOUT);
  HAL_GPIO_WritePin(MCP_CS_GPIO_Port, MCP_CS_Pin, GPIO_PIN_SET);
}

/* Read a single register from the MCP23S17 via SPI */
uint8_t mcp_read(uint8_t reg)
{
  uint8_t tx[3] = { MCP_READ_OP, reg, 0x00 };
  uint8_t rx[3] = { 0, 0, 0 };
  HAL_GPIO_WritePin(MCP_CS_GPIO_Port, MCP_CS_Pin, GPIO_PIN_RESET);
  HAL_SPI_TransmitReceive(&hspi1, tx, rx, 3, SPI_TIMEOUT);
  HAL_GPIO_WritePin(MCP_CS_GPIO_Port, MCP_CS_Pin, GPIO_PIN_SET);
  return rx[2];
}

/* Configure the MCP23S17: PORTA as outputs (columns), PORTB as inputs with pull-ups (rows) */
void mcp_init(void)
{
  mcp_write(MCP_IODIRA, 0x00);  // PORTA = all outputs (columns)
  mcp_write(MCP_IODIRB, 0xFF);  // PORTB = all inputs (rows)
  mcp_write(MCP_GPPUB,  0x0F);  // enable pull-ups on GPB0-3
  mcp_write(MCP_OLATA,  0xFF);  // all columns HIGH (idle state)
}

/* Scan the 4x4 key matrix and send MIDI Note On/Off messages on state changes */
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

  // -- NIEUW: als we wachten op DMA of de LED is aan het blokkeren, scan dan nog NIET om MIDI misfits te vermijden --
  if (datasentflag == 0) return;

  // Scan each column
  for (uint8_t col = 0; col < 4; col++)
  {
    // Drive this column LOW, all others HIGH
    mcp_write(MCP_OLATA, (uint8_t)(~(1u << col)) & 0x0F);

    // Short settle (~1 us at 32 MHz)
    for (int i = 0; i < 32; i++) __NOP();

    // Read row inputs (active LOW = pressed)
    uint8_t rows = mcp_read(MCP_GPIOB) & 0x0F;

    // Restore all columns HIGH
    mcp_write(MCP_OLATA, 0xFF);

    // --- DE-BOUNCE: als we bezig zijn met LED updaten, skip MIDI logic even ---
    if (datasentflag == 0) return;

    for (uint8_t row = 0; row < 4; row++)
    {
      uint8_t btn     = (uint8_t)(row * 4 + col);
      uint8_t pressed = !(rows & (1u << row));

      if (pressed && !matrix_prev[btn])
      {
        midi_note_on((uint8_t)(MIDI_NOTE_BASE + btn), 100);
        HAL_GPIO_WritePin(GPIOB, GPIO_PIN_0, GPIO_PIN_SET);
        
        // --- EXTRA: Laat de bijbehorende LED oplichten afhankelijk van welke knop is ingedrukt ---
        Set_LED(btn, 10 + (btn * 10), 0, 50); // R en B variëren
        WS2812_Send();
      }
      else if (!pressed && matrix_prev[btn])
      {
        midi_note_off((uint8_t)(MIDI_NOTE_BASE + btn));
        HAL_GPIO_WritePin(GPIOB, GPIO_PIN_0, GPIO_PIN_RESET);
        
        // --- EXTRA: Zet de bijbehorende LED weer uit nadat knop wordt losgelaten ---
        Set_LED(btn, 0, 0, 0);
        WS2812_Send();
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
  /* PB4 is by default NJTRST (JTAG) and has a pull-up resistor causing the SK6812 to glitch green!
     Force PB4 to a standard output LOW instantly to kill the floating/pullup voltage.
     Later MX_TIM3_Init will cleanly override this to Alternate Function PWM. */
  __HAL_RCC_GPIOB_CLK_ENABLE();
  {
    GPIO_InitTypeDef pb4 = {0};
    pb4.Pin   = GPIO_PIN_4;
    pb4.Mode  = GPIO_MODE_OUTPUT_PP;
    pb4.Pull  = GPIO_NOPULL;
    pb4.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(GPIOB, &pb4);
    HAL_GPIO_WritePin(GPIOB, GPIO_PIN_4, GPIO_PIN_RESET);
  }

  /* Initialize LED_STATUS (PB0) - LED turns on when a button is pressed */
  {
    GPIO_InitTypeDef gi = {0};
    gi.Pin   = GPIO_PIN_0;
    gi.Mode  = GPIO_MODE_OUTPUT_PP;
    gi.Pull  = GPIO_NOPULL;
    gi.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(GPIOB, &gi);
  }
  /* USER CODE END SysInit */

  /* Initialize all configured peripherals */
  MX_GPIO_Init();
  MX_GPDMA1_Init();
  MX_SPI1_Init();
  MX_ADC1_Init();
  MX_TIM6_Init();
  MX_USB_PCD_Init();
  MX_TIM3_Init();
  /* USER CODE BEGIN 2 */
  /* Initialize MCP23S17 I/O expander before USB starts */
  mcp_init();

  /* Start the tinyUSB device stack */
  tusb_init();

  /* Start ADC and Timer for Potentiometer */
  ADC_Start();

  /* Geef spanningen (5V USB rail) rustig de tijd om de chip 100% operationeel te maken */
  HAL_Delay(500);

  /* Forceer UIT meerdere keren voor de zekerheid */
  for(int k=0; k<MAX_LED; k++) { Set_LED(k, 0, 0, 0); }
  for(int i=0; i<3; i++) {
      WS2812_Send();
      HAL_Delay(5);
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

    // Process potentiometer values for MIDI CC
    process_potentiometers();

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
  __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE0);

  while(!__HAL_PWR_GET_FLAG(PWR_FLAG_VOSRDY)) {}

  /** Initializes the RCC Oscillators according to the specified parameters
  * in the RCC_OscInitTypeDef structure.
  */
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSI48|RCC_OSCILLATORTYPE_HSI
                              |RCC_OSCILLATORTYPE_CSI;
  RCC_OscInitStruct.HSIState = RCC_HSI_ON;
  RCC_OscInitStruct.HSIDiv = RCC_HSI_DIV2;
  RCC_OscInitStruct.HSICalibrationValue = RCC_HSICALIBRATION_DEFAULT;
  RCC_OscInitStruct.HSI48State = RCC_HSI48_ON;
  RCC_OscInitStruct.CSIState = RCC_CSI_ON;
  RCC_OscInitStruct.CSICalibrationValue = RCC_CSICALIBRATION_DEFAULT;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
  RCC_OscInitStruct.PLL.PLLSource = RCC_PLL1_SOURCE_CSI;
  RCC_OscInitStruct.PLL.PLLM = 1;
  RCC_OscInitStruct.PLL.PLLN = 125;
  RCC_OscInitStruct.PLL.PLLP = 2;
  RCC_OscInitStruct.PLL.PLLQ = 2;
  RCC_OscInitStruct.PLL.PLLR = 2;
  RCC_OscInitStruct.PLL.PLLRGE = RCC_PLL1_VCIRANGE_2;
  RCC_OscInitStruct.PLL.PLLVCOSEL = RCC_PLL1_VCORANGE_WIDE;
  RCC_OscInitStruct.PLL.PLLFRACN = 0;
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
  {
    Error_Handler();
  }

  /** Initializes the CPU, AHB and APB buses clocks
  */
  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_SYSCLK
                              |RCC_CLOCKTYPE_PCLK1|RCC_CLOCKTYPE_PCLK2
                              |RCC_CLOCKTYPE_PCLK3;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV1;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;
  RCC_ClkInitStruct.APB3CLKDivider = RCC_HCLK_DIV1;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_5) != HAL_OK)
  {
    Error_Handler();
  }

  /** Configure the programming delay
  */
  __HAL_FLASH_SET_PROGRAM_DELAY(FLASH_PROGRAMMING_DELAY_2);
}

/**
  * @brief Peripherals Common Clock Configuration
  * @retval None
  */
void PeriphCommonClock_Config(void)
{
  RCC_PeriphCLKInitTypeDef PeriphClkInitStruct = {0};

  /** Initializes the peripherals clock
  */
  PeriphClkInitStruct.PeriphClockSelection = RCC_PERIPHCLK_CKPER;
  PeriphClkInitStruct.CkperClockSelection = RCC_CLKPSOURCE_HSI;
  if (HAL_RCCEx_PeriphCLKConfig(&PeriphClkInitStruct) != HAL_OK)
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

  ADC_ChannelConfTypeDef sConfig = {0};

  /* USER CODE BEGIN ADC1_Init 1 */

  /* USER CODE END ADC1_Init 1 */

  /** Common config
  */
  hadc1.Instance = ADC1;
  hadc1.Init.ClockPrescaler = ADC_CLOCK_ASYNC_DIV4;
  hadc1.Init.Resolution = ADC_RESOLUTION_8B;
  hadc1.Init.DataAlign = ADC_DATAALIGN_RIGHT;
  hadc1.Init.ScanConvMode = ADC_SCAN_ENABLE;
  hadc1.Init.EOCSelection = ADC_EOC_SINGLE_CONV;
  hadc1.Init.LowPowerAutoWait = DISABLE;
  hadc1.Init.ContinuousConvMode = DISABLE;
  hadc1.Init.NbrOfConversion = 8;
  hadc1.Init.DiscontinuousConvMode = DISABLE;
  hadc1.Init.ExternalTrigConv = ADC_EXTERNALTRIG_T6_TRGO;
  hadc1.Init.ExternalTrigConvEdge = ADC_EXTERNALTRIGCONVEDGE_RISING;
  hadc1.Init.DMAContinuousRequests = ENABLE;
  hadc1.Init.SamplingMode = ADC_SAMPLING_MODE_NORMAL;
  hadc1.Init.Overrun = ADC_OVR_DATA_PRESERVED;
  hadc1.Init.OversamplingMode = DISABLE;
  if (HAL_ADC_Init(&hadc1) != HAL_OK)
  {
    Error_Handler();
  }

  /** Configure Regular Channel
  */
  sConfig.Channel = ADC_CHANNEL_0;
  sConfig.Rank = ADC_REGULAR_RANK_1;
  sConfig.SamplingTime = ADC_SAMPLETIME_24CYCLES_5;
  sConfig.SingleDiff = ADC_SINGLE_ENDED;
  sConfig.OffsetNumber = ADC_OFFSET_NONE;
  sConfig.Offset = 0;
  if (HAL_ADC_ConfigChannel(&hadc1, &sConfig) != HAL_OK)
  {
    Error_Handler();
  }

  /** Configure Regular Channel
  */
  sConfig.Channel = ADC_CHANNEL_1;
  sConfig.Rank = ADC_REGULAR_RANK_2;
  if (HAL_ADC_ConfigChannel(&hadc1, &sConfig) != HAL_OK)
  {
    Error_Handler();
  }

  /** Configure Regular Channel
  */
  sConfig.Channel = ADC_CHANNEL_4;
  sConfig.Rank = ADC_REGULAR_RANK_3;
  if (HAL_ADC_ConfigChannel(&hadc1, &sConfig) != HAL_OK)
  {
    Error_Handler();
  }

  /** Configure Regular Channel
  */
  sConfig.Channel = ADC_CHANNEL_8;
  sConfig.Rank = ADC_REGULAR_RANK_4;
  if (HAL_ADC_ConfigChannel(&hadc1, &sConfig) != HAL_OK)
  {
    Error_Handler();
  }

  /** Configure Regular Channel
  */
  sConfig.Channel = ADC_CHANNEL_10;
  sConfig.Rank = ADC_REGULAR_RANK_5;
  if (HAL_ADC_ConfigChannel(&hadc1, &sConfig) != HAL_OK)
  {
    Error_Handler();
  }

  /** Configure Regular Channel
  */
  sConfig.Channel = ADC_CHANNEL_11;
  sConfig.Rank = ADC_REGULAR_RANK_6;
  if (HAL_ADC_ConfigChannel(&hadc1, &sConfig) != HAL_OK)
  {
    Error_Handler();
  }

  /** Configure Regular Channel
  */
  sConfig.Channel = ADC_CHANNEL_12;
  sConfig.Rank = ADC_REGULAR_RANK_7;
  if (HAL_ADC_ConfigChannel(&hadc1, &sConfig) != HAL_OK)
  {
    Error_Handler();
  }

  /** Configure Regular Channel
  */
  sConfig.Channel = ADC_CHANNEL_13;
  sConfig.Rank = ADC_REGULAR_RANK_8;
  if (HAL_ADC_ConfigChannel(&hadc1, &sConfig) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN ADC1_Init 2 */

  /* USER CODE END ADC1_Init 2 */

}

/**
  * @brief GPDMA1 Initialization Function
  * @param None
  * @retval None
  */
static void MX_GPDMA1_Init(void)
{

  /* USER CODE BEGIN GPDMA1_Init 0 */

  /* USER CODE END GPDMA1_Init 0 */

  /* Peripheral clock enable */
  __HAL_RCC_GPDMA1_CLK_ENABLE();

  /* GPDMA1 interrupt Init */
    HAL_NVIC_SetPriority(GPDMA1_Channel0_IRQn, 0, 0);
    HAL_NVIC_EnableIRQ(GPDMA1_Channel0_IRQn);
    HAL_NVIC_SetPriority(GPDMA1_Channel1_IRQn, 0, 0);
    HAL_NVIC_EnableIRQ(GPDMA1_Channel1_IRQn);

  /* USER CODE BEGIN GPDMA1_Init 1 */

  /* USER CODE END GPDMA1_Init 1 */
  /* USER CODE BEGIN GPDMA1_Init 2 */

  /* USER CODE END GPDMA1_Init 2 */

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
  * @brief TIM3 Initialization Function
  * @param None
  * @retval None
  */
static void MX_TIM3_Init(void)
{

  /* USER CODE BEGIN TIM3_Init 0 */

  /* USER CODE END TIM3_Init 0 */

  TIM_ClockConfigTypeDef sClockSourceConfig = {0};
  TIM_MasterConfigTypeDef sMasterConfig = {0};
  TIM_OC_InitTypeDef sConfigOC = {0};

  /* USER CODE BEGIN TIM3_Init 1 */

  /* USER CODE END TIM3_Init 1 */
  htim3.Instance = TIM3;
  htim3.Init.Prescaler = 0;
  htim3.Init.CounterMode = TIM_COUNTERMODE_UP;
  htim3.Init.Period = 312;
  htim3.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
  htim3.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_DISABLE;
  if (HAL_TIM_Base_Init(&htim3) != HAL_OK)
  {
    Error_Handler();
  }
  sClockSourceConfig.ClockSource = TIM_CLOCKSOURCE_INTERNAL;
  if (HAL_TIM_ConfigClockSource(&htim3, &sClockSourceConfig) != HAL_OK)
  {
    Error_Handler();
  }
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
  htim6.Init.Prescaler = 249;
  htim6.Init.CounterMode = TIM_COUNTERMODE_UP;
  htim6.Init.Period = 999;
  htim6.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_DISABLE;
  if (HAL_TIM_Base_Init(&htim6) != HAL_OK)
  {
    Error_Handler();
  }
  sMasterConfig.MasterOutputTrigger = TIM_TRGO_UPDATE;
  sMasterConfig.MasterSlaveMode = TIM_MASTERSLAVEMODE_DISABLE;
  if (HAL_TIMEx_MasterConfigSynchronization(&htim6, &sMasterConfig) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN TIM6_Init 2 */

  /* USER CODE END TIM6_Init 2 */

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
  __HAL_RCC_GPIOC_CLK_ENABLE();
  __HAL_RCC_GPIOA_CLK_ENABLE();
  __HAL_RCC_GPIOB_CLK_ENABLE();

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(LED_STATUS_GPIO_Port, LED_STATUS_Pin, GPIO_PIN_RESET);

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(MCP_CS_GPIO_Port, MCP_CS_Pin, GPIO_PIN_SET);

  /*Configure GPIO pin : LED_STATUS_Pin */
  GPIO_InitStruct.Pin = LED_STATUS_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(LED_STATUS_GPIO_Port, &GPIO_InitStruct);

  /*Configure GPIO pin : MCP_CS_Pin */
  GPIO_InitStruct.Pin = MCP_CS_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;
  HAL_GPIO_Init(MCP_CS_GPIO_Port, &GPIO_InitStruct);

  /* USER CODE BEGIN MX_GPIO_Init_2 */

  /* USER CODE END MX_GPIO_Init_2 */
}

/* USER CODE BEGIN 4 */
void HAL_TIM_PWM_PulseFinishedCallback(TIM_HandleTypeDef *htim)
{
  if (htim->Instance == TIM3) {
    HAL_TIM_PWM_Stop_DMA(&htim3, TIM_CHANNEL_1);
    datasentflag = 1;
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
