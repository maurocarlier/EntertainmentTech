# Verslag Opdracht 3: ADC Potentiometers – MIDI Control Change

## 1. Pin-toewijzing en Fysieke Aansluiting

Voor deze configuratie zijn de potentiometers als volgt aangesloten op de STM32 Nucleo:
* **Fysieke Bedrading (per Potentiometer):**
  * **Linker buitenste pin:** Aangesloten op **3.3V** (*Gebruik geen 5V ivm schadegevaar bij de ADC pinnen*).
  * **Rechter buitenste pin:** Aangesloten op **GND** (Massa).
  * **Middelste pin (Signaal):** Aangesloten op **PA0** en **PA1**.

* **Verloop van het Digitale Signaal (Hardware naar MIDI):**
  * **Potentiometer 1:** Pin **PA0** is intern gekoppeld aan **ADC1 Kanaal 0**. Onze code wijst hier in zijn Array-locatie de startwaarde aan toe (`16 + 0`) → Zender produceert **MIDI CC 16**.
  * **Potentiometer 2:** Pin **PA1** is intern gekoppeld aan **ADC1 Kanaal 1**. Onze code wijst hier `16 + 1` aan toe → Zender produceert **MIDI CC 17**.

## 2. CubeMX Configuratie

Om deze opstelling correct hardwarematig aan te sturen zonder de CPU te belasten, zijn de volgende modules ingesteld en op elkaar afgestemd in STM32CubeMX:
* **TIM6 (Trigger):** Deze Basic Timer verzorgt een strak ritme van 1 kHz als interne pacemaker (Prescaler 249, Period 999 bij een 250MHz klok). Via de Trigger Output (TRGO) op een *Update Event* laat hij de ADC continu weten wanneer de volgende sample mag gebeuren.
* **ADC1 (Uitlezer):** 
  * *Resolutie:* Beperkt tot 8 bits in plaats van de gebruikelijke 12 bits, aangezien ons einddoel beperkt is tot MIDI's 7-bit Control Change (waarden 0 t.e.m. 127).
  * *Scan Conversion Mode:* Geactiveerd (`Enabled`) zodat hij meerdere meetpunten (Ranks) onmiddellijk in één getriggerde cyclus kan verwerken. We maken gebruik van 2 conversies: Rank 1 (Kanaal 0) en Rank 2 (Kanaal 1). 
  * *External Trigger:* Ingesteld op de binnenkomende puls van TIM6. 
* **GPDMA1 (Geheugen Verplaatser):** Dit DMA-kanaal zit aan de ADC1 peripheral vast in *Circular Mode* (vervoert data van Peripheral to Memory in een oneindige lus). De optie *Destination Address Increment* staat ingeschakeld, zodat Kanaal 0 en Kanaal 1 niet over elkaar heen schrijven in het geheugen, maar netjes opeenvolgend als bytes in onze gealloceerde `adc_buffer`-array worden geplaatst.

## 3. Broncode en Implementatie

Hieronder staat een overzicht van de geschreven code voor Fase 2 (met 2 potentiometers) in `main.c`, verdeeld in logische blokken met telkens een verklaring van wat de code doet.

### 3.1 Defines en Globale Variabelen
```c
// ADC Potentiometers (Fase 2)
#define NUM_POTS       2    // Aantal potentiometers in gebruik
#define MIDI_CC_START  16   // Eerste CC nummer (dit wordt CC16 en CC17)
#define HYSTERESIS     4    // Jitter filter drempel
#define DEADZONE_LOW   20   // Ruwe waarde (0-255) eronder wordt 0
#define DEADZONE_HIGH  240  // Ruwe waarde (0-255) erboven wordt 255

volatile uint8_t adc_buffer[NUM_POTS];
uint8_t last_midi_value[NUM_POTS] = {0};
```
**Uitwerking:**
* **`NUM_POTS` en `MIDI_CC_START`:** Om eenvoudig uit te breiden is het aantal potmeters configureerbaar gemaakt. Vanaf basisadres 16 (CC16) krijgt elke extra pin een oplopend bedieningsnummer door compiler logica.
* **`HYSTERESIS` en `DEADZONE`:** Tijdens het testen bleek de hardware onderhevig aan elektrische ruis ("jitter") en haalden de potentiometers hun 'absolute nul'-stand niet wegens interne afwijkingen. Hierop stelden we robuuste beveiligingsmarges en een kalibratie deadzone in.
* **`adc_buffer`:** Is gedeclareerd als `volatile` en in vorm van een Array (`adc_buffer[2]`). Zodat het DMA (Direct Memory Access) proces direct en automatisch per cyclus beide metingen in deze array wegschrijft, zonder dat de CPU dit steeds handmatig moet afhandelen of dat de compiler het wegoptimaliseert.

