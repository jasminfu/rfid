/*
This code is written for the Adafruit Feather RP2040 Adalogger to work with a WL-134 module,
DS3231 RTC, an 128x64 OLED monitor with a SH1107 driver, and a MPM3610 5V buck.
The code logs detections of 134.2khZ RFID tags into a csv file, which is saved onto an SD card.
The device has a sleep function with sleep and wake times that can be modified.
The OLED monitor allows users to check device functionality (whether the RTC and SD card are working).
Please see GitHub Page for instructions on how to work with the code and corresponding hardware. (https://jasminfu.github.io/rfid/)
*/

#include <Arduino.h>
#include <SPI.h>
#include "SdFat.h"
#include <Wire.h>
#include <RTClib.h>
#include "pico/stdlib.h"
#include "hardware/gpio.h"
#include "hardware/xosc.h"
#include "hardware/clocks.h"
#include "hardware/watchdog.h"
#include "hardware/rtc.h"
#include <Adafruit_GFX.h>
#include <Adafruit_SH110X.h>
#include <math.h>

#define EN_PIN 12
#define CS_PIN 23
#define WAKE_PIN 25
#define BTN_A 9
#define BTN_B 6
#define BTN_C 5

// OLED Variables
Adafruit_SH1107 display = Adafruit_SH1107(64, 128, &Wire);
volatile bool btnAPressed = false;
volatile bool btnBPressed = false;
volatile bool btnCPressed = false;
volatile bool oledPowerState = false;
volatile unsigned long oledOnTime = 0;
const unsigned long OLED_TIMEOUT = 20000;
int currentPage = 1;

// SD variables
SdFat SD;
SdSpiConfig config(CS_PIN, DEDICATED_SPI, SD_SCK_MHZ(16), &SPI1);

// MODIFY THIS LIST WITH YOUR CHECK TAG IDs
const char* checkTags[] = {
  "3A68D395D1",
  "D968D395D1",
  "0175348100",
  "9968D395D1",
  "8968D395D1",
  "1175348100",
  "6968D395D1",
  "6475348100"
};

// File name variables
const char* deviceID = "00";  // MODIFY THIS FOR EACH DEVICE
char housekeeping[32];
char rfidlog[32];

// Real Time Clock for keeping time
RTC_DS3231 rtc;

// Hourly sleep check variables
unsigned long lastCheck = 0;
const unsigned long checkInterval = 60000;
const unsigned long TIMEOUT_DURATION_MS = 21000;
unsigned long lastMotionTime;
String lastTag = "NONE";

// Default sleep times. startSleepTime should be bigger than endSleepTime
float startSleepTime = 21.75;  // 9:45 PM, MODIFY TO CHANGE SLEEP TIME
float endSleepTime = 5.5;      // 5:30 AM, MODIFY TO CHANGE WAKE TIME

// Boolean variables for monitoring device function. Will be set to true in setup if working.
bool rtcInit = false;
bool sdInit = false;
bool hkInit = false;
bool oledInit = false;

// Location variables for calculating sleep and wake times
const float LATITUDE = 33.9423;    // CHANGE THIS TO YOUR LOCATION's LATITUDE
const float LONGITUDE = -83.3724;  // CHANGE THIS TO YOUR LOCATION's LONGITUDE
const int TIMEZONE = -4;           // CHANGE THIS TO REFLECT YOUR TIME ZONE
const bool dynamicSleep = true;    // SET THIS TO 'false' IF YOU WANT STATIC SLEEP AND WAKE TIMES

