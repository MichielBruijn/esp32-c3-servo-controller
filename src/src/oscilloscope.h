/*
  A handy oscilloscope for 0-3.3V RC signals natively on 2 switchable probe inputs,
  up to ~5.4V per probe with its own external voltage divider (see voltMaxByChannel[])
*/

#include "Arduino.h"
#include "adcLookup.h"

//
// =======================================================================================================
// GLOBAL VARIABLES
// =======================================================================================================
//

const int displayWidth = 128;

const int oneSampleCalibration = 71; // Allows to fine adjust the pulsewith & frequency readings (About 71 µs)

int timeBase = 0;      // define a variable for the x delay (90 for 50 Hz, 10 for 333, 0 for 1520)
int samplingDelay = 0; // define a variable for the X scale / delay
int sampleNo = 0;      // define a variable for the sample number used to collect x position into array
int sampleHi = 0;      // Samples, which are above trigger level
int posX = 0;          // define a variable for the sample number used to write x position on the LCD

// array parameters
#define arraySize displayWidth // how much samples are in the array
int myArray[arraySize];        // define an array to hold the data coming in
int arrayMin = 0;
int arrayAverage = 0;
int arrayMax = 0;
Array<int> samplingArray = Array<int>(myArray, arraySize);

// OLED scaling and offset
int OledScaleX = 1;
int scaleY = 85;        // define a variable for the y scale (85) adcmax/scaleY = bottom end
int scopeOffsetY = -13; // shifted 13 pixels downwards to make room for text

// Voltage measuring
float voltMaxByChannel[2] = {5.45, 5.45}; // Assumes a 16.2k/24.9k voltage divider on both OSCILLOSCOPE_PIN and OSCILLOSCOPE_PIN2 (5V -> 3.03V each), scaled back up for the vPP readout
int voltCalPermille[2] = {1000, 1000};    // Per-probe fine trim on top of voltMaxByChannel (x1000, e.g. 1000 = 1.000x), for divider resistor tolerance / ADC gain error. Persisted to EEPROM, adjustable live in the Oscilloscope screen
int adcMax = 4095; // the maximum reading from the ADC 1023, but 4095 on ESP32!
float vPP = 0;     // peak to peak voltage

// Frequency & pulsewidth measuring
unsigned long startSampleMicros = 0;
unsigned long endSampleMicros = 0;
unsigned long oneSampleDuration = 0;
float signalFrequency1 = 0;
float signalFrequency = 0;
uint32_t pulseWidth1 = 0;
uint32_t pulseWidth = 0;
uint32_t averagingPasses = 10;

// trigger
int triggerLevel = 2048;
byte triggerMode = 0;

// Probe channel switching
int oscChannel = 0; // 0 = primary probe (OSCILLOSCOPE_PIN / ADC1_CHANNEL_3), 1 = secondary probe (OSCILLOSCOPE_PIN2 / ADC1_CHANNEL_6)

adc1_channel_t currentOscAdcChannel()
{
  return (oscChannel == 0) ? ADC1_CHANNEL_3 : ADC1_CHANNEL_6;
}

// Display
unsigned long popupMillis;
byte popupMode = 0; // 0 = adjustable-value popup (see oscAdjustMode), 1 = probe switch popup
bool takeNewSamples;

// What the encoder currently adjusts, cycled with a short button press - like a real scope's
// single "menu" knob doubling for several settings
byte oscAdjustMode = 0; // 0 = sampling delay (timebase), 1 = trigger level, 2 = per-probe voltage calibration

// menu
byte menu = 1; // the current menu item

//
// =======================================================================================================
// ADSJUST ADC TIMEBASE
// =======================================================================================================
//
void adjustADC()
{
  if (buttonState == 2) // Short press: cycle what the encoder adjusts (timebase / trigger / voltage cal)
  {
    oscAdjustMode = (oscAdjustMode + 1) % 3;
    popupMode = 0;
    popupMillis = millis();
  }

  if (encoderState == 1 || encoderState == 2)
  {
    int dir = (encoderState == 1) ? -1 : 1;
    switch (oscAdjustMode)
    {
    case 0:
      samplingDelay += dir * 4;
      break;
    case 1:
      triggerLevel += dir * 64;
      break;
    case 2:
      voltCalPermille[oscChannel] += dir * 5; // 0.5% steps
      break;
    }
    popupMode = 0;
    popupMillis = millis();
  }

  samplingDelay = constrain(samplingDelay, 0, 300); // 160 for 50Hz
  triggerLevel = constrain(triggerLevel, 100, 3990); // stay clear of the trigger-wait loops' strict 0/4095 edges
  voltCalPermille[oscChannel] = constrain(voltCalPermille[oscChannel], 800, 1200); // +/-20%, plenty for resistor tolerance/ADC gain error
}

