#include <ESP8266WiFi.h>
#include <ArduinoJson.h>
#include <SocketIOclient.h>
#include <EEPROM.h>
#include <ESP8266WebServer.h>
#include <ArduinoOTA.h>

const int soilSensorPin = A0;
const int pumpPin = D1;

struct WifiConf
{
  char wifi_ssid[50];
  char wifi_password[50];
  char wifi_hostname[50];
  char cstr_terminator = 0;
};

SocketIOclient socketIO;
WifiConf wifiConf;
ESP8266WebServer server(80);

const char *AP_ssid = "ESP8266_fallback_AP_1";
const char *AP_password = "SuperSecretPassword";
IPAddress AP_IP = IPAddress(10, 1, 1, 1);
IPAddress AP_subnet = IPAddress(255, 255, 255, 0);

String webSocketsServer = "grasutu-pc.local";
int webSocketsPort = 3000;

unsigned long soilMoistureTimestamp = 0;
unsigned long keepAliveTimestamp = 0;
unsigned long lastWateringTimestamp = 0;
uint64_t keepAliveInterval = 10000;
uint64_t soilMoistureInterval = 10000;
uint64_t wateringInterval = 60000;
uint64_t wateringDuration = 5000;

float soilMoisture = 0;

String ipAddress;
bool connectedToWifi = false;

void readWifiConf()
{
  for (int i = 0; i < sizeof(wifiConf); i++)
  {
    ((char *)(&wifiConf))[i] = char(EEPROM.read(i));
  }
  wifiConf.cstr_terminator = 0;
}

void writeWifiConf()
{
  for (int i = 0; i < sizeof(wifiConf); i++)
  {
    EEPROM.write(i, ((char *)(&wifiConf))[i]);
  }
  EEPROM.commit();
}

void setUpAccessPoint()
{
  Serial.println("Setting up access point.");
  Serial.printf("SSID: %s\n", AP_ssid);
  Serial.printf("Password: %s\n", AP_password);

  WiFi.mode(WIFI_AP_STA);
  WiFi.softAPConfig(AP_IP, AP_IP, AP_subnet);
  if (WiFi.softAP(AP_ssid, AP_password))
  {
    Serial.print("Ready. Access point IP: ");
    Serial.println(WiFi.softAPIP());
  }
  else
  {
    Serial.println("Setting up access point failed!");
  }
}

void setUpWebServer()
{
  server.on("/", handleWebServerRequest);
  server.begin();
}

void handleWebServerRequest()
{
  bool save = false;

  if (server.hasArg("hostname") && server.hasArg("ssid") && server.hasArg("password"))
  {
    server.arg("hostname").toCharArray(
      wifiConf.wifi_hostname,
      sizeof(wifiConf.wifi_hostname)
    );
    server.arg("ssid").toCharArray(
      wifiConf.wifi_ssid,
      sizeof(wifiConf.wifi_ssid)
    );
    server.arg("password").toCharArray(
      wifiConf.wifi_password, 
      sizeof(wifiConf.wifi_password)
    );

    Serial.println(server.arg("ssid"));
    Serial.println(wifiConf.wifi_ssid);

    writeWifiConf();
    save = true;
  }

  String message = "";
  message += "<!DOCTYPE html>";
  message += "<html>";
  message += "<head>";
  message += "<title>ESP8266 conf</title>";
  message += "</head>";
  message += "<body>";
  if (save)
  {
    message += "<div>Saved! Rebooting...</div>";
  }
  else
  {
    message += "<h1>Wi-Fi conf</h1>";
    message += "<form action='/' method='POST'>";
    message += "<div>Hostname:</div>";
    message += "<div><input type='text' name='hostname' value='" + String(wifiConf.wifi_hostname) + "'/></div>";
    message += "<div>SSID:</div>";
    message += "<div><input type='text' name='ssid' value='" + String(wifiConf.wifi_ssid) + "'/></div>";
    message += "<div>Password:</div>";
    message += "<div><input type='password' name='password' value='" + String(wifiConf.wifi_password) + "'/></div>";
    message += "<div><input type='submit' value='Save'/></div>";
    message += "</form>";
  }
  message += "</body>";
  message += "</html>";
  server.send(200, "text/html", message);

  if (save)
  {
    Serial.println("Wi-Fi conf saved. Rebooting...");
    delay(1000);
    ESP.restart();
  }
}

