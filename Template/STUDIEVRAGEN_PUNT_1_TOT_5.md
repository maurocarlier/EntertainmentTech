# Studiedocument - Vragen Punt 1 tot 5

Dit document bundelt alle vragen en kernuitleg van punt 1 tot en met punt 5, zodat je alles op een duidelijke manier kan herhalen voor je mondelinge uitleg.

## 1. Project Architectuur

### 1.1 Wat is de volledige hardware-keten?

| Onderdeel | Interface | Rol |
|---|---|---|
| 4x4 knoppenmatrix | Fysieke bedrading | 16 drukknoppen georganiseerd in rijen en kolommen |
| MCP23S17 | SPI | I/O-expander die kolommen aanstuurt en rijen inleest |
| STM32H533 | SPI + USB | Verwerkt knopdrukken en verstuurt MIDI |
| Computer / DAW | USB MIDI | Ontvangt de MIDI-berichten |

### 1.2 Hoe werken hardware en software samen?

- Een knop verbindt een rij met een kolom.
- De STM32 maakt via de MCP23S17 telkens een kolom actief.
- De MCP23S17 leest daarna welke rij laag wordt.
- De STM32 zet die detectie om in een MIDI Note On of Note Off.
- Via USB wordt dat bericht naar de computer of DAW gestuurd.

### 1.3 Welke softwarelaag heeft welke verantwoordelijkheid?

| Laag | Bestand / component | Verantwoordelijkheid |
|---|---|---|
| Applicatielaag | `main.c` | Matrix scannen, knopdetectie, MIDI-logica |
| USB MIDI-laag | TinyUSB | USB MIDI-communicatie en berichtverzending |
| HAL-laag | STM32 HAL | SPI, GPIO en USB-functies aanbieden |
| MSP-laag | `stm32h5xx_hal_msp.c` | Pinnen, klokken en periferiën laag-niveau configureren |
| Descriptorlaag | `usb_descriptors.c` | Host vertellen welk USB-apparaat dit is |

### 1.4 Wat is de dataflow van knopdruk tot MIDI-bericht?

1. Een knop wordt ingedrukt in de 4x4 matrix.
2. De STM32 activeert via SPI één kolom op de MCP23S17.
3. De STM32 leest via SPI de rij-ingangen terug.
4. De software bepaalt welke knop actief is.
5. De huidige toestand wordt vergeleken met de vorige toestand.
6. Bij een nieuwe druk wordt een Note On-bericht gemaakt.
7. Bij loslaten wordt een Note Off-bericht gemaakt.
8. TinyUSB verstuurt het bericht via USB MIDI naar de computer.

### 1.5 Waarom is de initialisatievolgorde belangrijk?

| Stap | Waarom nodig? |
|---|---|
| Klokken configureren | Alle periferiën hebben een geldige klok nodig |
| SPI initialiseren | Nodig om met de MCP23S17 te communiceren |
| USB hardware initialiseren | Nodig voor USB-enumeratie |
| MCP23S17 initialiseren | Matrixpinnen correct instellen |
| TinyUSB starten | Pas mogelijk wanneer USB hardware klaar is |

Als deze volgorde verkeerd is, kan de USB-stack niet starten of werkt de matrixscan niet correct.

### 1.6 Wat doet de I/O-expander precies?

- De MCP23S17 voegt 16 extra digitale I/O-pinnen toe.
- In dit project worden 4 pinnen gebruikt voor kolommen en 4 voor rijen.
- Hij wordt volledig via SPI bestuurd door de STM32.
- Daardoor hoeft de STM32 niet alle matrixlijnen rechtstreeks zelf te gebruiken.

### 1.7 Wat is het verschil tussen HAL en applicatielaag?

| HAL-laag | Applicatielaag |
|---|---|
| Regelt hoe de hardware wordt aangesproken | Bepaalt wat de toepassing moet doen |
| Voorbeeld: `HAL_SPI_Transmit()` | Voorbeeld: `scan_matrix()` |
| Kent pinnen, registers, timers, SPI | Kent knoppen, noten en MIDI-logica |

## 2. MIDI en USB MIDI

### 2.1 Wat is MIDI?

