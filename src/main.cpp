#include "stdio.h"
#include "Arduino.h"
#include <EEPROM.h>
#include <Wire.h>
#define OTA
#ifdef OTA
#include <ArduinoOTA.h>
#endif
#include <ESP8266WiFi.h>
#include <ESP8266mDNS.h>
#include <ESPAsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <ESPAsyncHTTPClient.h>
#ifdef USE_FAUXMO_ESP
#include <fauxmoESP.h>
#endif
#include <base64.h>
#include <SPIFFSEditor.h>
#include "WiFi.h"

#define I2C_TARGET 0x23

#define SSID_START 0x0
#define SSID_MAX 0x20
#define PASSWORD_START 0x20
#define PASSWORD_MAX 0x40
#define URL_START (PASSWORD_START + PASSWORD_MAX)
#define URL_MAX 0x100
#define HOSTNAME_START (URL_START + URL_MAX)
#define HOSTNAME_MAX 63

//#define DEBUG(...) { Wire.beginTransmission(I2C_TARGET); Wire.print(__VA_ARGS__); Wire.endTransmission(); }
//#define DEBUG_SERIAL
//#define DEBUG(...) {Serial.println(__VA_ARGS__);}
#define DEBUG(...) {}

String readStringFromEEPROM(int start, int max);
void writeStringToEEPROM(String s, int start, int max);
void WiFiConnect();
void StartOTA();
void mainHandler(AsyncWebServerRequest *request);
void systemHandler(AsyncWebServerRequest *request);
void wifiHandler(AsyncWebServerRequest *request);
void initializeCredentials();
void broadcastUpdate(String field, String value);
void broadcastUpdateWithQuotes(String field, String value);
void onEvent(AsyncWebSocket *server, AsyncWebSocketClient *client, AwsEventType type, void *arg, uint8_t *data, size_t len);
void sendTimeToI2C();
void sendStatus(String msg);

AsyncWebServer server(80);
AsyncWebSocket ws("/ws"); // access at ws://[esp ip]/ws
#ifdef USE_FAUXMO_ESP
fauxmoESP fauxmo;
#endif

int led = 1;
int d = 5000;
int count = 1;

String hostName = "in-14-shield";

String ssid = "........";
String password = "...........";

bool time_or_date = true;
byte date_format = 1;
bool time_format = false;	//false = 24 hour
bool leading_zero = false;
byte display_on = 5;
byte display_off = 22;

bool backlight = false;
bool hue_cycling = true;
byte cycle_time = 23;
byte hue = 128;
byte saturation = 128;
byte brightness = 128;

bool alarm_set = false;
String alarm_time = "22:30";

String lastStatus = "";

AsyncHTTPClient httpClient;

// the setup function runs once when you press reset or power the board
void setup() {
#ifdef DEBUG_SERIAL
	Serial.begin(9600);
#endif
	EEPROM.begin(512);
	SPIFFS.begin();

	String configHostname = readStringFromEEPROM(HOSTNAME_START, HOSTNAME_MAX);
	if (configHostname.length() > 0) {
		hostName = configHostname;
	}

	// initialize digital pin 13 as an output.
	pinMode(led, OUTPUT);

#ifndef DEBUG_SERIAL
	Wire.begin(0, 2); // SDA = 0, SCL = 2
#endif
	WiFiConnect();

	MDNS.addService("http", "tcp", 80);

	StartOTA();

	httpClient.initialize(readStringFromEEPROM(URL_START, URL_MAX));

	server.on("/", HTTP_GET, mainHandler);
	server.on("/system", HTTP_GET, systemHandler);
	server.on("/set_wifi", HTTP_POST, wifiHandler);

	server.serveStatic("/assets", SPIFFS, "/assets");

	// attach AsyncWebSocket
	ws.onEvent(onEvent);
	server.addHandler(&ws);

	server.begin();
	ws.enable(true);
#ifdef USE_FAUXMO_ESP
    fauxmo.addDevice("nixie one");
    fauxmo.addDevice("backlight one");
    fauxmo.addDevice("cycle one");
    fauxmo.addDevice("date one");

    fauxmo.onMessage(fauxmoMessageHandler);
#endif
}

unsigned long lastUpdate = 0;
unsigned long lastBlink = 0;
byte ledState = 0;