void setUpOverTheAirProgramming()
{

  // Change OTA port.
  // Default: 8266
  // ArduinoOTA.setPort(8266);

  // Change the name of how it is going to
  // show up in Arduino IDE.
  // Default: esp8266-[ChipID]
  // ArduinoOTA.setHostname("myesp8266");

  // Re-programming passowrd.
  // No password by default.
  ArduinoOTA.setPassword("bibistrocel");

  ArduinoOTA.begin();
}

void sendUpdateToServer(uint64_t now, String event_name, JsonObject updateObj)
{
  DynamicJsonDocument doc(1024);
  JsonArray array = doc.to<JsonArray>();

  // add evnet name
  // Hint: socket.on('event_name', ....
  array.add(event_name);

  array.add(updateObj);

  String output;
  serializeJson(doc, output);

  // Send event
  socketIO.sendEVENT(output);
  Serial.println(output);
}

void sendKeepAlive()
{
  uint64_t now = millis();
  const size_t CAPACITY = JSON_OBJECT_SIZE(3);
  StaticJsonDocument<CAPACITY> doc;
  JsonObject updateObj = doc.to<JsonObject>();

  if (now - keepAliveTimestamp > keepAliveInterval)
  {
    keepAliveTimestamp = now;
    updateObj["now"] = (uint32_t)now;
    updateObj["ip"] = ipAddress;
    updateObj["hostname"] = wifiConf.wifi_hostname;

    sendUpdateToServer(now, "keep_alive", updateObj);
  }
}

void socketIOEvent(socketIOmessageType_t type, uint8_t *payload, size_t length)
{
  switch (type)
  {
  case sIOtype_DISCONNECT:
    Serial.printf("[IOc] Disconnected!\n");
    break;
  case sIOtype_CONNECT:
    Serial.printf("[IOc] Connected to url: %s\n", payload);

    // join default namespace (no auto join in Socket.IO V3)
    socketIO.send(sIOtype_CONNECT, "/");
    break;
  case sIOtype_EVENT:
  {
    char *sptr = NULL;
    int id = strtol((char *)payload, &sptr, 10);
    Serial.printf("[IOc] get event: %s id: %d\n", payload, id);
    if (id)
    {
      payload = (uint8_t *)sptr;
    }
    DynamicJsonDocument doc(1024);
    DeserializationError error = deserializeJson(doc, payload, length);
    if (error)
    {
      Serial.print(F("deserializeJson() failed: "));
      Serial.println(error.c_str());
      return;
    }

    String eventName = doc[0];
    Serial.printf("[IOc] event name: %s\n", eventName.c_str());

    // Message Includes a ID for a ACK (callback)
    if (id)
    {
      // creat JSON message for Socket.IO (ack)
      DynamicJsonDocument docOut(1024);
      JsonArray array = docOut.to<JsonArray>();

      // add payload (parameters) for the ack (callback function)
      JsonObject param1 = array.createNestedObject();
      param1["now"] = millis();

      // JSON to String (serializion)
      String output;
      output += id;
      serializeJson(docOut, output);

      // Send event
      socketIO.send(sIOtype_ACK, output);
    }
  }
  break;
  case sIOtype_ACK:
    Serial.printf("[IOc] get ack: %u\n", length);
    break;
  case sIOtype_ERROR:
    Serial.printf("[IOc] get error: %u\n", length);
    break;
  case sIOtype_BINARY_EVENT:
    Serial.printf("[IOc] get binary: %u\n", length);
    break;
  case sIOtype_BINARY_ACK:
    Serial.printf("[IOc] get binary ack: %u\n", length);
    break;
  }
}

