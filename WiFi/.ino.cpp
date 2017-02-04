#ifdef __IN_ECLIPSE__
//This is a automatic generated file
//Please do not modify this file
//If you touch this file your change will be overwritten during the next build
//This file has been generated on 2017-01-31 22:46:49

#include "Arduino.h"
#include "stdio.h"
#include "Arduino.h"
#include <EEPROM.h>
#include <Wire.h>
#include <ArduinoOTA.h>
#include <ESP8266WiFi.h>
#include <ESP8266mDNS.h>
#include <ESPAsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <fauxmoESP.h>
#include <base64.h>
#include <SPIFFSEditor.h>
#include "WiFi.h"
void setup() ;
void loop() ;
boolean getDataFromI2C() ;
void fauxmoMessageHandler(unsigned char device_id, const char * device_name, bool state) ;
void receiveHandler(int bytes) ;
void sendToI2C(I2CCommands command, byte value) ;
void sendToI2C(I2CCommands command, String value) ;
void sendToI2C(I2CCommands command, bool value) ;
void grabInts(String s, int *dest, String sep) ;
void grabBytes(String s, byte *dest, String sep) ;
void sendTimeToI2C() ;
void sendAlarmToI2C() ;
void sendStatus(String msg) ;
void sendWifiValues(AsyncWebSocketClient *client) ;
void sendClockValues(AsyncWebSocketClient *client) ;
void sendLEDValues(AsyncWebSocketClient *client) ;
void sendAlarmValues(AsyncWebSocketClient *client) ;
void updateValue(String pair) ;
void broadcastUpdate(String field, String value) ;
void broadcastUpdateWithQuotes(String field, String value) ;
void handleWSMsg(AsyncWebSocketClient *client, char *msg) ;
void onEvent(AsyncWebSocket *server, AsyncWebSocketClient *client, AwsEventType type, void *arg, uint8_t *data, size_t len);
String getInput(String type, String name, String label, String value) ;
void mainHandler(AsyncWebServerRequest *request) ;
String getTableHead2Col(String tableHeader, String col1Header, String col2Header) ;
String getTableRow2Col(String col1Val, String col2Val) ;
String getTableRow2Col(String col1Val, int col2Val) ;
String getTableFoot() ;
void systemHandler(AsyncWebServerRequest *request) ;
void wifiHandler(AsyncWebServerRequest *request) ;
void StartOTA() ;
void WiFiConnect() ;
void initializeCredentials() ;
String readStringFromEEPROM(int start, int max) ;
void writeStringToEEPROM(String s, int start, int max) ;

#include "WiFi.ino"


#endif