// the loop function runs over and over again forever
void loop() {
#ifdef OTA
	ArduinoOTA.handle();
#endif

	unsigned long now = millis();

#ifdef USE_FAUXMO_ESP
	fauxmo.handle();
#endif

	if (WiFi.status() == WL_CONNECTED) {
		// See if it is time to update the Clock
		if (lastUpdate == 0 || now - lastUpdate > 60000) {
			lastUpdate = now;
			httpClient.makeRequest(sendTimeToI2C, sendStatus);
		}
	}
#ifdef notdef
	if (now - lastBlink > d) {
		lastBlink = now;
		ledState = ledState ^ 1;
		digitalWrite(led, ledState);
		DEBUG("loop");
	}
#endif
}

void getDataFromI2C() {
#ifndef DEBUG_SERIAL
	int available = Wire.requestFrom(I2C_TARGET, (unsigned int) I2CCommands::alarm_time+2);

	bool gotValues = false;
	int data[17];
	for (int i=0; i<17; i++) {
		data[i] = 256;
	}

	if (available == ((byte)I2CCommands::alarm_time+2)) {
		for (int i=0; i<17; i++) {
			data[i] = Wire.read();
		}
		int i = 2; // First two bytes are junk
		time_or_date = data[i++];
		date_format = data[i++];
		time_format = data[i++];	//false = 24 hour
		leading_zero = data[i++];
		display_on = data[i++];
		display_off = data[i++];

		backlight = data[i++];
		hue_cycling = data[i++];
		cycle_time = data[i++];
		hue = data[i++];
		saturation = data[i++];
		brightness = data[i++];

		alarm_set = data[i++];

		// This last one is a string. Expect two bytes, hour first, mins second
		byte hours = data[i++];
		byte mins = data[i++];
		alarm_time = "";
		if (hours < 10) {
			alarm_time = "0";
		}

		alarm_time += hours;
		alarm_time += ":";

		if (mins < 10) {
			alarm_time += 0;
		}

		alarm_time += mins;
		gotValues = true;
	}

	Wire.beginTransmission(I2C_TARGET);
	String s = "Got Values, flag=" + String(gotValues) + ":";
	Wire.print(s);
	Wire.endTransmission();
	for (int i=0; i<17; i++) {
		DEBUG(String(data[i]).c_str());
	}
#endif
}

#ifdef USE_FAUXMO_ESP
void fauxmoMessageHandler(unsigned char device_id, const char * device_name, bool state) {
	Wire.beginTransmission(I2C_TARGET);
	Wire.printf("Device #%d (%s): %s\n", device_id, device_name, state ? "ON" : "OFF");
	Wire.endTransmission();

	switch (device_id) {
	case 0:
		if (alarm_set != state) {
			alarm_set = state;
			sendToI2C(I2CCommands::alarm_set, alarm_set);
			broadcastUpdate("alarm_set", alarm_set ? "true" : "false");
		}
		break;
	case 1:
		if (backlight != state) {
			backlight = state;
			sendToI2C(I2CCommands::backlight, backlight);
			broadcastUpdate("backlight", backlight ? "true" : "false");
		}
		break;
	case 2:
		if (hue_cycling != state) {
			hue_cycling = state;
			sendToI2C(I2CCommands::hue_cycling, hue_cycling);
			broadcastUpdate("hue_cycling", hue_cycling ? "true" : "false");
		}
		break;
	case 3:
		// Weird because date == on means time_or_date = false!
		if (time_or_date == state) {
			time_or_date = !state;
			sendToI2C(I2CCommands::time_or_date, time_or_date);
			broadcastUpdate("time_or_date", time_or_date ? "true" : "false");
		}
		break;
	}
}
#endif

void receiveHandler(int bytes) {
#ifndef DEBUG_SERIAL
	byte command = Wire.read();
	switch ((I2CCommands) command) {
		case I2CCommands::hue:
			hue = Wire.read();
			broadcastUpdate("hue", String(hue));
			break;
	}
	DEBUG("Got info from clock");
	Wire.print(command);
#endif
}

void sendToI2C(I2CCommands command, byte value) {
#ifndef DEBUG_SERIAL
	Wire.beginTransmission(I2C_TARGET);
	Wire.write((byte)(command)); // Command
	Wire.write(value);
	int error = Wire.endTransmission();
#endif
}