- MIDI betekent Musical Instrument Digital Interface.
- Het is een digitale communicatiestandaard voor muziekinstrumenten, controllers en computers.
- MIDI verstuurt geen geluid, maar commando's.
- Die commando's beschrijven bijvoorbeeld welke noot moet starten of stoppen.

### 2.2 Hoe zit een MIDI-bericht in elkaar?

| Byte | Naam | Functie |
|---|---|---|
| Byte 1 | Statusbyte | Type bericht + MIDI-kanaal |
| Byte 2 | Databyte 1 | Meestal nootnummer |
| Byte 3 | Databyte 2 | Meestal velocity |

### 2.3 Wat is het verschil tussen Note On en Note Off?

| Bericht | Statusbyte | Betekenis |
|---|---|---|
| Note On | `0x90` | Start een noot |
| Note Off | `0x80` | Stopt een noot |

Belangrijk:

- Een Note On met velocity `0` mag ook als Note Off geïnterpreteerd worden.
- Daardoor zie je soms `0x90 3C 00` in plaats van `0x80 3C 00`.

### 2.4 Wat betekent kanaal, nootnummer en velocity?

| Element | Betekenis |
|---|---|
| Kanaal | Voor welk instrument of welke MIDI-stroom het bericht bedoeld is |
| Nootnummer | Welke toon gespeeld moet worden |
| Velocity | Hoe sterk de noot gespeeld wordt |

### 2.5 Waarom loopt een nootnummer van 0 tot 127?

- MIDI-data gebruikt 7 bits voor de eigenlijke data.
- Met 7 bits kan je $2^7 = 128$ waarden voorstellen.
- Dat geeft het bereik 0 tot 127.
- Daarom liggen nootnummer en velocity altijd binnen dat bereik.

### 2.6 Hoe lees je bijvoorbeeld `0x90 3C 64`?

| Byte | Hex | Decimaal | Betekenis |
|---|---|---:|---|
| 1 | `0x90` | 144 | Note On op kanaal 0 |
| 2 | `0x3C` | 60 | Nootnummer 60 |
| 3 | `0x64` | 100 | Velocity 100 |

Rekenvoorbeeld:

$$
0x3C = 3 \times 16 + 12 = 60
$$

$$
0x64 = 6 \times 16 + 4 = 100
$$

### 2.7 Hoe werkt een USB MIDI class?

- Het apparaat meldt zich aan als een standaard USB MIDI-apparaat.
- Op USB-niveau valt MIDI onder de USB Audio class.
- Daardoor herkent de computer het toestel zonder aparte driver.
- De host gebruikt de descriptors om te weten welk type apparaat aangesloten is.

### 2.8 Hoe worden Note On en Note Off in code gemaakt?

```c
uint8_t msg[3] = { (uint8_t)(0x90 | MIDI_CHANNEL), note, velocity };
tud_midi_stream_write(0, msg, 3);
```

| Codeonderdeel | Betekenis |
|---|---|
| `msg[3]` | Array van 3 bytes voor het MIDI-bericht |
| `0x90 | MIDI_CHANNEL` | Statusbyte voor Note On op gekozen kanaal |
| `note` | Nootnummer |
| `velocity` | Aanslagsterkte |
| `tud_midi_stream_write(...)` | Verstuurt de bytes via USB MIDI |

Voor Note Off wordt dezelfde structuur gebruikt met `0x80 | MIDI_CHANNEL` en velocity `0`.

## 3. SPI en MCP23S17

### 3.1 Hoe werkt SPI?

SPI is een synchrone seriële communicatie tussen een master en een slave.

- STM32 = master
- MCP23S17 = slave

### 3.2 Wat zijn de 4 SPI-lijnen?

| Lijn | Volledige naam | Richting | Functie |
|---|---|---|---|
| `SCK` | Serial Clock | STM32 -> MCP23S17 | Geeft het kloksignaal |
| `MOSI` | Master Out Slave In | STM32 -> MCP23S17 | Stuurt data naar de MCP |
| `MISO` | Master In Slave Out | MCP23S17 -> STM32 | Stuurt data terug naar de STM32 |
| `CS` | Chip Select | STM32 -> MCP23S17 | Selecteert de slave |

### 3.3 Hoe werkt CS?

| CS-niveau | Betekenis |
|---|---|
| Hoog | MCP23S17 is niet geselecteerd |
| Laag | MCP23S17 is actief en luistert naar SPI |

