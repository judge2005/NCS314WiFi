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
#include <ArduinoJson.h>

#define I2C_TARGET 0x23
#define I2C_TIME_UPDATE 0x0

#define SSID_START 0x0
#define SSID_MAX 0x20
#define PASSWORD_START 0x20
#define PASSWORD_MAX 0x40
#define URL_START 0x40
#define URL_MAX 0x100

#define DEBUG(...) { Wire.beginTransmission(I2C_TARGET); Wire.write(__VA_ARGS__); Wire.endTransmission(); }
//#define DEBUG(...) {}

AsyncWebServer server(80);

int led = 1;
int d = 500;
int count = 1;

const char *hostName = "in-14-shield";
String ssid = "FiOS-3RA3E";
String password = "son8638woman458wag";

IPAddress apIP;
IPAddress myIP ;

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

	void initialize(String url) {
		bool hasPort = false;

		// check for : (http: or https:
		int index = url.indexOf(':');
		if(index < 0) {
			initialized = false;	// This is not a URL
		}

		protocol = url.substring(0, index);
		url.remove(0, (index + 3)); // remove http:// or https://

		index = url.indexOf('/');
		String hostPart = url.substring(0, index);
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
		index = hostPart.indexOf(':');
		if(index >= 0) {
			host = hostPart.substring(0, index); // hostname
			host.remove(0, (index + 1)); // remove hostname + :
			port = host.toInt(); // get port
		} else {
			host = hostPart;
		}
		uri = url;
		if (protocol != "http") {
			initialized = false;
		}

		request = "GET " + uri + " HTTP/1.1\r\nHost: " + host + "\r\n\r\n";

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

	void makeRequest(void (*success)()) {
		if (!initialized)
			return;

		if (aClient) //client already exists
			return;

		aClient = new AsyncClient();
		if (!aClient) //could not allocate client
			return;

		onSuccess = success;

		aClient->onError(clientError, this);

		aClient->onConnect(clientConnect, this);

		if (!aClient->connect(host.c_str(), port)) {
			DEBUG("Connect Fail");
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
	WiFiConnect();
	StartOTA();

	httpClient.initialize(readStringFromEEPROM(URL_START, URL_MAX));

	server.on("/", HTTP_GET, mainHandler);
	server.on("/heap", HTTP_GET, heapHandler);
	server.on("/status", HTTP_GET, statusHandler);

	server.on("/set_wifi", HTTP_POST, wifiHandler);

	server.begin();
}

unsigned long lastUpdate = 0;
unsigned long lastWiFi = 0;
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
			httpClient.makeRequest(sendTimeToI2C);
		}
	}

	if (now - lastWiFi > 500) {
		lastWiFi = now;
		WiFiConnect();
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
	Wire.write(httpClient.getBody().c_str());
	int error = Wire.endTransmission();
}

void mainHandler(AsyncWebServerRequest *request) {
	DEBUG("Got main request");
	if (myIP == IPAddress()) {
		request->send(SPIFFS, "/main_ap.html");
	} else {
		request->send(SPIFFS, "/index.html");
	}
}

void heapHandler(AsyncWebServerRequest *request) {
	DEBUG("Got heap request");
	request->send(200, "text/plain", String(ESP.getFreeHeap()));
}

void wifiHandler(AsyncWebServerRequest *request) {
	DEBUG("Got set wifi request");
	AsyncWebParameter* p = request->getParam("ssid", true);
	if (p) {
		String ssid = p->value();
		writeStringToEEPROM(ssid, SSID_START, SSID_MAX);
	} else {
		writeStringToEEPROM("", SSID_START, SSID_MAX);
	}

	p = request->getParam("password", true);
	if (p) {
		String password = p->value();
		writeStringToEEPROM(password, PASSWORD_START, PASSWORD_MAX);
	} else {
		writeStringToEEPROM("", PASSWORD_START, PASSWORD_MAX);
	}

	request->redirect("/");

	WiFi.disconnect(false);
}

void statusHandler(AsyncWebServerRequest *request) {
	DEBUG("Got status request");
	request->send(200, "text/plain", httpClient.getBody());
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

/**
 * Connect to wifi:
 * 1. If already connected, return
 * 2. Don't wait for connection, that just blocks stuff
 * 3. If SSID is empty, just be an AP
 */
wl_status_t oldStatus;

void WiFiConnect() {
	if (WiFi.status() == WL_CONNECTED) {
		if (oldStatus != WL_CONNECTED) {
			oldStatus = WL_CONNECTED;
			apIP = WiFi.softAPIP();
			myIP = WiFi.localIP();

			DEBUG(myIP.toString().c_str());
			DEBUG(WiFi.macAddress().c_str());

			MDNS.addService("http", "tcp", 80);
		}

		return;
	}

	oldStatus = WL_DISCONNECTED;

	WiFi.hostname(hostName);
	WiFi.mode(WIFI_AP_STA);
	initializeCredentials();

	if (ssid.length() > 0) {
		WiFi.softAP(hostName);

		if (password.length() > 0) {
			WiFi.begin(ssid.c_str(), password.c_str());
		} else {
			WiFi.begin(ssid.c_str());
		}

		if (WiFi.status() != WL_CONNECTED) {
			DEBUG("STA: Failed!\n");
			return;
		}

		DEBUG("Connected");
	} else {
		WiFi.mode(WIFI_AP);
		apIP = WiFi.softAPIP();
		myIP = IPAddress();
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
	String esid = "";
	for (int i = start; i < end; i++) {
		byte readByte = EEPROM.read(i);
		if (readByte == 0) {
			break;
		}
		esid += char(readByte);
	}
	return esid;
}

void writeStringToEEPROM(String s, int start, int max) {
	int end = start + max;
	for (int i = start; i < end; i++) {
	    if (i < s.length()) {
	      EEPROM.write(i, s[i]);
	    } else {
	      EEPROM.write(i, 0);
	      break;
	    }
	}
}