//
// =======================================================================================================
// SWITCH PROBE CHANNEL
// =======================================================================================================
//
void switchOscChannel()
{
  oscChannel = !oscChannel;
  popupMode = 1;
  popupMillis = millis();
}

//
// =======================================================================================================
// READ PROBE
// =======================================================================================================
//

void readProbe()
{
  if (takeNewSamples) // Only take samples, if required
  {
    // takeNewSamples = false; // Testing only: PPM triggering is unstable this way, TODO
    portDISABLE_INTERRUPTS();
    // wait until threshold hits trigger level -------------------------------------------------
    sampleNo = 0;
    adc1_channel_t adcCh = currentOscAdcChannel();
#if defined FAST_ADC
    while ((local_adc1_read(adcCh) < triggerLevel) && (sampleNo < 2500)) // We have to wait long enough for 50Hz or PPM, so longer than arraySize
#else
    while ((adc1_get_raw(adcCh) < triggerLevel) && (sampleNo < 2500)) // We have to wait long enough for 50Hz or PPM, so longer than arraySize
#endif
    {
      sampleNo++; // proceed after reaching array end, if no trigger level was detected!
    }
    sampleNo = 0;
#if defined FAST_ADC
    while ((local_adc1_read(adcCh) > triggerLevel) && (sampleNo < 2500)) // We have to wait long enough for 50Hz or PPM, so longer than arraySize
#else
    while ((adc1_get_raw(adcCh) > triggerLevel) && (sampleNo < 2500)) // We have to wait long enough for 50Hz or PPM, so longer than arraySize
#endif
    {
      sampleNo++;
    }

    // fill the array as fast as possible with samples (therefore we have as less code as possible in this for - loop)

    // read samples and store them in array ---------------------------------------------------
    startSampleMicros = esp_timer_get_time();
    sampleNo = 0;
    while (sampleNo < arraySize)
    {
#if defined FAST_ADC
      myArray[sampleNo] = local_adc1_read(adcCh); // Super fast custom analogRead() alterantive
#else
      myArray[sampleNo] = adc1_get_raw(adcCh);                        // slower, but also working, if WiFi is disabled
#endif
      sampleNo++;
      int64_t m = esp_timer_get_time(); // slim replacement vor delayMicroseconds()
      while (esp_timer_get_time() < m + samplingDelay)
      {
        NOP();
      }
    }
    endSampleMicros = esp_timer_get_time();
    portENABLE_INTERRUPTS();

    // Calculate the required time for taking one sample -------------------------------------
    oneSampleDuration = ((endSampleMicros + oneSampleCalibration - startSampleMicros) / arraySize);

    // calculate signal frequency using trigger level crossing
    sampleNo = 0;
    sampleHi = 0;
    while (myArray[sampleNo] < triggerLevel && (sampleNo < (arraySize - 1)))
      sampleNo++;
    while (myArray[sampleNo] > triggerLevel && sampleNo < (arraySize - 1))
    {
      sampleNo++;
      sampleHi++; // Samples above zero crossing for pulsewidth calculation
    }
    if (sampleHi > 0)
    {
      signalFrequency1 = (1000000L / (sampleNo * oneSampleDuration)); // Hz = full waves per one million microseconds
      signalFrequency = (signalFrequency * (averagingPasses - 1) + signalFrequency1) / averagingPasses;
    }
    else
    {
      signalFrequency = 0;
    }
    pulseWidth1 = sampleHi * oneSampleDuration; // Pulsewidth calculation
    pulseWidth = (pulseWidth * (averagingPasses - 1) + pulseWidth1) / averagingPasses;

    // Calibrate & invert the whole array -----------------------------------------------------
    for (sampleNo = 0; sampleNo < arraySize; sampleNo++)
    {
#if defined ADC_LINEARITY_COMPENSATION
      myArray[sampleNo] = (int)ADC_LUT[myArray[sampleNo]]; // get the calibrated value from Lookup table
#endif
      myArray[sampleNo] = (adcMax - myArray[sampleNo]);
    }

    // compute array min, average, max values ------------------------------------------------
    arrayMin = samplingArray.getMin();
    arrayMax = samplingArray.getMax();
    arrayAverage = samplingArray.getAverage();
    vPP = (arrayMax - arrayMin) * voltMaxByChannel[oscChannel] * voltCalPermille[oscChannel] / 1000.0 / adcMax;
  }
}

//
// =======================================================================================================
// DRAW LCD
// =======================================================================================================
//