void sendToI2C(I2CCommands command, String value) {
#ifndef DEBUG_SERIAL
	Wire.beginTransmission(I2C_TARGET);
	Wire.write((byte)(command)); // Command
	Wire.write(value.c_str());
	int error = Wire.endTransmission();
#endif
}

void sendToI2C(I2CCommands command, bool value) {
#ifndef DEBUG_SERIAL
	Wire.beginTransmission(I2C_TARGET);
	Wire.write((byte)(command)); // Command
	Wire.write(value);
	int error = Wire.endTransmission();
#endif
}

void grabInts(String s, int *dest, String sep) {
	int end = 0;
	for (int start = 0; end != -1; start = end + 1) {
		end = s.indexOf(sep, start);
		if (end > 0) {
			*dest++ = s.substring(start, end).toInt();
		} else {
			*dest++ = s.substring(start).toInt();
		}
	}
}

void grabBytes(String s, byte *dest, String sep) {
	int end = 0;
	for (int start = 0; end != -1; start = end + 1) {
		end = s.indexOf(sep, start);
		if (end > 0) {
			*dest++ = s.substring(start, end).toInt();
		} else {
			*dest++ = s.substring(start).toInt();
		}
	}
}

void sendTimeToI2C() {
	String body = httpClient.getBody();
	DEBUG(body);

#ifndef DEBUG_SERIAL
	int intValues[7];
	grabInts(body, &intValues[1], ",");

	byte args[7];
	args[0] = (byte) I2CCommands::time;
	args[1] = (byte) (intValues[1] - 2000);
	for (int i=2; i<7; i++) {
		args[i] = intValues[i];
	}

	Wire.beginTransmission(I2C_TARGET);
	Wire.write(args, 7);
	int error = Wire.endTransmission();
#endif
	sendStatus(body);
}

void sendAlarmToI2C() {
#ifndef DEBUG_SERIAL
	byte args[3];
	args[0] = (byte) I2CCommands::alarm_time;
	grabBytes(alarm_time, &args[1], ":");

	Wire.beginTransmission(I2C_TARGET);
	Wire.write(args, 3);
	int error = Wire.endTransmission();
#endif
}

void sendStatus(String msg) {
	lastStatus = msg;
	String json = "{\"type\":\"sv.status\",\"value\":\""+msg+"\"}";

	ws.textAll(json);
}

void sendWifiValues(AsyncWebSocketClient *client) {
	String json = String("{\"type\":\"sv.init.wifi\",\"value\":{\"ssid\":\"");
	json += ssid;
	json += "\",\"password\":\"";
	json += password;
	json += "\",\"hostname\":\"";
	json += hostName;
	json += "\"}}";

	client->text(json);

	json = "{\"type\":\"sv.status\",\"value\":\""+lastStatus+"\"}";

	client->text(json);
}

void sendClockValues(AsyncWebSocketClient *client) {
	String url = readStringFromEEPROM(URL_START, URL_MAX);
	String json = String("{\"type\":\"sv.init.clock\",\"value\":{") +
			"\"time_or_date\":" + time_or_date + "," +
			"\"date_format\":" + date_format + "," +
			"\"time_format\":" + time_format + "," +
			"\"leading_zero\":" + leading_zero + "," +
			"\"display_on\":" + display_on + "," +
			"\"display_off\":" + display_off + "," +
			"\"time_server\":\"" + url +
			"\"}}";

	client->text(json);
}

void sendLEDValues(AsyncWebSocketClient *client) {
	String json = String("{\"type\":\"sv.init.clock\",\"value\":{") +
			"\"backlight\":" + backlight + "," +
			"\"hue_cycling\":" + hue_cycling + "," +
			"\"cycle_time\":" + cycle_time + "," +
			"\"hue\":" + hue + "," +
			"\"saturation\":" + saturation + "," +
			"\"brightness\":" + brightness + "}}";

	client->text(json);
}

void sendAlarmValues(AsyncWebSocketClient *client) {
	String json = String("{\"type\":\"sv.init.clock\",\"value\":{") +
			"\"alarm_set\":" + alarm_set + "," +
			"\"alarm_time\":\"" + alarm_time + "\"}}";

	client->text(json);
}