### 3.2 Initiëren van de Hardware (Timer en ADC/DMA)
```c
void ADC_Start(void) {
    HAL_TIM_Base_Start(&htim6);
    HAL_ADC_Start_DMA(&hadc1, (uint32_t*)adc_buffer, NUM_POTS);
}
```
**Uitwerking:**
* **`HAL_TIM_Base_Start`:** We ontwaken en starten interne hardware-Timer 6 (1 kHz). Deze functioneert als de interne pacemaker (Trigger) om de analoge inlezing exact en strak te timen.
* **`HAL_ADC_Start_DMA`:** We activeren het inlezen op ADC1. Door in DMA-Modus te opereren, vertellen we het toestel dat de verzamelde bits rechtstreeks naar het adres van `adc_buffer` overgepompt moeten worden. Omdat `NUM_POTS` op 2 staat, haalt de "Scan Mode" beide pinnen per cyclus binnen.

### 3.3 Data verwerken & MIDI uitsturen
```c
void process_potentiometers(void) {
    for (int i = 0; i < NUM_POTS; i++) {
        uint16_t raw_val = adc_buffer[i];

        // Pas deadzones en lineaire mapping toe om te zorgen dat elke slider 
        // hardware-onafhankelijk altijd perfect van 0 tot 255 uitvalt
        if (raw_val <= DEADZONE_LOW) {
            raw_val = 0;
        } else if (raw_val >= DEADZONE_HIGH) {
            raw_val = 255;
        } else {
            raw_val = ((raw_val - DEADZONE_LOW) * 255) / (DEADZONE_HIGH - DEADZONE_LOW);
        }

        uint8_t new_value = raw_val >> 1; // 8-bit geshift naar 7-bit (0 tot 127)

        int16_t diff = (int16_t)new_value - (int16_t)last_midi_value[i];        
        if (diff < 0) diff = -diff;

        // Als we verschil detecteren *OF* de potmeter staat op 0 (in deadzone) 
        // en dit was nog niet uitgezonden (Voorkomt Hysteresis blokkade naar 0 toe op eindpunt).
        if (diff >= HYSTERESIS || (new_value == 0 && last_midi_value[i] != 0)) {
            if (new_value != last_midi_value[i]) {
                if (tud_midi_mounted()) {
                    uint8_t msg[3] = { 0xB0, MIDI_CC_START + i, new_value };        
                    tud_midi_stream_write(0, msg, 3);
                }
                last_midi_value[i] = new_value;
            }
        }
    }
}
```
**Uitwerking:**
1. **Loop:** Gaat af aan de hand van het aantal potmeters. De inkomende DMA waarde per pin wordt opgeslagen als `raw_val`.
2. **Deadzone Correctie:** Lage afwijkingen kleiner dan `20` worden hardwarematig geforceerd naar `0`, en waarden boven `240` worden geforceerd naar `255` om zuivere uitersten te bereiken.
3. **Conversie en Ruis-Filtering (Hysteresis):** Na het vertalen (`>> 1` shift) van 8-bit naar MIDI 7-bit schaal (0-127), rekent het blok het absolute verschil tussen de vorige verzonden positie en huidige inlezing uit. Dankzij de regel dat de sprong groter moet zijn dan de drempel `HYSTERESIS (4)`, raakt de hardware verlost van zogenoemde "Jitter" en rustig zwevende spanningsschommelingen. 
4. **Verzending:** Voldoet deze drempel, wordt via USB via `tud_midi_stream_write` de `0xB0` control change payload succesvol opgestuurd. Onze kleine `(new_value == 0)` safety-net verzekert dat we het signaal bij nulpunt zeker muten in de deadzone en hij nooit op 1 kan blijven haperen door een hysteresis val.

---

## 4. Antwoorden op de extra vragen