Verloop:

1. STM32 zet CS laag.
2. SPI-gegevens worden verstuurd.
3. STM32 zet CS opnieuw hoog.

### 3.4 Hoe ziet een MCP23S17 SPI-transactie eruit?

| Byte | Inhoud | Functie |
|---|---|---|
| 1 | Opcode | Lezen of schrijven |
| 2 | Registeradres | Welk register je aanspreekt |
| 3 | Data of dummy-byte | Schrijfwaarde of klokpuls bij lezen |

Opcodes:

| Opcode | Betekenis |
|---|---|
| `0x40` | Schrijven |
| `0x41` | Lezen |

### 3.5 Hoe configureer je registers voor dit project?

| Register | Waarde | Functie |
|---|---|---|
| `IODIRA` | `0x00` | PORTA als outputs |
| `IODIRB` | `0xFF` | PORTB als inputs |
| `GPPUB` | `0x0F` | Pull-ups op de rijen |
| `OLATA` | `0xFF` | Kolommen hoog in rust |

### 3.6 Hoe schrijf je naar GPIOA en lees je van GPIOB?

- Naar de kolommen schrijven gebeurt via `OLATA`.
- De rijen lezen gebeurt via `GPIOB`.
- De actieve kolom wordt laag gemaakt, de andere blijven hoog.
- Als een knop ingedrukt is, wordt de overeenkomstige rij laag gelezen.

### 3.7 Welke registers zijn belangrijk en wat doen ze?

| Register | Functie in dit project |
|---|---|
| `IODIRA` | Bepaalt dat PORTA outputs zijn |
| `IODIRB` | Bepaalt dat PORTB inputs zijn |
| `GPPUB` | Activeert pull-ups op de rij-ingangen |
| `OLATA` | Stuurt de kolommen aan |
| `GPIOB` | Leest de rijtoestand in |

## 4. Contactdender en debouncing

### 4.1 Wat is contactdender?

- Mechanische contacten schakelen niet perfect in één keer.
- Bij indrukken of loslaten botsen de contactpunten kort.
- Daardoor wisselt het signaal enkele milliseconden snel tussen hoog en laag.

| Situatie | Ideaal signaal | Werkelijk signaal |
|---|---|---|
| Indrukken | `0 -> 1` | `0 -> 1 -> 0 -> 1 -> 1` |
| Loslaten | `1 -> 0` | `1 -> 0 -> 1 -> 0 -> 0` |

### 4.2 Waarom is debouncing nodig?

- Zonder debounce kan één druk meerdere Note On-berichten veroorzaken.
- Zonder debounce kan één loslating meerdere Note Off-berichten veroorzaken.
- Debouncing zorgt voor stabiele en betrouwbare MIDI-uitvoer.

| Zonder debounce | Met debounce |
|---|---|
| Meerdere events per druk | Eén correct event per overgang |
| Instabiele MIDI-uitvoer | Betrouwbare MIDI-uitvoer |

### 4.3 Hoe werkt timer-gebaseerde debounce?

- De matrix wordt niet continu verwerkt.
- De software scant enkel om de 5 ms.
- Dat gebeurt via `HAL_GetTick()` en een vergelijking met `last_scan`.
- Snelle denderpulsen tussen twee scans worden zo meestal genegeerd.

### 4.4 Hoe worden toestandsveranderingen gedetecteerd?

De vorige toestand van elke knop wordt bijgehouden in `matrix_prev[16]`.

| Vorige toestand | Huidige toestand | Actie |
|---|---|---|
| 0 | 0 | Niets doen |
| 0 | 1 | Note On |
| 1 | 1 | Niets doen |
| 1 | 0 | Note Off |

### 4.5 Wat is het verschil tussen toestand opvragen en verandering detecteren?

| Begrip | Betekenis | Resultaat |
|---|---|---|
| Toestand opvragen | Alleen lezen of knop nu hoog of laag is | Zelfde status kan blijven terugkomen |
| Verandering detecteren | Vergelijken met vorige toestand | Alleen reageren bij overgang |

Voor MIDI is verandering detecteren noodzakelijk, omdat Note On en Note Off gebeurtenissen zijn en geen continue statusmeldingen.