// Display device status on OLED monitor
void updateDisplay() {
  display.clearDisplay();
  if (!oledPowerState) {
    display.display();  // Push the empty buffer
    display.oled_command(SH110X_DISPLAYOFF);
    return;
  }

  display.oled_command(SH110X_DISPLAYON);
  display.setCursor(0, 0);
  if (currentPage == 1) {
    display.println("Tag: " + lastTag);
    String sdSpaceMsg;
    if (sdInit) {
      uint32_t freeClusters = SD.vol()->freeClusterCount();
      uint32_t totalClusters = SD.vol()->clusterCount();

      // freeClusterCount() returns 0 on error
      if (freeClusters == 0 || totalClusters == 0) {
        sdSpaceMsg = "SD: ERROR";
      } else {
        float percentUsed = ((float)(totalClusters - freeClusters) * 100.0) / totalClusters;
        sdSpaceMsg = "SD: " + String(percentUsed, 2) + "%";
      }
    }
    display.println(sdSpaceMsg);
    String logStatus = "";
    FsFile rlFile = SD.open(rfidlog, FILE_WRITE);
    if (rlFile) {
      logStatus = "Log: OK";
      rlFile.close();
    } else {
      // Attempt recovery
      if (SD.begin(CS_PIN)) {
        logStatus = "Log: RECOVERED";
        sdInit = true;
      } else {
        logStatus = "Log: ERROR";
        sdInit = false;
      }
    }
    display.println(logStatus);
  } else {
    String timeString = "RTC: ERROR";
    if (rtcInit) {
      DateTime now = rtc.now();
      char timeBuffer[20];
      snprintf(timeBuffer, sizeof(timeBuffer), "%04d-%02d-%02d %02d:%02d",
               now.year(), now.month(), now.day(), now.hour(), now.minute());
      timeString = String(timeBuffer);
    }
    display.println(timeString);
    display.println("ID: " + String(deviceID));
  }
  display.display();
}

// Interrupt Handlers for buttons
void handleBtnA() {
  btnAPressed = true;
}
void handleBtnB() {
  btnBPressed = true;
}
void handleBtnC() {
  btnCPressed = true;
}

// Calculate wake up and sleep times depending on location and time zone
// DELETE IF USING STATIC SLEEP AND WAKE TIMES
float calculateSunTime(int year, int month, int day, bool isSunrise) {
  // 1. Calculate Day of Year (N)
  int N1 = floor(275 * month / 9);
  int N2 = floor((month + 9) / 12);
  int N3 = (1 + floor((year - 4 * floor(year / 4) + 2) / 3));
  int N = N1 - (N2 * N3) + day - 30;

  // 2. Convert Longitude to hour value and calculate approximate time (t)
  float lngHour = LONGITUDE / 15.0;
  float t = N + ((isSunrise ? 6.0 : 18.0) - lngHour) / 24.0;

  // 3. Sun's mean anomaly (M)
  float M = (0.9856 * t) - 3.289;

  // 4. Sun's true longitude (L)
  float L = M + (1.916 * sin(M * PI / 180)) + (0.020 * sin(2 * M * PI / 180)) + 282.634;
  L = fmod(L, 360.0);

  // 5. Sun's right ascension (RA)
  float RA = atan(0.91764 * tan(L * PI / 180)) * 180 / PI;
  RA = fmod(RA, 360.0);
  // Adjust quadrant
  float Lquadrant = floor(L / 90) * 90;
  float RAquadrant = floor(RA / 90) * 90;
  RA = (RA + (Lquadrant - RAquadrant)) / 15.0;

  // 6. Sun's declination (sinDec)
  float sinDec = 0.39782 * sin(L * PI / 180);
  float cosDec = cos(asin(sinDec));

  // 7. Local hour angle (H)
  float zenith = 90.833;  // Official sunrise/sunset zenith
  float cosH = (cos(zenith * PI / 180) - (sinDec * sin(LATITUDE * PI / 180))) / (cosDec * cos(LATITUDE * PI / 180));

  if (cosH > 1) return -1;   // Sun never rises
  if (cosH < -1) return -1;  // Sun never sets

  float H = isSunrise ? (360 - acos(cosH) * 180 / PI) : (acos(cosH) * 180 / PI);
  H = H / 15.0;

  // 8. Local Mean Time (T)
  float T = H + RA - (0.06571 * t) - 6.622;

  // 9. Adjust to UTC and then Local Time
  float UT = fmod(T - lngHour, 24.0);
  float localT = UT + TIMEZONE;

  if (localT < 0) localT += 24.0;
  if (localT > 24) localT -= 24.0;

  return localT;
}

// Create a string that reflects current time for data
String rtcRead() {
  if (rtcInit) {
    DateTime now = rtc.now();
    char timeBuffer[20];
    snprintf(timeBuffer, sizeof(timeBuffer), "%04d-%02d-%02d %02d:%02d:%02d",
             now.year(), now.month(), now.day(), now.hour(), now.minute(), now.second());
    return String(timeBuffer);
  }
  return String("0000-00-00 00:00:00");  // No RTC was found, so we are using a default time
}

// Log errors or system messages in a CSV file for troubleshooting later
void housekeepingWrite(String data) {
  Serial.println(data);
  if (hkInit && sdInit) {
    FsFile hkFile = SD.open(housekeeping, FILE_WRITE);
    if (hkFile) {
      hkFile.println(rtcRead() + "," + data);
      hkFile.close();
    }
  }
}