void updateValue(String pair) {
	int index = pair.indexOf(':');

	// _key has to hang around because key points to an internal data structure
	String _key = pair.substring(0, index);
	const char* key = _key.c_str();
	String value = pair.substring(index+1);

	if (strcmp("time_server", key) == 0) {
		writeStringToEEPROM(value, URL_START, URL_MAX);
		httpClient.initialize(value);
		broadcastUpdateWithQuotes(_key, value);
	} else if (strcmp("time_or_date", key) == 0) {
		time_or_date = value == "true";
		sendToI2C(I2CCommands::time_or_date, time_or_date);
		broadcastUpdate(_key, value);
	} else if (strcmp("date_format", key) == 0) {
		date_format = value.toInt();
		sendToI2C(I2CCommands::date_format, date_format);
		broadcastUpdate(_key, value);
	} else if (strcmp("time_format", key) == 0) {
		time_format = value == "true";
		sendToI2C(I2CCommands::time_format, time_format);
		broadcastUpdate(_key, value);
	} else if (strcmp("leading_zero", key) == 0) {
		leading_zero = value == "true";
		sendToI2C(I2CCommands::leading_zero, leading_zero);
		broadcastUpdate(_key, value);
	} else if (strcmp("display_on", key) == 0) {
		display_on = value.toInt();
		sendToI2C(I2CCommands::display_on, display_on);
		broadcastUpdate(_key, value);
	} else if (strcmp("display_off", key) == 0) {
		display_off = value.toInt();
		sendToI2C(I2CCommands::display_off, display_off);
		broadcastUpdate(_key, value);
	} else if (strcmp("backlight", key) == 0) {
		backlight = value == "true";
		sendToI2C(I2CCommands::backlight, backlight);
		broadcastUpdate(_key, value);
	} else if (strcmp("hue_cycling", key) == 0) {
		hue_cycling = value == "true";
		sendToI2C(I2CCommands::hue_cycling, hue_cycling);
		broadcastUpdate(_key, value);
	} else if (strcmp("cycle_time", key) == 0) {
		cycle_time = value.toInt();
		sendToI2C(I2CCommands::cycle_time, cycle_time);
		broadcastUpdate(_key, value);
	} else if (strcmp("hue", key) == 0) {
		hue = value.toInt();
		sendToI2C(I2CCommands::hue, hue);
		broadcastUpdate(_key, value);
	} else if (strcmp("saturation", key) == 0) {
		saturation = value.toInt();
		sendToI2C(I2CCommands::saturation, saturation);
		broadcastUpdate(_key, value);
	} else if (strcmp("brightness", key) == 0) {
		brightness = value.toInt();
		sendToI2C(I2CCommands::brightness, brightness);
		broadcastUpdate(_key, value);
	} else if (strcmp("alarm_set", key) == 0) {
		alarm_set = value == "true";
		sendToI2C(I2CCommands::alarm_set, alarm_set);
		broadcastUpdate(_key, value);
	} else if (strcmp("alarm_time", key) == 0) {
		alarm_time = value;
		sendAlarmToI2C();
		broadcastUpdate(_key, value);
	}
}

void broadcastUpdate(String field, String value) {
	String json = String("{\"type\":\"sv.update\",\"value\":{") +
			"\"" + field + "\":" + value + "}}";
	ws.textAll(json);
}

void broadcastUpdateWithQuotes(String field, String value) {
	String json = String("{\"type\":\"sv.update\",\"value\":{") +
			"\"" + field + "\":\"" + value + "\"}}";
	ws.textAll(json);
}

/*
 * msg is
 * <num>: for a message with no arguments or
 * <num>:<string>:<value> for a message that has a key/value pair
 *
 */
void handleWSMsg(AsyncWebSocketClient *client, char *msg) {
	DEBUG(msg);
	String wholeMsg(msg);
	int code = wholeMsg.substring(0, wholeMsg.indexOf(':')).toInt();

	switch (code) {
	case 1:
		sendWifiValues(client);
		break;
	case 2:
		sendClockValues(client);
		break;
	case 3:
		sendLEDValues(client);
		break;
	case 4:
		sendAlarmValues(client);
		break;
	case 5:
		updateValue(wholeMsg.substring(wholeMsg.indexOf(':')+1));
		break;
	}
}

