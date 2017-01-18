#include "stdio.h"
#include "Arduino.h"
#include <EEPROM.h>
#include <Wire.h>
#ifdef OTA	// ESP01 is too small!
#include <ArduinoOTA.h>
#endif
#include <ESP8266WiFi.h>
#include <ESP8266mDNS.h>
#include <ESPAsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <base64.h>
#include <SPIFFSEditor.h>

#define I2C_TARGET 0x23
#define I2C_TIME_UPDATE 0x0

#define SSID_START 0x0
#define SSID_MAX 0x20
#define PASSWORD_START 0x20
#define PASSWORD_MAX 0x40
#define URL_START 0x40
#define URL_MAX 0x100

#define DEBUG(...) { Wire.beginTransmission(I2C_TARGET); Wire.print(__VA_ARGS__); Wire.endTransmission(); }
//#define DEBUG(...) {}

AsyncWebServer server(80);
AsyncWebSocket ws("/ws"); // access at ws://[esp ip]/ws

int led = 1;
int d = 500;
int count = 1;

const char *hostName = "in-14-shield";

String ssid = "FiOS-3RA3E";
String password = "son8638woman458wag";

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

class ByteString: public String {
public:
	ByteString(void *data, size_t len) :
			String() {
		copy(data, len);
	}

	ByteString() :
			String() {
	}

	String& copy(const void *data, unsigned int length) {
		if (!reserve(length)) {
			invalidate();
			return *this;
		}
		len = length;
		memcpy(buffer, data, length);
		buffer[length] = 0;
		return *this;
	}
};

/**
 * Asynchronous TCP Client to retrieve data/time
 */
struct AsyncHTTPClient {
	AsyncClient *aClient = NULL;

	bool initialized = false;
	String protocol;
	String base64Authorization;
	String host;
	int port;
	String uri;
	String request;

	ByteString response;
	int statusCode;
	void (*onSuccess)();
	void (*onFail)(String);

	void initialize(String url) {
		// check for : (http: or https:
		int index = url.indexOf(':');
		if(index < 0) {
			initialized = false;	// This is not a URL
		}

		protocol = url.substring(0, index);
		DEBUG(protocol);
		url.remove(0, (index + 3)); // remove http:// or https://

		index = url.indexOf('/');
		String hostPart = url.substring(0, index);
		DEBUG(hostPart);
		url.remove(0, index); // remove hostPart part

		// get Authorization
		index = hostPart.indexOf('@');

		if(index >= 0) {
			// auth info
			String auth = hostPart.substring(0, index);
			hostPart.remove(0, index + 1); // remove auth part including @
			base64Authorization = base64::encode(auth);
		}

		// get port
		port = 80;	//Default
		index = hostPart.indexOf(':');
		if(index >= 0) {
			host = hostPart.substring(0, index); // hostname
			host.remove(0, (index + 1)); // remove hostname + :
			DEBUG(host);
			port = host.toInt(); // get port
			DEBUG(port);
		} else {
			host = hostPart;
			DEBUG(host);
		}
		uri = url;
		if (protocol != "http") {
			initialized = false;
		}

		DEBUG(initialized);
		request = "GET " + uri + " HTTP/1.1\r\nHost: " + host + "\r\n\r\n";

		DEBUG(request);
		initialized = true;
	}

	int getStatusCode() { return statusCode; }

	String getBody() {
		if (statusCode == 200) {
			int bodyStart = response.indexOf("\r\n\r\n") + 4;
			return response.substring(bodyStart);
		} else {
			return "";
		}
	}

	static void clientError(void *arg, AsyncClient *client, int error) {
		DEBUG("Connect Error");
		AsyncHTTPClient *self = (AsyncHTTPClient*)arg;
		self->onFail("Connection error");
		self->aClient = NULL;
		delete client;
	}

	static void clientDisconnect(void *arg, AsyncClient *client) {
		DEBUG("Disconnected");
		AsyncHTTPClient *self = (AsyncHTTPClient*)arg;
		self->aClient = NULL;
		delete client;
	}

	static void clientData(void *arg, AsyncClient *client, void *data, size_t len) {
		DEBUG("Got response");

		AsyncHTTPClient *self = (AsyncHTTPClient*)arg;
		self->response = ByteString(data, len);
		String status = self->response.substring(9, 12);
		self->statusCode = atoi(status.c_str());
		DEBUG(status.c_str());

		if (self->statusCode == 200) {
			self->onSuccess();
		} else {
			self->onFail("Failed with code " + status);
		}
	}

	static void clientConnect(void *arg, AsyncClient *client) {
		DEBUG("Connected");

		AsyncHTTPClient *self = (AsyncHTTPClient*)arg;

		self->response.copy("", 0);
		self->statusCode = -1;

		// Clear oneError handler
		self->aClient->onError(NULL, NULL);

		// Set disconnect handler
		client->onDisconnect(clientDisconnect, self);

		client->onData(clientData, self);

		//send the request
		client->write(self->request.c_str());
	}

