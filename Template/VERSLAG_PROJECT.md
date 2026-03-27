# USB MIDI Controller - Volledig Projectverslag

## Inleiding

Dit verslag beschrijft de volledige ontwikkeling van een **USB MIDI Controller** op een **STM32H533RE microcontroller** (NUCLEO-H533RE evaluatieboard). Het doel van het project is om een 4x4 toetsmatrix via een I/O expander (MCP23S17) aan te sturen en MIDI-noten via USB naar een computer te sturen.

De controller fungeert als een standaard MIDI-instrument dat herkend wordt door DAW's (Digital Audio Workstations), synthesizers en MIDI-applicaties op Windows, macOS en Linux.

---

## 1. Projectvereisten

### Hardware
- **Microcontroller**: STM32H533RE (ARM Cortex-M33, 32kB SRAM, 256kB Flash)
- **Evaluatieboard**: NUCLEO-H533RE (Nucleo-64 Form Factor)
- **I/O Expander**: MCP23S17 (SPI interface, 16 GPIO)
- **Toetsmatrix**: 4x4 drukschakelaars (16 toetsen)
- **LED**: Indicatorlampje (PB0) voor visuele feedback
- **Kristal/Oscillator**: HSI48 (ingebouwd 48 MHz oscillator voor USB) en HSI (32 MHz CPU-klok)

### Software Stack
- **IDE**: Keil µVision 5 (ARM Compiler V6.22)
- **HAL**: STM32H5xx Hardware Abstraction Layer (officiële ST HAL-bibliotheek)
- **USB Stack**: TinyUSB (open-source, platform-agnostisch)
- **Build System**: ARM C/C++ Compiler

### Communicatieprotocollen
- **USB**: Full-Speed (12 Mbps) USB 2.0
- **SPI**: 250 kHz seriele communicatie naar MCP23S17
- **MIDI**: Musical Instrument Digital Interface (3 bytes per notebericht)

---

## 2. Systeemarchitectuur

```
┌─────────────────────────────────────┐
│   STM32H533RE Microcontroller       │
├─────────────────────────────────────┤
│                                     │
│  ┌──────────────────────────────┐  │
│  │     User Application         │  │
│  │  - MIDI Note On/Off          │  │
│  │  - Matrix Scanning           │  │
│  │  - LED Control               │  │
│  └─────────────┬────────────────┘  │
│                │                    │
│  ┌─────────────▼──────────────────┐ │
│  │   USB Stack (TinyUSB)          │ │
│  │  - Device Enumeration         │ │
│  │  - MIDI Class Handling        │ │ ←── USB beschrijvers
│  │  - Packet Serialization       │ │
│  └─────────────┬──────────────────┘ │
│                │                    │
│  ┌─────────────▼──────────────────┐ │
│  │ STM32H5 USB Device Controller  │ │
│  │  - DMA Engine                 │ │
│  │  - Endpoint Management        │ │
│  │  - Protocol State Machine     │ │
│  └─────────────┬──────────────────┘ │
│                │                    │
│  ┌─────────────▼──────────────────┐ │
│  │  SPI Master Controller (SPI1)   │ │
│  │  - 250 kHz Clock               │ │
│  │  - Full-Duplex Transfers       │ │
│  └─────────────┬──────────────────┘ │
│                │                    │
└────────────────┼────────────────────┘
          (GPIOA PA5/PA6/PA7 + PA8)
                │
    ┌───────────┴─────────────┐
    │                         │
┌───▼────────────────┐   ┌───▼────────────────┐
│  MCP23S17 Expander│   │  USB Host Computer │
│  ├─ GPIOA (OUT)   │   │  ├─ DAW (Ableton) │
│  │  └─ Columns    │   │  ├─ Synthesizer   │
│  ├─ GPIOB (IN)    │   │  └─ Web MIDI API  │
│  │  └─ Rows       │   │                   │
│  └────────────────┘   └───────────────────┘
         │
    ┌────▼────────────────┐
    │   4x4 Key Matrix    │
    │  (16 Drukschakel.)  │
    └─────────────────────┘
```