void onEvent(AsyncWebSocket *server, AsyncWebSocketClient *client, AwsEventType type, void *arg, uint8_t *data, size_t len){
  //Handle WebSocket event
	switch (type) {
	case WS_EVT_CONNECT:
		DEBUG("WS connected");
		break;
	case WS_EVT_DISCONNECT:
		DEBUG("WS disconnected");
		break;
	case WS_EVT_ERROR:
		DEBUG("WS error");
		DEBUG((char*)data);
		break;
	case WS_EVT_PONG:
		DEBUG("WS pong");
		break;
	case WS_EVT_DATA:	// Yay we got something!
		DEBUG("WS data");
		AwsFrameInfo * info = (AwsFrameInfo*) arg;
		if (info->final && info->index == 0 && info->len == len) {
			//the whole message is in a single frame and we got all of it's data
			if (info->opcode == WS_TEXT) {
				DEBUG("WS text data");
				data[len] = 0;
				handleWSMsg(client, (char *)data);
			} else {
				DEBUG("WS binary data");
			}
		} else {
			DEBUG("WS data was split up!");
		}
		break;
	}
}

String getInput(String type, String name, String label, String value) {
	String input = "<input type=\"" + type + "\" name=\"" + name + "\" placeholder=\"" + label + "\" value=\"" + value + "\"/>";

	return input;
}

void mainHandler(AsyncWebServerRequest *request) {
	DEBUG("Got main request");
	if (WiFi.status() != WL_CONNECTED) {
		String response("<!DOCTYPE html><html><head><title>IN-14 Setup</title>");

		response += "<meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">";
		response +=	"<link rel=\"stylesheet\" href=\"/assets/form.css\" /></head>";
		response += "<body><div id=\"form-main\"><div id=\"form-div\">";
		response += "<form class=\"form\" action=\"/set_wifi\" method=\"POST\" id=\"wifi_form\">";
		response += getInput("text", "ssid", "SSID", readStringFromEEPROM(SSID_START, SSID_MAX));
		response += getInput("password", "password", "Password", readStringFromEEPROM(PASSWORD_START, PASSWORD_MAX));
		response += getInput("text", "hostname", "Hostname", readStringFromEEPROM(HOSTNAME_START, HOSTNAME_MAX));
		response += "<input type=\"submit\" value=\"Set\" id=\"button-blue\"/>";
		response += "</form></div></div></body></html>";

		request->send(200, "text/html", response);
	} else {
		getDataFromI2C();
		request->send(SPIFFS, "/index.html");
	}
}


/**
   Get the header for a 2 column table
*/
String getTableHead2Col(String tableHeader, String col1Header, String col2Header) {
  String tableHead = "<h3>";
  tableHead += tableHeader;
  tableHead += "</h3>";
  tableHead += "<table><thead><tr><th>";
  tableHead += col1Header;
  tableHead += "</th><th>";
  tableHead += col2Header;
  tableHead += "</th></tr></thead><tbody>";

  return tableHead;
}

String getTableRow2Col(String col1Val, String col2Val) {
  String tableRow = "<tr><td>";
  tableRow += col1Val;
  tableRow += "</td><td>";
  tableRow += col2Val;
  tableRow += "</td></tr>";

  return tableRow;
}

String getTableRow2Col(String col1Val, int col2Val) {
  String tableRow = "<tr><td>";
  tableRow += col1Val;
  tableRow += "</td><td>";
  tableRow += col2Val;
  tableRow += "</td></tr>";

  return tableRow;
}

String getTableFoot() {
  return "</tbody></table></div></div>";
}

void systemHandler(AsyncWebServerRequest *request) {
	DEBUG("Got system request");
	String response("<html><head><title>IN-14 Stats</title><meta name=\"viewport\" content=\"width=device-width, initial-scale=1\"><link rel=\"stylesheet\" href=\"/assets/table.css\"/></head><body>");
	float voltage = (float) ESP.getVcc() / (float) 1024;
	voltage -= 0.01f;  // by default reads high
	char dtostrfbuffer[15];
	dtostrf(voltage, 7, 2, dtostrfbuffer);
	String vccString = String(dtostrfbuffer);

	// ESP8266 Info table
	response += getTableHead2Col("ESP8266 information", "Name", "Value");
	response += getTableRow2Col("Sketch size", ESP.getSketchSize());
	response += getTableRow2Col("Free sketch size", ESP.getFreeSketchSpace());
	response += getTableRow2Col("Free heap", ESP.getFreeHeap());
	response += getTableRow2Col("Boot version", ESP.getBootVersion());
	response += getTableRow2Col("CPU Freqency (MHz)", ESP.getCpuFreqMHz());
	response += getTableRow2Col("SDK version", ESP.getSdkVersion());
	response += getTableRow2Col("Chip ID", ESP.getChipId());
	response += getTableRow2Col("Flash Chip ID", ESP.getFlashChipId());
	response += getTableRow2Col("Flash size", ESP.getFlashChipRealSize());
	response += getTableRow2Col("Vcc", vccString);
	response += getTableFoot();

	response += "</body></html>";
	request->send(200, "text/html", response);
}