	void makeRequest(void (*success)(), void (*fail)(String msg)) {
		onFail = fail;

		if (!initialized) {
			fail("Not initialized");
			return;
		}

		if (aClient) { //client already exists
			fail("Call taking forever");
			return;
		}

		aClient = new AsyncClient();
		if (!aClient) { //could not allocate client
			fail("Out of memory");
			return;
		}

		onSuccess = success;

		aClient->onError(clientError, this);

		aClient->onConnect(clientConnect, this);

		if (!aClient->connect(host.c_str(), port)) {
			DEBUG("Connect Fail");
			fail("Connection failed");
			AsyncClient * client = aClient;
			aClient = NULL;
			delete client;
		}
	}
};

AsyncHTTPClient httpClient;


// the setup function runs once when you press reset or power the board
void setup() {
	EEPROM.begin(512);
	SPIFFS.begin();

	// initialize digital pin 13 as an output.
	pinMode(led, OUTPUT);

	Wire.begin(0, 2); // SDA = 0, SCL = 2
	delay(100);

	MDNS.begin(hostName);

	WiFiConnect();

	MDNS.addService("http", "tcp", 80);

	StartOTA();

	httpClient.initialize(readStringFromEEPROM(URL_START, URL_MAX));

	server.on("/", HTTP_GET, mainHandler);
	server.on("/system", HTTP_GET, systemHandler);
	server.on("/set_wifi", HTTP_POST, wifiHandler);

	// attach AsyncWebSocket
	ws.onEvent(onEvent);
	server.addHandler(&ws);

	server.begin();
	ws.enable(true);
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

	if (WiFi.status() == WL_CONNECTED) {
		// See if it is time to update the Clock
		if (now - lastUpdate > 60000) {
			lastUpdate = now;
			httpClient.makeRequest(sendTimeToI2C, sendStatus);
		}
	}

	if (now - lastBlink > d) {
		lastBlink = now;
		ledState = ledState ^ 1;
		digitalWrite(led, ledState);
	}
}

void sendTimeToI2C() {
	Wire.beginTransmission(I2C_TARGET);
	Wire.write(I2C_TIME_UPDATE); // Command
	String body = httpClient.getBody();
	sendStatus(body);
	Wire.write(body.c_str());
	int error = Wire.endTransmission();
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
		broadcastUpdate(_key, value);
	} else if (strcmp("date_format", key) == 0) {
		date_format = value == "true";
		broadcastUpdate(_key, value);
	} else if (strcmp("time_format", key) == 0) {
		time_format = value == "true";
		broadcastUpdate(_key, value);
	} else if (strcmp("leading_zero", key) == 0) {
		leading_zero = value == "true";
		broadcastUpdate(_key, value);
	} else if (strcmp("display_on", key) == 0) {
		display_on = value.toInt();
		broadcastUpdate(_key, value);
	} else if (strcmp("display_off", key) == 0) {
		display_off = value.toInt();
		broadcastUpdate(_key, value);
	} else if (strcmp("backlight", key) == 0) {
		backlight = value == "true";
		broadcastUpdate(_key, value);
	} else if (strcmp("hue_cycling", key) == 0) {
		hue_cycling = value == "true";
		broadcastUpdate(_key, value);
	} else if (strcmp("cycle_time", key) == 0) {
		cycle_time = value.toInt();
		broadcastUpdate(_key, value);
	} else if (strcmp("hue", key) == 0) {
		hue = value.toInt();
		broadcastUpdate(_key, value);
	} else if (strcmp("saturation", key) == 0) {
		saturation = value.toInt();
		broadcastUpdate(_key, value);
	} else if (strcmp("brightness", key) == 0) {
		brightness = value.toInt();
		broadcastUpdate(_key, value);
	} else if (strcmp("alarm_set", key) == 0) {
		alarm_set = value == "true";
		broadcastUpdate(_key, value);
	} else if (strcmp("alarm_time", key) == 0) {
		alarm_time = value;
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

void mainHandler(AsyncWebServerRequest *request) {
	DEBUG("Got main request");
	if (WiFi.status() != WL_CONNECTED) {
		request->send(SPIFFS, "/main_ap.html");
	} else {
		request->send(SPIFFS, "/index.html");
	}
}

/**
   Get the header for a 2 column table
*/
String getTableHead2Col(String tableHeader, String col1Header, String col2Header) {
  String tableHead = "<div class=\"container\" role=\"main\"><h3 class=\"sub-header\">";
  tableHead += tableHeader;
  tableHead += "</h3><div class=\"table-responsive\"><table class=\"table table-striped\"><thead><tr><th>";
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
	String response;
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

	request->redirect("/");

	WiFiConnect();
}

void StartOTA() {
#ifdef OTA
	// Port defaults to 8266
	ArduinoOTA.setPort(8266);

	// Hostname defaults to esp8266-[ChipID]
	ArduinoOTA.setHostname(hostName);

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
	WiFi.mode(WIFI_AP_STA);
	WiFi.softAP(hostName);

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