void drawDisplay()
{

  static unsigned long previousUpdate;

  if (millis() - previousUpdate >= 100)
  { // every 100 ms
    previousUpdate = millis();

    // clear old display contentmt
    display.clear();

    // draw reading line
    for (posX = 1; posX < displayWidth; posX += 1) // looks better, if posX = 0 is not displayed!
    {                                              // for loop draws displayWidth pixels
      display.drawLine((posX * OledScaleX) - (OledScaleX - 1), myArray[posX - 1] / scaleY - scopeOffsetY, posX * OledScaleX, myArray[posX] / scaleY - scopeOffsetY);
    }

    // draw reference lines
    for (sampleNo = 0; sampleNo < displayWidth; sampleNo += 4)
    {
      display.setPixel(sampleNo, (adcMax / scaleY) - scopeOffsetY);     // Draw 5V Line
      display.setPixel(sampleNo, (adcMax / scaleY / 2) - scopeOffsetY); // Draw 2,5V Line
      display.setPixel(sampleNo, 0 - scopeOffsetY);                     // Draw 0V Line
    }

    // draw array min, average, max lines
    for (sampleNo = 0; sampleNo < displayWidth; sampleNo += 10)
    {
      // display.setPixel(sampleNo, (arrayMin / scaleY) - scopeOffsetY);      // Draw min Line
      display.setPixel(sampleNo, (arrayAverage / scaleY) - scopeOffsetY); // Draw average Line
      // display.setPixel(sampleNo, (arrayMax / scaleY) - scopeOffsetY);      // Draw max Line

      // draw trigger level marking
      display.drawLine(2, (adcMax - triggerLevel) / scaleY - scopeOffsetY, 4, (adcMax - triggerLevel) / scaleY - scopeOffsetY);
    }

    // Readings on top of display
    display.setFont(ArialMT_Plain_10);
    display.setTextAlignment(TEXT_ALIGN_RIGHT);
    display.drawString(displayWidth, 0, String(vPP) + "vPP"); // Show peak to peak voltage

    if (triggerLevel > 0)
    { // if trigger is active
      display.setTextAlignment(TEXT_ALIGN_CENTER);
      display.drawString(displayWidth / 2, 0, String(signalFrequency, 0) + "Hz"); // Show signal frequency
    }

    // Left side has the most free room, so the active probe indicator goes here (TEXT_ALIGN_RIGHT
    // grows leftward, so putting it there instead would push it into the centered Hz text)
    display.setTextAlignment(TEXT_ALIGN_LEFT);
    display.drawString(0, 0, "P" + String(oscChannel + 1) + " " + String(pulseWidth) + "µs"); // Show active probe + pulsewidth

    // Popup window
    if (millis() - popupMillis < 1500)
    {
      display.setTextAlignment(TEXT_ALIGN_CENTER);
      display.setColor(BLACK);
      display.fillRect(25, 22, 78, 29); // Clear area behind window
      display.setColor(WHITE);
      display.drawRect(25, 22, 78, 29); // Draw window frame
      if (popupMode == 1)
      {
        display.drawString(64, 25, "Probe " + String(oscChannel + 1));
        display.drawString(64, 35, "Pin " + String(oscChannel == 0 ? OSCILLOSCOPE_PIN : OSCILLOSCOPE_PIN2));
      }
      else
      {
        switch (oscAdjustMode)
        {
        case 0:
          display.drawString(64, 25, "Sampling delay");
          display.drawString(64, 35, String(samplingDelay) + " µs");
          break;
        case 1:
          display.drawString(64, 25, "Trigger level");
          display.drawString(64, 35, String(triggerLevel));
          break;
        case 2:
          display.drawString(64, 25, "Volt cal P" + String(oscChannel + 1));
          display.drawString(64, 35, String(voltCalPermille[oscChannel] / 1000.0, 3) + "x");
          break;
        }
      }
    }

    // refresh display content
    display.display();
    takeNewSamples = true;
  }
}

//
// =======================================================================================================
// MAIN OSCILLOSCOPE LOOP
// =======================================================================================================
//

void oscilloscopeLoop(bool init)
{
  if (init)
  {
    // adcAttachPin() takes a GPIO pin number, not an ADC1_CHANNEL_x enum - the original code passed
    // the channel enum here (numerically 4), silently attaching the wrong GPIO (4, the buzzer pin)
    // instead of the actual scope pin. Fixed while restoring: pass the real pin number.
    adcAttachPin(OSCILLOSCOPE_PIN);
    adcAttachPin(OSCILLOSCOPE_PIN2);
    // adc1_get_raw(ADC1_CHANNEL_3); // required for correct ADC configuration
    samplingDelay = 160;    // Set ideal delay for standard RC Signals (132 for analogRead, 160 for adc1_get_raw)
    popupMode = 0;
    popupMillis = millis(); // Show popup
  }

  adjustADC();

  readProbe();

  drawDisplay();
}
