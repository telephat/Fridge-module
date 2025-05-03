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
#include <time.h>

#define ONE_WIRE_BUS 22
#define FREEZER_PIN 21


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
 // секция термостатов
int64_t uptime; //uptime в секундах
int64_t basetimer;

boolean freezer_status = false; // Freezer setup
int freezerTemp;
int freezer_uptime = 0;
int freezerTargetT = -15;
int freezer_cd = 0;
int freezer_turncounter = 0;
int freezer_totaluptime = 0;
const int freezerTolerance = 3;//
const int freezer_cd_target = 300; //

void setupWebserver();
void onOTAEnd(bool success);
void onOTAStart();
void onOTAProgress(size_t current, size_t final);
void setupWiFi();
void setupSNMP();
float get_ds_temperature(DeviceAddress deviceAddress);
String get_strUptime(int64_t seconds);
void eachSecond();
void printtUptime();
void printtemperature();

void freezerOn();
void freezedOff();
void printFreezerStatus();

void setup() {
  Serial.begin(9600);
  sensors.begin(); //initialise DS18b20 sensors
  delay(500); //delay to sensors init
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
  int64_t upTimeUS = esp_timer_get_time();
  basetimer = upTimeUS / 1000000;
}

void loop() {
  int64_t upTimeUS = esp_timer_get_time(); // in microseconds
  uptime = upTimeUS / 1000000;
  if ((uptime - basetimer) >= 1) {
    basetimer = uptime;
    eachSecond(); // <=== Обработчик всего
  }
//No temperature update!
  snmp_ds_temp1=int(var_ds_temp1);
  snmp_ds_temp2=int(var_ds_temp2);
  snmp.loop();
  server.handleClient();
  ElegantOTA.loop();
}

void freezerOn() {
  digitalWrite(FREEZER_PIN, HIGH);
  freezer_status = true;
}

void freezerOff() {
  digitalWrite(FREEZER_PIN, LOW);
  freezer_status = false;
}

void printUptime() {
  Serial.print(get_strUptime(uptime));
}

void printtemperature() {
  Serial.print("t1: ");
  Serial.print(var_ds_temp1);
  Serial.print(" | t2: ");
  Serial.print(var_ds_temp2);
}

String get_strUptime(int64_t seconds) {
  char buf[50];
  uint32_t days = (uint32_t)seconds/86400;
  uint32_t hr=(uint32_t)seconds % 86400 /  3600;
  uint32_t min=(uint32_t)seconds %  3600 / 60;
  uint32_t sec=(uint32_t)seconds % 60;
  snprintf (buf,sizeof(buf),"%dd %d:%02d:%02d", days, hr, min, sec);
  String upTime = String(buf);
  //Serial.println(upTime);
  return buf;
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

void checkFreezer() { //Freezer routine
  if (freezer_status) { //если включён
    freezer_uptime++; // пошёл аптайм
    freezer_totaluptime++;
  }
  else {                //если выключен
    freezer_uptime = 0; //ресет аптайма
    freezer_cd--; //отсчитываем кулдаун.
  }
  
  if (freezerTemp >= freezerTargetT) {

  }
}
void printFreezerStatus() {
  Serial.print("Freezer temperature: "); Serial.print(var_ds_temp1); Serial.print(" (");Serial.print(freezerTargetT);Serial.print(")");Serial.println();
  Serial.print("total uptime: "); Serial.print(freezer_totaluptime); Serial.println();
  Serial.print("turn on counter: ");Serial.print(freezer_turncounter); Serial.println();
  Serial.print("Freezer status: "); if (freezer_status) {Serial.print("ON");} else {Serial.print("OFF");} Serial.println();
}

void eachSecond(){
  //checkFreezer();
  
  //printUptime();
  //printtemperature();
  printFreezerStatus();
  //Serial.println();
}