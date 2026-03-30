# Verslag Opdracht 3: ADC Potentiometers – MIDI Control Change

## 1. Pin-toewijzing
Voor deze configuratie zijn de potentiometers als volgt aangesloten op de STM32:
* **Potentiometer 1:** Pin **PA0** → ADC1 Kanaal 0 → MIDI CC 16
* **Potentiometer 2:** Pin **PA1** → ADC1 Kanaal 1 → MIDI CC 17

## 2. Bijzonderheden in de Implementatie (Broncode)
Tijdens het testen en aansluiten van de componenten bleek de hardware onderhevig te zijn aan elektrische ruis ("jitter"), bovendien haalden de potentiometers hun 'absolute nul'-stand niet wegens interne afwijkingen bij lage weerstand.

Om dit wiskundig te corrigeren zijn er twee specifieke softwareverbeteringen geïmplementeerd die niet in het standaardontwerp zaten:
1. **Deadzone Mapping:** De ruwe 8-bit analoogwaardes onder `20` worden hardwarematig geforceerd naar `0`, en waarden boven `240` worden geforceerd naar `255`. Hierdoor sturen beide schuivers ongeacht fabrieksfouten of weerstand in hun massakabel altijd loepzuiver een `0` of een `127` midi signaal door in de eindstanden.
2. **Hysteresis filter + Deadzone Override:** De Hysteresis parameter is verhoogd naar `4` in combinatie met een 'fall-to-zero safety'. Als de berekening detecteerde dat de deadzone was geraakt (zuivere 0-waarde), telt deze sterker door dan de hysteresis check die anders deze laatste kleine stap over het hoofd had gemaskerd.

### Broncode `process_potentiometers`
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

---

## 3. Antwoorden op de extra vragen

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

## 4. Te Voorzien door de Student (Bijlagen)
* [ ] **Screenshot inleveren:** Voeg hiernaast nog een screenshot in van *MIDI Monitor / MIDI-OX* waarin te zien is dat CC16 en CC17 gelijktijdig signaal afgeven.
* [ ] **Demovideo link:** Lever jouw bewijsvideo aan op de afgesproken plek via ufora / Toledo.