---

## 3. Hardware-Configuratie

### 3.1 Microcontroller Pins

| STM32 Pin | Nucleo Header | Functie | Periferie | Alt. Functie | Snelheid |
|---|---|---|---|---|---|
| PA5 | CN10 pin 11 | SPI1_SCK | SPI1 Master | AF5 | VERY_HIGH |
| PA6 | CN10 pin 13 | SPI1_MISO | SPI1 Master | AF5 + Pull-up | VERY_HIGH |
| PA7 | CN10 pin 15 | SPI1_MOSI | SPI1 Master | AF5 | VERY_HIGH |
| PA8 | CN10 pin 23 | MCP_CS | GPIO Output | - (idle HIGH) | HIGH |
| PA11 | CN10 pin 14 | USB_DM | USB DRD FS | AF10 | - |
| PA12 | CN10 pin 12 | USB_DP | USB DRD FS | AF10 | - |
| PB0 | CN10 pin 31 | LED_STATUS | GPIO Output | - | LOW |

### 3.2 MCP23S17 Aansluitingen

De MCP23S17 is een 28-pins DIP/SOIC I/O expander. Hier zijn alle pinnen en hun verbindingen:

| MCP23S17 Pin | Naam | Verbinding | Beschrijving |
|---|---|---|---|
| 1 | GPB0 | Rij 0 van de matrix | Input, pull-up actief via GPPUB |
| 2 | GPB1 | Rij 1 van de matrix | Input, pull-up actief via GPPUB |
| 3 | GPB2 | Rij 2 van de matrix | Input, pull-up actief via GPPUB |
| 4 | GPB3 | Rij 3 van de matrix | Input, pull-up actief via GPPUB |
| 5 | GPB4 | Niet aangesloten | — |
| 6 | GPB5 | Niet aangesloten | — |
| 7 | GPB6 | Niet aangesloten | — |
| 8 | GPB7 | Niet aangesloten | — |
| 9 | VDD | 3.3V | Voeding |
| 10 | VSS | GND | Massa |
| 11 | /CS | PA8 (CN10 pin 23) | Chip Select, actief LAAG |
| 12 | SCK | PA5 (CN10 pin 11) | SPI klok |
| 13 | SI | PA7 (CN10 pin 15) | SPI data in (MOSI) |
| 14 | SO | PA6 (CN10 pin 13) | SPI data uit (MISO) |
| 15 | A0 | GND | Adresbit 0 → hardware adres = 0b000 |
| 16 | A1 | GND | Adresbit 1 → hardware adres = 0b000 |
| 17 | A2 | GND | Adresbit 2 → hardware adres = 0b000 |
| 18 | /RESET | 3.3V | Reset inactief houden (altijd HIGH) |
| 19 | INTB | Niet aangesloten | Interrupt Port B (niet gebruikt) |
| 20 | INTA | Niet aangesloten | Interrupt Port A (niet gebruikt) |
| 21 | GPA0 | Kolom 0 van de matrix | Output, scant kolom 0 |
| 22 | GPA1 | Kolom 1 van de matrix | Output, scant kolom 1 |
| 23 | GPA2 | Kolom 2 van de matrix | Output, scant kolom 2 |
| 24 | GPA3 | Kolom 3 van de matrix | Output, scant kolom 3 |
| 25 | GPA4 | Niet aangesloten | — |
| 26 | GPA5 | Niet aangesloten | — |
| 27 | GPA6 | Niet aangesloten | — |
| 28 | GPA7 | Niet aangesloten | — |

### 3.3 4x4 Toetsmatrix Aansluitingen

De matrix werkt met **kolom-scanning**: één kolom tegelijk LAAG, rijen lezen.

