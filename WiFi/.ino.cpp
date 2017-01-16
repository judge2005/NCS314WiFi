#ifdef __IN_ECLIPSE__
//This is a automatic generated file
//Please do not modify this file
//If you touch this file your change will be overwritten during the next build
//This file has been generated on 2017-01-16 12:38:56

#include "Arduino.h"
#include "Arduino.h"
#include <EEPROM.h>
#include <Wire.h>
#include <ArduinoOTA.h>
#include <ESP8266WiFi.h>
#include <ESP8266mDNS.h>
#include <ESPAsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <base64.h>
#include <SPIFFSEditor.h>
#include <ArduinoJson.h>
void setup() ;
void loop() ;
void sendTimeToI2C() ;
void mainHandler(AsyncWebServerRequest *request) ;
void heapHandler(AsyncWebServerRequest *request) ;
void wifiHandler(AsyncWebServerRequest *request) ;
void statusHandler(AsyncWebServerRequest *request) ;
void StartOTA() ;
void WiFiConnect() ;
void initializeCredentials() ;
String readStringFromEEPROM(int start, int max) ;
void writeStringToEEPROM(String s, int start, int max) ;

#include "WiFi.ino"


#endif