void wifiHandler(AsyncWebServerRequest *request) {
	DEBUG("Got set wifi request");
	int args = request->args();
	for(int i=0;i<args;i++){
	  DEBUG(request->argName(i).c_str());
	  DEBUG(request->arg(i).c_str());
	}

	AsyncWebParameter* p = request->getParam("ssid", true);
	if (p) {
		String _ssid = p->value();
		writeStringToEEPROM(_ssid, SSID_START, SSID_MAX);
		DEBUG(_ssid.c_str());
	} else {
		writeStringToEEPROM("", SSID_START, SSID_MAX);
	}

	p = request->getParam("password", true);
	if (p) {
		String _password = p->value();
		writeStringToEEPROM(_password, PASSWORD_START, PASSWORD_MAX);
		DEBUG(_password.c_str());
	} else {
		writeStringToEEPROM("", PASSWORD_START, PASSWORD_MAX);
	}

	p = request->getParam("hostname", true);
	if (p) {
		String _hostname = p->value();
		writeStringToEEPROM(_hostname, HOSTNAME_START, HOSTNAME_MAX);
		DEBUG(_hostname.c_str());
	} else {
		writeStringToEEPROM("", HOSTNAME_START, HOSTNAME_MAX);
	}

	ESP.restart();
}

void StartOTA() {
#ifdef OTA
	// Port defaults to 8266
	ArduinoOTA.setPort(8266);

	// Hostname defaults to esp8266-[ChipID]
	ArduinoOTA.setHostname(hostName.c_str());

	// No authentication by default
//	ArduinoOTA.setPassword("in14");

	ArduinoOTA.onStart([]() {DEBUG("Start");});
	ArduinoOTA.onEnd([]() {DEBUG("\nEnd");});
	ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
		DEBUG("Progress: ");DEBUG(progress / (total / 100));DEBUG("\r");
	});
	ArduinoOTA.onError([](ota_error_t error) {
		DEBUG("Error")
		switch (error) {
			case OTA_AUTH_ERROR: DEBUG("Auth Failed"); break;
			case OTA_BEGIN_ERROR: DEBUG("Begin Failed"); break;
			case OTA_CONNECT_ERROR: DEBUG("Connect Failed"); break;
			case OTA_RECEIVE_ERROR: DEBUG("Receive Failed"); break;
			case OTA_END_ERROR: DEBUG("End Failed"); break;
		}
	});

	ArduinoOTA.begin();
#endif
}

void WiFiConnect() {
	MDNS.begin(hostName.c_str());

	WiFi.mode(WIFI_AP_STA);
	WiFi.softAP(hostName.c_str());

	initializeCredentials();

	if (ssid.length() > 0) {
		DEBUG(ssid.c_str());
		DEBUG(password.c_str());

		WiFi.hostname(hostName);

		if (password.length() > 0) {
			WiFi.begin(ssid.c_str(), password.c_str());
		} else {
			WiFi.begin(ssid.c_str());
		}
	}
}

/**
 * EEPROM functions
 */
void initializeCredentials() {
	ssid = readStringFromEEPROM(SSID_START, SSID_MAX);
	password = readStringFromEEPROM(PASSWORD_START, PASSWORD_MAX);
}

String readStringFromEEPROM(int start, int max) {
	int end = start + max;
	String s = "";
	for (int i = start; i < end; i++) {
		byte readByte = EEPROM.read(i);
		if (readByte > 0 && readByte < 128) {
			s += char(readByte);
		} else {
			break;
		}
	}
	return s;
}

void writeStringToEEPROM(String s, int start, int max) {
	int end = start + max;
	for (int i = start; i < end; i++) {
	    if (i - start < s.length()) {
	      EEPROM.write(i, s[i - start]);
	    } else {
	      EEPROM.write(i, 0);
	      break;
	    }
	}

	EEPROM.commit();
}