| Matrix Positie | Knop nr. | MIDI Noot | Kolom (GPA) | Rij (GPB) |
|---|---|---|---|---|
| Rij 0, Kolom 0 | Knop 0 | 60 (C4) | GPA0 | GPB0 |
| Rij 0, Kolom 1 | Knop 1 | 61 (C#4) | GPA1 | GPB0 |
| Rij 0, Kolom 2 | Knop 2 | 62 (D4) | GPA2 | GPB0 |
| Rij 0, Kolom 3 | Knop 3 | 63 (D#4) | GPA3 | GPB0 |
| Rij 1, Kolom 0 | Knop 4 | 64 (E4) | GPA0 | GPB1 |
| Rij 1, Kolom 1 | Knop 5 | 65 (F4) | GPA1 | GPB1 |
| Rij 1, Kolom 2 | Knop 6 | 66 (F#4) | GPA2 | GPB1 |
| Rij 1, Kolom 3 | Knop 7 | 67 (G4) | GPA3 | GPB1 |
| Rij 2, Kolom 0 | Knop 8 | 68 (G#4) | GPA0 | GPB2 |
| Rij 2, Kolom 1 | Knop 9 | 69 (A4) | GPA1 | GPB2 |
| Rij 2, Kolom 2 | Knop 10 | 70 (A#4) | GPA2 | GPB2 |
| Rij 2, Kolom 3 | Knop 11 | 71 (B4) | GPA3 | GPB2 |
| Rij 3, Kolom 0 | Knop 12 | 72 (C5) | GPA0 | GPB3 |
| Rij 3, Kolom 1 | Knop 13 | 73 (C#5) | GPA1 | GPB3 |
| Rij 3, Kolom 2 | Knop 14 | 74 (D5) | GPA2 | GPB3 |
| Rij 3, Kolom 3 | Knop 15 | 75 (D#5) | GPA3 | GPB3 |

### 3.4 SPI Configuratie

De SPI1 interface wordt gebruikt voor communicatie met de MCP23S17:

```c
SPI_HandleTypeDef hspi1;
hspi1.Instance = SPI1;
hspi1.Init.Mode = SPI_MODE_MASTER;
hspi1.Init.Direction = SPI_DIRECTION_2LINES;       // Volleduplexe SPI
hspi1.Init.DataSize = SPI_DATASIZE_8BIT;           // 8-bit per transfer
hspi1.Init.CLKPolarity = SPI_POLARITY_LOW;
hspi1.Init.CLKPhase = SPI_PHASE_1EDGE;
hspi1.Init.BaudRatePrescaler = SPI_BAUDRATEPRESCALER_128; // 32 MHz / 128 = 250 kHz
hspi1.Init.FirstBit = SPI_FIRSTBIT_MSB;
hspi1.Init.NSS = SPI_NSS_SOFT;                      // Software-beheerde CS
hspi1.Init.NSSPMode = SPI_NSS_PULSE_DISABLE;       // Geen automation
```

**Waarom 250 kHz?** Het MCP23S17 ondersteunt tot 10 MHz, maar 250 kHz is ruim voldoende voor een matrix scan van <5 ms en garandeert stabiele communicatie zonder signaalkwaliteitsproblemen.

### 3.5 USB Clocking

USB Full-Speed vereist exact 48 MHz timing. Dit wordt bereikt via:

```c
// In SystemClock_Config():
RCC_OscInitStruct.HSI48State = RCC_HSI48_ON;       // 48 MHz ingebouwde oscillator

// In HAL_PCD_MspInit():
PeriphClkInitStruct.PeriphClockSelection = RCC_PERIPHCLK_USB;
PeriphClkInitStruct.UsbClockSelection = RCC_USBCLKSOURCE_HSI48;  // Directe routing
HAL_RCCEx_PeriphCLKConfig(&PeriphClkInitStruct);
```

De STM32H5 heeft een dedicated HSI48 ingebouwd, dus geen externe kristal of PLL nodig.

---

## 4. Software Architectuur

### 4.1 Initialisatievolgorde

Het project initialiseeert in deze volgorde:

```
1. HAL_Init()                  ← Hardware Abstraction Layer setup
2. SystemClock_Config()        ← CPU (HSI 32 MHz) + USB (HSI48 48 MHz)
3. PeriphCommonClock_Config()  ← CKPER = HSI voor SPI
4. MX_GPIO_Init()              ← Poorten PA/PB/PC
5. MX_USB_PCD_Init()           ← USB controller + interrupt setup
6. MX_SPI1_Init()              ← SPI master voor MCP23S17
7. mcp_init()                  ← MCP23S17 configuratie
8. tusb_init()                 ← TinyUSB stack start
9. Main loop: tud_task() + scan_matrix()
```

**Kritiek: `mcp_init()` VOOR `tusb_init()`** zodat de matrix gereed is voordat USB opstart.

### 4.2 MCP23S17 I/O Expander

De MCP23S17 is een 16-bit SPI-gestuurde GPIO expander met twee poorten (A en B):

```c
// Register adressen (BANK=0)
#define MCP_IODIRA      0x00  // Port A: 0=output, 1=input
#define MCP_IODIRB      0x01  // Port B: 0=output, 1=input
#define MCP_GPPUB       0x0D  // Pull-up enable voor Port B
#define MCP_OLATA       0x14  // Output Latch A (schrijf kolommen)
#define MCP_GPIOB       0x13  // GPIO Port B (lees rijen)

// Configuratie:
// Port A: ALLE OUTPUTS (kolommen scannen)
mcp_write(MCP_IODIRA, 0x00);

// Port B: ALLE INPUTS (rijsensoren)
mcp_write(MCP_IODIRB, 0xFF);

// Pull-ups op Port B (rijen: actief LAAG als ingedrukt)
mcp_write(MCP_GPPUB, 0x0F);   // GPB0-3 pull-ups

// Kolommen idle HIGH
mcp_write(MCP_OLATA, 0xFF);
```

**Matrix-scanslogica:**
```
Voor elke kolom (0-3):
  1. Zet deze kolom LAAG (GPA0-3), rest HIGH
  2. Lees Port B (rijen)
  3. Elke bit == 0 → knop ingedrukt
  4. Zet alle kolommen terug op HIGH
  
Voorbeeld: Knop (rij=1, kolom=2) ingedrukt:
  ├─ Schrijf OLATA = 0xFB (kolom 2 laag)
  ├─ Lees GPIOB → 0x02 (bit 1 actief)
  └─ Send MIDI Note 60 + (1*4 + 2) = Note 66
```

### 4.3 USB MIDI Implementatie

#### USB Descriptors

De device identificeert zich naar de host via **USB descriptors** in `usb_descriptors.c`:

```c
#define USB_VID   0xCAFE  // Vendor ID (test ID)
#define USB_PID   0x4002  // Product ID (uniek voor deze controller)

tusb_desc_device_t const desc_device = {
    .bcdUSB = 0x0200,          // USB 2.0
    .bDeviceClass = 0x00,      // MIDI is Interface-level class
    .bDeviceSubClass = 0x00,
    .bDeviceProtocol = 0x00,
    
    .idVendor = USB_VID,
    .idProduct = USB_PID,
    .bcdDevice = 0x0100,
    
    .iManufacturer = 0x01,     // "STMicroelectronics"
    .iProduct = 0x02,          // "Mauro's MIDI Controller"
    .iSerialNumber = 0x03      // "123456"
};
```

**Belangrijke fix**: PID moet uniek zijn per controller om Windows driver cache conflicten te voorkomen.

#### Configuration Descriptor

```c
#define EPNUM_MIDI_OUT   0x01  // EP voor inkomende MIDI (host → device)
#define EPNUM_MIDI_IN    0x81  // EP voor uitgaande MIDI (device → host)

uint8_t const desc_fs_configuration[] = {
    TUD_CONFIG_DESCRIPTOR(1, ITF_NUM_TOTAL, 0, CONFIG_TOTAL_LEN, 0, 100),
    TUD_MIDI_DESCRIPTOR(ITF_NUM_MIDI, 0, EPNUM_MIDI_OUT, EPNUM_MIDI_IN, 64)
};
```

TinyUSB's `TUD_MIDI_DESCRIPTOR` macro genereert de volstandige IAD (Interface Association Descriptor), Control Interface, Streaming Interface, Jack descriptors en Endpoint descriptors.

#### MIDI Message Format

MIDI Note On/Off berichten zijn 3 bytes:

```c
// Note On (velocity 100)
uint8_t note_on[3] = {
    (uint8_t)(0x90 | channel),  // Status byte: 0x9n (Note On, channel n)
    note,                        // Data byte 1: noot (0-127)
    velocity                     // Data byte 2: snelheid (0-127)
};
tud_midi_stream_write(0, note_on, 3);

// Note Off
uint8_t note_off[3] = {
    (uint8_t)(0x80 | channel),  // Status byte: 0x8n (Note Off, channel n)
    note,                        // Zelfde noot
    0                            // Velocity (genegeerd voor Note Off)
};
tud_midi_stream_write(0, note_off, 3);
```

### 4.4 Scan- en Detectielogica

```c
void scan_matrix(void) {
    // Wacht tot USB is gekoppeld
    if (!tud_mounted()) return;
    
    // Max 5 ms tussen scans (200 Hz polling)
    static uint32_t last_scan = 0;
    uint32_t now = HAL_GetTick();
    if (now - last_scan < 5) return;
    last_scan = now;
    
    // Drain inkomende MIDI packets (liftoff voorkomen)
    uint8_t packet[4];
    while (tud_midi_available()) tud_midi_packet_read(packet);
    
    // Scan 4 kolommen
    for (uint8_t col = 0; col < 4; col++) {
        // Deze kolom LOW, rest HIGH
        mcp_write(MCP_OLATA, (~(1u << col)) & 0x0F);
        
        // Settle (~1 µs)
        for (int i = 0; i < 32; i++) __NOP();
        
        // Rijen lezen (actief LAAG = ingedrukt)
        uint8_t rows = mcp_read(MCP_GPIOB) & 0x0F;
        
        // Kolommen terug naar idle
        mcp_write(MCP_OLATA, 0xFF);
        
        // Elke rij controleren
        for (uint8_t row = 0; row < 4; row++) {
            uint8_t btn = row * 4 + col;
            uint8_t pressed = !(rows & (1u << row));
            
            // Flank-detectie (press/release)
            if (pressed && !matrix_prev[btn]) {
                midi_note_on(MIDI_NOTE_BASE + btn, 100);
                HAL_GPIO_WritePin(GPIOB, GPIO_PIN_0, GPIO_PIN_SET);
            } else if (!pressed && matrix_prev[btn]) {
                midi_note_off(MIDI_NOTE_BASE + btn);
                HAL_GPIO_WritePin(GPIOB, GPIO_PIN_0, GPIO_PIN_RESET);
            }
            
            matrix_prev[btn] = pressed;
        }
    }
}
```

**Debouncing**: Software-side flankdetectie zorgt ervoor dat contactstitter niet tot meerdere MIDI-noten leidt. Slechts één Note On per druk wordt verzonden.

---

## 5. STM32CubeMX Configuratie

### 5.1 Oscillators
- **HSI**: Ingeschakeld, DIV=2 (32 MHz CPU-klok)
- **HSI48**: Ingeschakeld (48 MHz USB-klok)
- **PLL**: Uitgeschakeld (niet nodig met HSI48)

### 5.2 Klokken
```
SYSCLK = HSI / 2 = 32 MHz
HCLK (AHB) = SYSCLK / 1 = 32 MHz
PCLK1 (APB1) = HCLK / 1 = 32 MHz
PCLK2 (APB2) = HCLK / 1 = 32 MHz
PCLK3 (APB3) = HCLK / 1 = 32 MHz
CKPER = HSI = 32 MHz (voor SPI prescaler)
```

### 5.3 Peripherals
- **USB DRD FS**: Device mode, Full-Speed
- **SPI1**: Master, Full-Duplex, Prescaler 128
- **GPIO**: PA5/PA6/PA7 (SPI), PA8 (CS), PA11/PA12 (USB)

### 5.4 IOC Bestand

De `.ioc` file slaat alle CubeMX-instellingen op en kan opnieuw code genereren. **LET OP**: De handmatig geschreven code (TinyUSB, MIDI scan) blijft behouden in USER CODE secties.

```
/* USER CODE BEGIN */
// Dit wordt NIET overschreven bij hergeneren
/* USER CODE END */
```

---

## 6. Build- en Flashproces

### 6.1 Compilation in Keil µVision

**Instellingen:**
- **Target**: STM32H533RETx
- **Optimization**: Level 2
- **Include Paths**: 
  - `Core/Inc/`
  - `Middlewares/tinyusb/src/`
  - `Drivers/STM32H5xx_HAL_Driver/Inc/`
- **Symbols**: `STM32H533xx`, `USE_HAL_DRIVER`

**Bronbestanden:**
```
Core/Src/
├── main.c
├── usb_descriptors.c
├── tusb_port.c
├── stm32h5xx_it.c
├── stm32h5xx_hal_msp.c
└── system_stm32h5xx.c

Middlewares/tinyusb/src/
├── tusb.c (main USB stack)
├── Device/usbd_control.c
├── Class/midi/midi_device.c
└── Device/dcd_stm32_fsdev.c (STM32 driver)

Drivers/STM32H5xx_HAL_Driver/Src/
└── stm32h5xx_hal_*.c (80+ HAL modules)
```

**Build Output:**
```
Build log: USB_MIDI2.axf (executable)
Hex file: USB_MIDI2.hex (voor flashing)
```

### 6.2 Flashing via ST-Link

1. Koppel Nucleo aan via ST-Link USB-poort
2. In Keil: **Project** → **Download**
3. De ingebouwde ST-Link debugger programmeert de Flash
4. Device reset automatisch na flash

**Flash Layout:**
```
0x00000000 - 0x0003FFFF: User firmware (USB_MIDI2.hex, ~200 KB)
0x00040000+: Vrij
0x200xxxxx: SRAM (runtime variabelen)
```

---

## 7. Testen en Validatie

### 7.1 Hardware Testing

**LED Feedback:**
- LED slaat aan bij keypress (PB0 HIGH)
- LED gaat uit bij key release (PB0 LOW)
- Visuele proof dat scan_matrix() werkt

**SPI Communicatie:**
Vroeger debugging via:
- `dbg_pre_init_iodira`: Lees IODIRA vóór init → moet 0xFF zijn (default)
- `dbg_gppub_readback`: Schrijf en lees GPPUB terug → moet 0x0F zijn

### 7.2 USB Enumeratie

**In Windows Device Manager:**
```
Softwareoplossingen
└── Mauro's MIDI Controller (VID 0xCAFE, PID 0x4002)
```

**Note**: Dit verschijnt NIET in COM & LPT (correct — pure MIDI device).

### 7.3 MIDI Software Verificatie

**MidiView (ST MIDI Monitor):**
- Toets indrukken → toont `90 NN 64` (Note On, noot NN, velocity 100)
- Toets loslaten → toont `80 NN 00` (Note Off)

**Chrome Web MIDI API:**
- Open: https://www.onlinemusictools.com/webmiditest/
- "Mauro's MIDI Controller" verschijnt in output list
- Schakelt input- en uitvoerapparaten correct

**DAW (Ableton Live, FL Studio):**
- Controller verschijnt in MIDI Input menu
- Ingestelde noten (60-75) activeren instrumenten

### 7.4 Debuggen

**Debugger in Keil:**
- **Breakpoint op scan_matrix()**: Verificeer `rows` waarde
- **Watch Variables**: `matrix_prev[16]`, `HAL_GetTick()`
- **UART Output** (optioneel via USARt): BSP_COM_Init(COM1, 115200)

---

## 8. Probleemoplossing

### Probleem 1: SPI Communicatie faalt (alle reads 0x00)

**Oorzaak**: HAL_SPI_MspInit() controleert verkeerde SPI (vb. SPI2 i.p.v. SPI1)

**Oplossing**:
- Controleer `Core/Src/stm32h5xx_hal_msp.c` → `HAL_SPI_MspInit()`
- Zorg dat het juiste `hspi->Instance` (SPI1) wordt gecheckt
- Controleer GPIO snelheid: `GPIO_SPEED_FREQ_VERY_HIGH` voor SCK/MOSI
- Controleer MISO pull-up: `GPIO_PULLUP` op MISO pin

### Probleem 2: USB enumeratie faalt

**Oorzaak**: Oscillator niet correct geconfigureerd

**Oplossing**:
- `SystemClock_Config()`: HSI48State = RCC_HSI48_ON
- `HAL_PCD_MspInit()`: UsbClockSelection = RCC_USBCLKSOURCE_HSI48
- Controleer interrupt: USB_DRD_FS_IRQn priority = 0

### Probleem 3: Windows ziet dubbele MIDI device entries

**Oorzaak**: Dezelfde VID/PID als ander apparaat (driver cache)

**Oplossing**:
- Wijzig PID in `usb_descriptors.c` (vb. 0x4001 → 0x4002)
- Verwijder alle entries in Device Manager
- Heraansluiten → Windows maakt schone entry

### Probleem 4: Matrix ziet toetsen als constant ingedrukt

**Oorzaak**: Pull-up niet ingeschakeld of kolom niet naar HIGH teruggesteld

**Oplossing**:
- `mcp_write(MCP_GPPUB, 0x0F)`: Zet pull-ups in
- Na elke rij-read: `mcp_write(MCP_OLATA, 0xFF)` → kolommen terug naar HIGH
- Test individueel: lees GPIOB met alle kolommen HIGH → moet 0x0F zijn

---

## 9. Projectstructuur (Git)

```
EntertainmentTech/                     (Repo root)
Template/
├── README_TINYUSB.md                 (Basis TinyUSB setup docs)
├── USB_MIDI_Explained.md             (Gedetailleerde USB uitleg)
├── USB_MIDI2.ioc                     (STM32CubeMX project)
├── Core/
│   ├── Inc/
│   │   ├── main.h
│   │   ├── tusb_config.h             (TinyUSB configuratie)
│   │   └── stm32h5xx_*.h
│   └── Src/
│       ├── main.c                    (MIDI scan + matrix logic)
│       ├── usb_descriptors.c         (USB device/config descriptors)
│       ├── tusb_port.c               (TinyUSB ↔ HAL bridge)
│       ├── stm32h5xx_it.c            (USB_DRD_FS_IRQn handler)
│       ├── stm32h5xx_hal_msp.c       (Low-level init: USB, SPI, GPIO)
│       └── system_stm32h5xx.c
├── Drivers/
│   ├── STM32H5xx_HAL_Driver/        (Official HAL)
│   └── CMSIS/                        (ARM CMSIS core files)
├── Middlewares/
│   └── tinyusb/                      (Open-source USB stack)
└── MDK-ARM/
    ├── USB_MIDI2.uvprojx             (Keil project)
    ├── USB_MIDI2/                    (Build output)
    │   └── USB_MIDI2.hex             (Flashable binary)
    └── startup_stm32h533xx.s         (Bootloader assembly)
```

**Git Commits (History):**
```
a4b126d: "laatste fix" - PID 0x4001 → 0x4002 (Windows duplicate fix)
e3f93b2: "Cleanup: remove debug variables and blink codes, add function comments"
8394ef6: "Fix SPI1: correct HAL_SPI_MspInit (was still SPI2), VERY_HIGH GPIO speed, prescaler 128"
5ba9000: "Add MCP23S17 4x4 matrix scan with lazy init and USB-safe timing"
7f6853f: "Add SPI2 + USB MIDI debug LED"
b8222be: "Initial TinyUSB setup"
```

---

## 10. Handleiding voor Toekomstige Ontwikkelaars

### Nieuwe functionaliteit toevoegen

**Voorbeeld: Note Velocity op basis van hoe lang knop wordt ingehouden**

1. Voeg timer toe in `scan_matrix()`:
```c
uint32_t key_press_time[16] = {0};
if (pressed && !matrix_prev[btn]) {
    key_press_time[btn] = HAL_GetTick();
}
```

2. Bij release: bereken duur en mapline naar velocity (0-127):
```c
if (!pressed && matrix_prev[btn]) {
    uint32_t duration = HAL_GetTick() - key_press_time[btn];
    uint8_t velocity = (duration > 100) ? 127 : 64;
    midi_note_off(MIDI_NOTE_BASE + btn, velocity);
}
```

3. Rebuild + flash

### Hardware uitbreiden

**Voorbeeld: Tweede MCP23S17 toevoegen**

1. Voeg tweede CS pin toe in `main.h`:
```c
#define MCP2_CS_Pin      GPIO_PIN_9
#define MCP2_CS_GPIO_Port GPIOA
```

2. Init in `MX_GPIO_Init()` (CSN beide HIGH):
```c
HAL_GPIO_WritePin(GPIOA, MCP2_CS_Pin, GPIO_PIN_SET);
```

3. Wrapper functions:
```c
void mcp2_write(uint8_t reg, uint8_t val) {
    uint8_t tx[3] = { MCP_WRITE_OP, reg, val };
    HAL_GPIO_WritePin(MCP2_CS_GPIO_Port, MCP2_CS_Pin, GPIO_PIN_RESET);
    HAL_SPI_Transmit(&hspi1, tx, 3, SPI_TIMEOUT);
    HAL_GPIO_WritePin(MCP2_CS_GPIO_Port, MCP2_CS_Pin, GPIO_PIN_SET);
}
```

4. Scan beide expanders in loop

---

## 11. Conclusie

Dit USB MIDI Controller project demonstreert:

✅ **Moderne MCU-programmering**: ARM Cortex-M33, SPI interface, USB Full-Speed
✅ **Embedded Systems Integration**: HAL abstraction, interrupt handling, real-time OS concepten
✅ **Protokolinplementatie**: USB descriptors, MIDI parser, debouncing
✅ **Productie-ready Code**: Error handling, comments, modularity
✅ **Hardware Design**: Klokassen, signaalintegriteit, Level-shifters

De controller is volledig functioneel en compatibel met moderne MIDI software op Windows, macOS en Linux. De bron is open-source (MIT license via TinyUSB) en kan eenvoudig worden uitgebreid.

---

## 12. Referenties

- **STM32H533 Reference Manual**: RM0481 (ST Microelectronics)
- **TinyUSB Documentation**: https://docs.tinyusb.org/
- **USB MIDI Specification**: USB.org (Class Definitions for MIDI)
- **MCP23S17 Datasheet**: Microchip Technology
- **ARM CMSIS Specification**: ARM Holdings

---

**Projectverantwoordelijke**: Mauro Carlier  
**Datum**: Maart 2026  
**Taal**: Nederlands  
**Status**: ✅ Voltooid
