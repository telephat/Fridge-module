#include <Arduino.h>
#include <WiFi.h>               // ESP32 Core WiFi Library    
#include <WiFiClient.h>
#include <WebServer.h>
#include <WiFiUdp.h>
#include <SNMP_Agent.h>
#include <SNMPTrap.h>
#include <ElegantOTA.h>

#include <DallasTemperature.h>
#include <OneWire.h>

#define ONE_WIRE_BUS 22
#define RELAY_PIN 32

OneWire oneWire(ONE_WIRE_BUS);
WebServer server(80); //Webserver
// Pass our oneWire reference to Dallas Temperature.
DallasTemperature sensors(&oneWire);
DeviceAddress ds_term1, ds_term2;

const char* ssid = "bg-lsp";
const char* password = "19316632";

WiFiUDP udp;
SNMPAgent snmp = SNMPAgent("public");

unsigned long ota_progress_millis = 0; //elegant OTA
int snmp_ds_temp1 = 255;
int snmp_ds_temp2 = 255;
char* str_ds_temp1;
char* str_ds_temp2;
int ds_timer = 0;
int var_ds_temp1;
int var_ds_temp2;
int freezerTemp; // секция термостатов
int coolerTemp;
int freezerTolerance = 3;//
int coolerTolerance = 3;//вариатор температуры
int freezerCooldown = 10;//кулдаун компрессора в минутах
int coolerCooldown = 10;//кулдаун компрессора
unsigned long uptime; //uptime в секундах
int overloadcounter;//счётчик обнуления millis()

void setupWebserver();
void onOTAEnd(bool success);
void onOTAStart();
void onOTAProgress(size_t current, size_t final);
void setupWiFi();
void setupSNMP();
float get_ds_temperature(DeviceAddress deviceAddress);

void setup() {
  Serial.begin(9600);
  sensors.begin(); //initialise DS18b20 sensors
  Serial.print("Sensors detected: ");
  Serial.print(sensors.getDeviceCount(), DEC);
  if (!sensors.getAddress(ds_term1, 0)) Serial.println("Unable to find address for Device 0");
  if (!sensors.getAddress(ds_term2, 1)) Serial.println("Unable to find address for Device 0");
  Serial.println();

  str_ds_temp1 = (char*)malloc(6);
  str_ds_temp2 = (char*)malloc(6);
  setupWiFi();
  setupSNMP();
  setupWebserver();
  sensors.requestTemperatures(); //initial request
  delay(100);
  var_ds_temp1 = get_ds_temperature(ds_term1);
  var_ds_temp2 = get_ds_temperature(ds_term2);
  delay(500);
}


void loop() {
  
  if (ds_timer > 10) {
    sensors.requestTemperatures();
    var_ds_temp1 = get_ds_temperature(ds_term1);
    var_ds_temp2 = get_ds_temperature(ds_term2);
    Serial.print("t1: ");
    Serial.print(var_ds_temp1);
    Serial.print(" | t2: ");
    Serial.print(var_ds_temp2);
    Serial.println();
    ds_timer = 0;  
  }
  
  snmp_ds_temp1=int(var_ds_temp1);
  snmp_ds_temp2=int(var_ds_temp2);
  
  snmp.loop();
  server.handleClient();
  ElegantOTA.loop();
  ds_timer++; // таймер для получения температуры, чтобы избежать delay(100) в лупе
}

void setupWebserver() {
  server.on("/", []() {
    server.send(200, "text/plain", "Hi! This is ElegantOTA Demo.");
  });
  ElegantOTA.begin(&server);    // Start ElegantOTA
  // ElegantOTA callbacks
  ElegantOTA.onStart(onOTAStart);
  ElegantOTA.onProgress(onOTAProgress);
  ElegantOTA.onEnd(onOTAEnd);

  server.begin();
  Serial.println("HTTP server started");
}

void onOTAEnd(bool success) {
  // Log when OTA has finished
  if (success) {
    Serial.println("OTA update finished successfully!");
  } else {
    Serial.println("There was an error during OTA update!");
  }
  // <Add your own code here>
}

void onOTAStart() {
  // Log when OTA has started
  Serial.println("OTA update started!");
  // <Add your own code here>
}

void onOTAProgress(size_t current, size_t final) {
  // Log every 1 second
  if (millis() - ota_progress_millis > 1000) {
    ota_progress_millis = millis();
    Serial.printf("OTA Progress Current: %u bytes, Final: %u bytes\n", current, final);
  }
}

float get_ds_temperature(DeviceAddress deviceAddress) {
  float tempC = sensors.getTempC(deviceAddress);
  if (tempC == DEVICE_DISCONNECTED_C) {
    return -255;
    delay(50);
  }
  else {return tempC;}
}

void setupWiFi() {
  WiFi.begin(ssid, password);             // Connect to the network
  while (WiFi.status() != WL_CONNECTED) { // Wait for the Wi-Fi to connect
    delay(500);
    Serial.print('.');
  }
  Serial.println("Connection established");  
  Serial.print("IP address:\t");
  Serial.println(WiFi.localIP());
}

void setupSNMP() {
  snmp.setUDP(&udp);
  // int testnumber = 5;
  snmp.addIntegerHandler(".1.3.6.1.4.1.5.0", &snmp_ds_temp1);
  snmp.addIntegerHandler(".1.3.6.1.4.1.5.1", &snmp_ds_temp2);

  snmp.begin();
}
