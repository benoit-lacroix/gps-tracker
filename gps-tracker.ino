#include <SoftwareSerial.h>
#include "Adafruit_FONA.h"
#include "AESLib.h"

// Enable Serial Debugging information
//#define TRACKER_DEBUG
// Enable blocking sketch on error (during initialization only)
//#define BLOCK_ON_ERROR

// Fona / SIM808 Connexion Pin (can be adapted to your board)
#define SIM808_RX   10
#define SIM808_TX   11
#define SIM808_RST  4

// Serial Communication Variables
SoftwareSerial softSerial   = SoftwareSerial(SIM808_TX, SIM808_RX);
SoftwareSerial *fonaSerial  = &softSerial;

// Reset function
void(* resetFunc) (void) = 0;

Adafruit_FONA fona = Adafruit_FONA(SIM808_RST);

// Char for notifying start tracking at first position sent to the API
char notify = '1';

void reboot() {
  #ifdef BLOCK_ON_ERROR
  while(1);
  #else
  resetFunc();
  #endif
}

void flushSerial() {
  while (Serial.available()) {
    Serial.read();
  }
}

void setup() {
  while (!Serial);
  Serial.begin(115200);
  Serial.println(F("GPS Tracker Starting"));

  fonaSerial->begin(4800);
  if (!fona.begin(*fonaSerial)) {
    Serial.println(F("Couldn't connect to SIM808"));
    reboot();
  }
  Serial.println(F("SIM808 OK"));
  Serial.println(F("Initializing GPRS"));
  
  // Fill with your GPRS provider
  // If login and password are null, then put '0' instead of 'F("xxx")'
  fona.setGPRSNetworkSettings(F("apn"), F("login"), F("password"));
  if (!fona.enableGPRS(true)) {
    Serial.println(F("Enabling GPRS failed!"));
    reboot();
  }

  Serial.println(F("Enabling GPS"));
  fona.enableGPS(true);

  Serial.println(F("Initialization completed"));
}

void loop() {
  flushSerial();
  float latitude, longitude, speed_kph, heading, altitude;
  boolean gps_success = fona.getGPS(&latitude, &longitude, &speed_kph, &heading, &altitude);
  if (gps_success) {
    char clearData[128] = {0};
    char encData[256] = {0};
    uint16_t httpStatus, dataLen;
    
    // Pattern expected by my Gateway API (cf. https://github.com/benoit-lacroix/gateway)
    // Set 'device' to identify your device.
    #define OBJECT_PATTERN "{ \"device\": \"xxx\", \"notify\": \"%c\", \"latitude\": \"%s\", \"longitude\": \"%s\" }"
    int dataSize = sprintf((char*)clearData, OBJECT_PATTERN, notify, String(latitude, 6).c_str(), String(longitude, 6).c_str());

    AESLib aesLib;
    // To be filled with your AES key (only AES 128 supported)
    const byte aes_key[16]  = { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 };
    // To be filled with your initialization vector
    const byte iv[16]       = { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 };

    aesLib.set_paddingmode((paddingMode) 0); // CMS (PKCS5PADDING)
    aesLib.encrypt64((byte*)clearData, dataSize, (char*)encData, aes_key, 16, iv);

    #ifdef TRACKER_DEBUG
    Serial.println(clearData);
    Serial.println(dataSize);
    Serial.println(encData);
    #endif

    // Fill this with your backend API.
    // Example: mybackend.org/my-api/my-gps-position?api-key=xxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxxx
    #define API_POSITION "xxx"
    fona.HTTP_POST_start(API_POSITION, F("application/json"), (uint8_t *) encData, strlen(encData), &httpStatus, (uint16_t *)&dataLen);

    #ifdef TRACKER_DEBUG
    Serial.println(F("*****"));
    while (dataLen > 0) {
      while (fona.available()) {
        char c = fona.read();
          #if defined(__AVR_ATmega328P__) || defined(__AVR_ATmega168__)
            loop_until_bit_is_set(UCSR0A, UDRE0); // Wait until data register empty.
            UDR0 = c;
          #else
            Serial.write(c);
          #endif
        dataLen--;
        if (! dataLen) break;
      }
    }
    Serial.println(F("\n*****"));
    #endif
    
    fona.HTTP_POST_end();
    
    if (httpStatus == 200) {
      Serial.println(F("API Call Ok"));
      notify = '0';
    } else {
      Serial.println(F("API Call Error"));
    }
  } else {
    Serial.println(F("Waiting for GPS"));
  }
  delay(1000); // One second between each position
}