void setup() {
  Serial.begin(9600);
  // Give user 5 seconds to open serial monitor if they want to see setup
  unsigned long startWait = millis();
  while (!Serial && millis() - startWait < 5000)
    ;  // This code for working on a battery and not USB Serial

  // Turn on RTC and OLED monitor
  Wire.begin();

  // Turn on the RFID module
  pinMode(EN_PIN, OUTPUT);
  digitalWrite(EN_PIN, LOW);
  delay(500);
  digitalWrite(EN_PIN, HIGH);
  delay(3000);

  // Start RFID transmissions via serial port (GPIO0 and GPIO1)
  Serial1.begin(9600, SERIAL_8N1);

  // Start SD card functions
  if (SD.begin(config)) {
    sdInit = true;
    Serial.println("SD SUCCESS");
  } else {
    Serial.println("SD FAIL");
  }

  // Construct file names based on device ID
  sprintf(housekeeping, "/%s_house.csv", deviceID);
  sprintf(rfidlog, "/%s_rfid.csv", deviceID);

  // Open housekeeping files
  if (sdInit) {
    FsFile hkFile = SD.open(housekeeping, FILE_WRITE);
    if (!hkFile) {
      Serial.println("Failed to open housekeeping file at setup");
    } else {
      hkFile.close();
      hkInit = true;  // Functions involving the housekeeping file will work now
    }
  }

  // Start the RTC (Real-Time-Clock)
  if (rtc.begin()) {
    rtcInit = true;
    Serial.println("RTC started");
    DateTime fallbackTime = DateTime(F(__DATE__), F(__TIME__));
    if (rtc.now() < fallbackTime) {
      rtc.adjust(fallbackTime);
      housekeepingWrite(String("RTC adjusted to fallback time"));
    } else {
      housekeepingWrite(String("RTC ahead of fallback time"));
    }
    if (rtc.lostPower()) {
      housekeepingWrite(String("RTC lost power"));
    }

    // Set up RTC wake up pin
    pinMode(WAKE_PIN, INPUT_PULLUP);
    rtc.writeSqwPinMode(DS3231_OFF);
    rtc.disable32K();
    rtc.clearAlarm(1);
  } else {
    housekeepingWrite(String("Couldn't find RTC!"));
  }

  // Initialize OLED Buttons with Internal Pullups
  pinMode(BTN_A, INPUT_PULLUP);
  pinMode(BTN_B, INPUT_PULLUP);
  pinMode(BTN_C, INPUT_PULLUP);

  // Attach Interrupts to buttons
  attachInterrupt(digitalPinToInterrupt(BTN_A), handleBtnA, FALLING);
  attachInterrupt(digitalPinToInterrupt(BTN_B), handleBtnB, FALLING);
  attachInterrupt(digitalPinToInterrupt(BTN_C), handleBtnC, FALLING);

  // Start the OLED monitor
  if (display.begin(0x3C, true)) {
    oledInit = true;
    display.setRotation(1);
    display.setTextSize(2);
    display.setTextColor(SH110X_WHITE);
    display.clearDisplay();
    display.display();
    display.oled_command(SH110X_DISPLAYOFF);
  }


  // Set the default motion time and check time
  lastMotionTime = millis();
  lastCheck = millis();

  housekeepingWrite("Device Initialized");
}