void connectToWiFi()
{
  Serial.println("Connecting to the WiFi");
  WiFi.mode(WIFI_STA);
  WiFi.setHostname(wifiConf.wifi_hostname);
  WiFi.begin(wifiConf.wifi_ssid, wifiConf.wifi_password);

  Serial.println("Waiting for connection");
  monitorWiFi();
}

void connectToWebSocket()
{
  if (!connectedToWifi)
    return;

  Serial.println("Connecting to the WebSocket");
  // DEBUG_WEBSOCKETS("Connecting to: grasutu-pc.local:3000");
  socketIO.begin(webSocketsServer, webSocketsPort);
  socketIO.onEvent(socketIOEvent);
}

void setup()
{
  pinMode(pumpPin, OUTPUT);
  Serial.begin(115200);
  Serial.setDebugOutput(true);

  EEPROM.begin(512);
  readWifiConf();

  connectToWiFi();

  if (!connectedToWifi) {
    setUpAccessPoint();
  }

  setUpWebServer();
  setUpOverTheAirProgramming();
}

void loopIfConnectedToWifi()
{
  if (!connectedToWifi)
    return;

  loopIfConnectedToWS();
}

void loopIfConnectedToWS()
{
  socketIO.loop();

  if (!socketIO.isConnected())
    return;

  sendKeepAlive();
}

void monitorWiFi()
{
  if (WiFi.waitForConnectResult() != WL_CONNECTED)
  {

    if (connectedToWifi == true)
    {
      connectedToWifi = false;
      Serial.print("Looking for WiFi ");
    }

    Serial.print(".");
    delay(500);
  }
  else if (connectedToWifi == false)
  {
    connectedToWifi = true;
    Serial.printf(" connected to %s\n", WiFi.SSID().c_str());
    ipAddress = WiFi.localIP().toString();
    connectToWebSocket();
  }
}

void loop()
{
  monitorWiFi();
  loopIfConnectedToWifi();
  ArduinoOTA.handle();
  server.handleClient();
  readSoilMoisture();
  waterThePlants();
}

void readSoilMoisture()
{
  uint64_t now = millis();

  if (now - soilMoistureTimestamp > soilMoistureInterval)
  {
    const size_t CAPACITY = JSON_OBJECT_SIZE(3);
    StaticJsonDocument<CAPACITY> doc;
    JsonObject updateObj = doc.to<JsonObject>();
    float moisture_percentage = ( 100.00 - ( (analogRead(soilSensorPin)/1023.00) * 100.00 ) );
    soilMoistureTimestamp = now;

    Serial.print("Soil Moisture(in Percentage) = ");
    Serial.print(moisture_percentage);
    Serial.println("%");
    updateObj["now"] = (uint32_t)now;
    updateObj["ip"] = ipAddress;
    updateObj["hostname"] = wifiConf.wifi_hostname;
    updateObj["soil_moisture"] = moisture_percentage;
    soilMoisture = moisture_percentage;

    if (socketIO.isConnected())
      sendUpdateToServer(now, "soil_moisture", updateObj);
  }
}

void waterThePlants()
{
  if (soilMoisture > 50)
    return;

  uint64_t now = millis();

  if (now - lastWateringTimestamp > wateringInterval)
  {
    const size_t CAPACITY = JSON_OBJECT_SIZE(3);
    StaticJsonDocument<CAPACITY> doc;
    JsonObject updateObj = doc.to<JsonObject>();
    updateObj["now"] = (uint32_t)now;
    updateObj["ip"] = ipAddress;
    updateObj["hostname"] = wifiConf.wifi_hostname;
    updateObj["duration"] = wateringDuration;
    lastWateringTimestamp = now;
    Serial.println("Watering the plants");
    digitalWrite(pumpPin, HIGH);
    delay(wateringDuration);
    digitalWrite(pumpPin, LOW);

    if (socketIO.isConnected())
      sendUpdateToServer(now, "watering_plants", updateObj);
  }
}