## 5. Matrixscanning

### 5.1 Hoe maakt een 4x4 matrix 16 knoppen mogelijk met 8 lijnen?

| Matrix | Aantal lijnen | Aantal knoppen |
|---|---:|---:|
| 4 rijen + 4 kolommen | 8 | 16 |

Principe:

- Elke knop zit op een kruispunt van een rij en een kolom.
- Je activeert telkens één kolom.
- Daarna kijk je welke rij reageert.
- Zo kan je het juiste kruispunt en dus de juiste knop bepalen.

### 5.2 Welke poort wordt gebruikt voor rijen en welke voor kolommen?

| Poort | Functie | Reden |
|---|---|---|
| `PORTA` | Kolommen | Wordt actief aangestuurd |
| `PORTB` | Rijen | Wordt ingelezen als input |

### 5.3 Wat is de volledige scanprocedure stap voor stap?

1. Controleer of USB gemount is.
2. Controleer of het 5 ms scaninterval verstreken is.
3. Maak één kolom laag via `OLATA`.
4. Wacht kort zodat het signaal zich stabiliseert.
5. Lees de rijen via `GPIOB`.
6. Zet alle kolommen terug hoog.
7. Bepaal per rij of een knop ingedrukt is.
8. Vergelijk met de vorige toestand.
9. Stuur indien nodig Note On of Note Off.
10. Sla de nieuwe toestand op.

### 5.4 Hoe wordt de MCP23S17 geïnitialiseerd voor matrix scanning?

```c
mcp_write(MCP_IODIRA, 0x00);
mcp_write(MCP_IODIRB, 0xFF);
mcp_write(MCP_GPPUB,  0x0F);
mcp_write(MCP_OLATA,  0xFF);
```

| Regel | Betekenis |
|---|---|
| `IODIRA = 0x00` | Kolommen als outputs |
| `IODIRB = 0xFF` | Rijen als inputs |
| `GPPUB = 0x0F` | Pull-ups op de rij-ingangen |
| `OLATA = 0xFF` | Kolommen hoog in rust |

### 5.5 Hoe sturen SPI-transacties de kolommen aan en lezen ze rijen in?

- Met `mcp_write(...)` stuurt de STM32 een nieuw patroon naar `OLATA`.
- Daardoor wordt één kolom laag gemaakt.
- Met `mcp_read(...)` leest de STM32 daarna `GPIOB` uit.
- Zo weet de software welke rij actief laag werd.

### 5.6 Hoe worden toestandswijzigingen opgeslagen voor MIDI?

- Voor elke knop wordt de vorige toestand onthouden in `matrix_prev[]`.
- Alleen bij een overgang wordt een MIDI-bericht gestuurd.
- Daardoor wordt één druk één Note On en één loslating één Note Off.

### 5.7 Wat is ghosting?

Ghosting is een fout bij matrixscanning waarbij een niet-ingedrukte knop toch als actief gedetecteerd wordt.

| Oorzaak | Gevolg |
|---|---|
| Meerdere knoppen tegelijk ingedrukt | Mogelijke valse detectie |
| Geen diodes in de matrix | Alternatieve stroompaden mogelijk |
| Rechthoekpatroon van ingedrukte toetsen | Vierde hoek kan foutief actief lijken |

### 5.8 Waarom is ghosting belangrijk om te vermelden?

- Het is een gekende beperking van matrixscanning zonder diodes.
- Het verklaart waarom bepaalde meerknopscombinaties foutief kunnen reageren.
- In eenvoudige toepassingen is dat vaak aanvaardbaar, maar technisch moet je het kunnen benoemen.

## Korte eindsamenvatting

- Punt 1 gaat over de volledige architectuur van knop tot MIDI-bericht.
- Punt 2 gaat over MIDI-berichten, Note On/Off, USB MIDI en de code die berichten opbouwt.
- Punt 3 gaat over SPI-communicatie en de rol van de MCP23S17-registers.
- Punt 4 gaat over contactdender, debouncing en het detecteren van overgangen.
- Punt 5 gaat over matrixscanning, poorten, scanstappen, statusopslag en ghosting.

Met dit document heb je een compacte maar volledige samenvatting van alle theorievragen die je tijdens de voorbereiding hebt gesteld.