void loop() {
  // Read RFID tag
  static uint8_t buffer[31];    // Local buffer
  static int buffer_index = 0;  // Local buffer index
  if (Serial1.available() > 0) {
    bool call_extract_tag = false;
    int ssvalue = Serial1.read();
    if (ssvalue == -1) return;  // No data was read
    if (ssvalue == 2) {
      if (buffer_index > 0) {
        return;  // We already have data, ignore this second STX
      }
      buffer_index = 0;  // Start of data
    } else if (ssvalue == 3)
      call_extract_tag = true;  // End of data
    if (buffer_index >= 31) {
      Serial.println("Buffer overflow detected");
      buffer_index = 0;
      return;
    }
    buffer[buffer_index++] = ssvalue;
    if (call_extract_tag && buffer_index > 2) {
      String tag = "";
      for (int i = 1; i <= 15; i++) {
        tag += (char)buffer[i];
      }
      lastMotionTime = millis();  // Keeps device from sleeping when there is RFID activity
      lastTag = tag;
      String timeString = rtcRead();
      Serial.println(tag + " @ " + timeString);
      if (sdInit) {
        FsFile rlFile = SD.open(rfidlog, FILE_WRITE);
        if (rlFile) {
          rlFile.println(String(deviceID) + "," + timeString + "," + tag);
          rlFile.close();
        }
      }

      if (oledPowerState) {
        updateDisplay();
      }

      // Check if it is a designated check tag and turn on OLED if yes
      for (auto t : checkTags) {
        if (tag.indexOf(t) != -1) {
          if (!oledPowerState) {
            btnAPressed = true;
          }
          break;
        }
      }

      buffer_index = 0;

      while (Serial1.available() > 0) {
        Serial1.read();  // Flush bytes
      }
      return;
    }
  }

  // Turn on OLED if button A is pressed
  if (btnAPressed) {
    btnAPressed = false;
    if (!oledPowerState) {
      oledPowerState = true;
      oledOnTime = millis();
      updateDisplay();
    }
  }

  // Switch Page on OLED monitor if button B is pressed
  if (btnBPressed) {
    btnBPressed = false;
    if (oledPowerState) {
      oledOnTime = millis();
      currentPage = (currentPage == 1) ? 2 : 1;
      updateDisplay();
    }
  }

  // Turn off monitor if button C is pressed
  if (btnCPressed) {
    btnCPressed = false;
    if (oledPowerState) {
      oledPowerState = false;
      updateDisplay();
    }
  }

  // Turn off monitor if OLED timeout has passed
  if (oledPowerState && (millis() - oledOnTime >= OLED_TIMEOUT)) {
    if (oledPowerState) {
      oledPowerState = false;
      updateDisplay();
    }
  }

  delay(10);  // Small debounce delay

  // Check if it is time to sleep
  unsigned long currentMillis = millis();
  if (rtcInit && (currentMillis - lastMotionTime >= TIMEOUT_DURATION_MS)) {
    if (currentMillis - lastCheck >= checkInterval) {
      DateTime now = rtc.now();

      if (dynamicSleep) {
        endSleepTime = calculateSunTime(now.year(), now.month(), now.day(), true) - 1;
        startSleepTime = calculateSunTime(now.year(), now.month(), now.day(), false) + 1;
      }

      float currentTime = (float)now.hour() + ((float)now.minute() / 60.0);
      bool shouldSleep = false;
      if (startSleepTime < endSleepTime) {
        shouldSleep = (currentTime >= startSleepTime && currentTime < endSleepTime);
      } else {
        shouldSleep = (currentTime >= startSleepTime || currentTime < endSleepTime);
      }
      if (shouldSleep) {
        housekeepingWrite("Going to sleep. Waking at " + String(endSleepTime));
        // Calculate Wake Time
        int wakeHour = (int)endSleepTime;
        int wakeMinute = (int)((endSleepTime - wakeHour) * 60);
        DateTime wakeTime(now.year(), now.month(), now.day(), wakeHour, wakeMinute, 0);
        if (wakeTime < now) wakeTime = wakeTime + TimeSpan(1, 0, 0, 0);
        // Set Alarm
        if (digitalRead(WAKE_PIN) == LOW) {
          rtc.clearAlarm(1);
          delay(50);
        }
        rtc.setAlarm1(wakeTime, DS3231_A1_Hour);
        if (sdInit) SD.end();
        Serial.end();
        Serial1.end();
        display.oled_command(SH110X_DISPLAYOFF);
        delay(3000);
        digitalWrite(EN_PIN, LOW);
        // Hardware Sleep Loop
        clock_configure(clk_sys, CLOCKS_CLK_SYS_CTRL_SRC_VALUE_CLK_REF, 0, 12 * MHZ, 12 * MHZ);  // Switch the system clock to the external crystal (12MHz)
        gpio_set_dormant_irq_enabled(WAKE_PIN, GPIO_IRQ_LEVEL_LOW, true);                        // Tell the Power Manager to wake up if pin is low
        xosc_dormant();                                                                          // Sleep
        gpio_set_dormant_irq_enabled(WAKE_PIN, GPIO_IRQ_LEVEL_LOW, false);                       // Disable interrupt
        watchdog_reboot(0, 0, 0);                                                                // Clean Reboot
        while (true) tight_loop_contents();
      }
    }
  }
}