**Vraag 1: De shift-operatie `adc_value >> 1` zet 8-bit om naar 7-bit. Welke resultaatwaarde krijg je maximaal bij 255 en bij 254? Is dit een probleem?**
* **Bij `adc_value = 255`**: Binair is dit `1111 1111`. Een bitshift per positie naar rechts (`>> 1`) geeft `0111 1111`, wat decimaal neerkomt op exact **127**.
* **Bij `adc_value = 254`**: Binair is dit `1111 1110`. Dezelfde bitshift (`>> 1`) knipt ook hier de laatste Least-Significant bit af. Dit levert ons wederom `0111 1111` op, oftewel eveneens **127**.
* *Is dit een probleem?* Nee, dit vormt totaal geen risico. Zowel voorwaarde `254` als `255` vertalen netjes naar het MIDI maximum van `127`. Het gevolg hiervan is simpelweg dat we de uiterste maximale output iets vlotter bereiken waardoor lichte dipjes op de maximale potentiometerstand niet direct het signaal onrustig naar 126 droppen.

**Vraag 2: HYSTERESIS is ingesteld op 2. Stel dat de potmeter stilstaat maar de waarde schommelt tussen 100 en 101. Worden er berichten verstuurd? En bij schommelingen van 100 tot 103? Leg uit.**
* Het algoritme rekent pas een nieuw event na wanneer het verschil (de variabele `diff`) groter of minstens even groot (`>=`) is dan de vastgestelde `HYSTERESIS`. 
* **Schommeling 100-101**: Het verschil is hier `1`. Aangezien `1 < 2` wordt er niet aan de conditie voldaan en blijft de controller in rust (er wordt **géén** bericht doorgestuurd). Het jitter-bedrag valt dood in de filter.
* **Schommeling 100-103**: In deze situatie bedraagt de fluctuatie of het onderlinge verschil `3`. Hier is de drempel ruimschoots behaald (`3 >= 2`). Dit betekent dat het conditionele blok open slaat en de controller **wél** een hele stroom ongewilde MIDI berichten ('noise') zal uitsturen tussen de standen 100 en 103. (Dit is overigens de reden waarom wij de Hysteresis in de code naar vier trokken).

**Vraag 3: In `ADC_Start()` wordt eerst `HAL_TIM_Base_Start` aangeroepen en daarna pas de DMA (`HAL_ADC_Start_DMA`). Waarom is dit belangrijk?**
* In deze keten genereert Timer 6 (TIM6) een regelmatige TRGO *trigger-puls* op vaststaande intervallen. Deze output wordt opgepikt door het trigger-mechanisme van de Analog to Digital Converter om te laten weten "wanneer" er mag ingelezen/gesampeld worden. 
Wanneer men eerst de Timer start en daarna de ADC + DMA klaarmaakt om op deze commando's in te luisteren, sluit de cyclus perfect aan op een reeds voorspelbare en strak draaiende interne klok. Zou je eerst de ADC met zijn DMA kanalen opstarten en daarna pas het trigger-kanaal openen, loop je kans op een race conditie waar de DMA in verwarring raakt in afwachting van 'niets', of kan de eerste 'trigger' asynchroon uitdraaien en missynchroniseren.

**Vraag 4: De main-loop roteert met een hogere frequentie dan dat de ADC op 1kHz data inleest. Worden er op deze versnelde cyclus dubbele MIDI berichten verstuurd? Leg uit.**
* Nee, er zal nimmer een "dubbel" bericht op de lijn verschijnen. Dit doordat het zend-statuut afgeschermd achter een lokale 'watchdog' array staat: de `last_midi_value[i]` lijst. Zelfs in scenario's waarbij de processor de `while(1)`-lus duizenden keren doorloopt tussen twee afzonderlijke ADC sampelingperiodes door, zal de conditie `if (new_value != last_midi_value[i])` nagaan of de laatst gezonde MIDI-waarde uit de geschiedenis-tabel identiek was aan de nieuw inkomende, hierop verwerpt hij acties die dubbel willen registreren, tenzij de waarde opnieuw veranderd is.

---

## 5. Te Voorzien door de Student (Bijlagen)
* [ ] **Screenshot inleveren:** Voeg hiernaast nog een screenshot in van *MIDI Monitor / MIDI-OX* waarin te zien is dat CC16 en CC17 gelijktijdig signaal afgeven.
* [ ] **Demovideo link:** Lever jouw bewijsvideo aan op de afgesproken plek via ufora / Toledo.