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
#define FREEZER_PIN 12
#define COOLER_PIN 13
//#define CONTROL_PIN 12


OneWire oneWire(ONE_WIRE_BUS);
WebServer server(80); //Webserver
// Pass our oneWire reference to Dallas Temperature.
DallasTemperature sensors(&oneWire);
DeviceAddress ds_term1, ds_term2, ds_term3;

const char* ssid = "bg-lsp";
const char* password = "19316632";

WiFiUDP udp;
SNMPAgent snmp = SNMPAgent("public");

unsigned long ota_progress_millis = 0; //elegant OTA
int snmp_ds_temp1 = -255;
int snmp_ds_temp2 = -255;
int snmp_ds_temp3 = -255;
char* str_ds_temp1;
char* str_ds_temp2;
char* str_ds_temp3;
int ds_timer = 0;
int var_ds_temp1;
int var_ds_temp2;
int var_ds_temp3;
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

boolean cooler_status = false; // Cooler setup

int simulate_direction = 1; // для симулятора работы компрессора, 1 - это камера нагревается (компрессор не работает), 2 - остужается
float simulate_gain = 0.1; // градусов в секунду
float simulated_temp;

void setupWebserver();
void onOTAEnd(bool success);
void onOTAStart();
void onOTAProgress(size_t current, size_t final);
void setupWiFi();
void setupSNMP();
float get_ds_temperature(DeviceAddress deviceAddress);
String get_strUptime(int64_t seconds);
void eachSecond();
void printUptime();
void printtemperature();
void checkFreezer();
void freezerOn();
void freezerOff();
void webfreezerOn();
void webfreezerOff();
void printFreezerStatus();
void checkCooler();
void coolerOn();
void coolerOff();
void webcoolerOn();
void webcoolerOff();
void printCoolerStatus();
void simulateCompressor();
String SendHTML(uint8_t led1stat,uint8_t led2stat);
void check_weboverride();

void setup() {
  pinMode(12, OUTPUT);
  pinMode(13, OUTPUT);
  Serial.begin(9600);
  sensors.begin(); //initialise DS18b20 sensors
  delay(500); //delay to sensors init
  Serial.print("Sensors detected: ");
  Serial.print(sensors.getDeviceCount(), DEC);
  if (!sensors.getAddress(ds_term1, 0)) Serial.println("Unable to find address for Device 0");
  if (!sensors.getAddress(ds_term2, 1)) Serial.println("Unable to find address for Device 1");
  if (!sensors.getAddress(ds_term3, 2)) Serial.println("Unable to find address for Device 2");
  Serial.println();
  delay(1000);
  str_ds_temp1 = (char*)malloc(6);
  str_ds_temp2 = (char*)malloc(6);
  setupWiFi();
  setupSNMP();
  setupWebserver();
  sensors.requestTemperatures(); //initial request
  delay(100);
  var_ds_temp1 = get_ds_temperature(ds_term1);
  var_ds_temp2 = get_ds_temperature(ds_term2);
  var_ds_temp3 = get_ds_temperature(ds_term2);
  delay(500);
  int64_t upTimeUS = esp_timer_get_time();
  basetimer = upTimeUS / 1000000;

  // // **** SIMULATION ****** //
  // var_ds_temp1 = 0;
  // simulated_temp = var_ds_temp1 * 1.0;
  
}

void loop() {
  int64_t upTimeUS = esp_timer_get_time(); // in microseconds
  uptime = upTimeUS / 1000000;
  if ((uptime - basetimer) >= 1) {
    basetimer = uptime;
    eachSecond(); // <=== Обработчик всего
    sensors.requestTemperatures();
    var_ds_temp1 = get_ds_temperature(ds_term1);
    var_ds_temp2 = get_ds_temperature(ds_term2);
    var_ds_temp3 = get_ds_temperature(ds_term3);
    snmp_ds_temp1=int(var_ds_temp1);
    snmp_ds_temp2=int(var_ds_temp2);
    snmp_ds_temp3=int(var_ds_temp3);

  }

  //

  snmp.loop();
  server.handleClient();
  ElegantOTA.loop();
  check_weboverride();
}

void check_weboverride() {
  if (freezer_status) freezerOn();
  else freezerOff();
  if (cooler_status) coolerOn();
  else coolerOff();
}

void checkCooler() {
  if (freezer_status) { //если включён
    freezer_uptime++; // пошёл аптайм
    freezer_totaluptime++;
  }
  else {                //если выключен
    freezer_uptime = 0; //ресет аптайма
    if (freezer_cd > 0) { //если кд есть 
      freezer_cd--; //отсчитываем кулдаун.
    }
  }

  if ((freezer_cd == 0) and (freezer_status == false) and (freezerTemp >= freezerTargetT-freezerTolerance)) {
    //Если нет КД, и компрессор выключен, и температура выше, чем заданная, то
    freezerOn();
    freezer_cd = freezer_cd_target;
    Serial.println("Turning freezer ON");
  }
  else if (freezerTemp <= freezerTargetT and freezer_status == true)
  //в противносм случае, если температура нужная и компрессор включён
  {
    freezerOff();
    Serial.println("Turnin freezer OFF");
  }
  Serial.println("------");
  Serial.println("Переменные: ");
  Serial.print("freezer_cd: ");Serial.print(freezer_cd);Serial.print(" freezer_status: ");Serial.print(freezer_status);Serial.print(" freezerTemp: ");Serial.print(freezerTemp);
  Serial.println();
  Serial.println("------");
  }

  void printCoolerStatus() {
  Serial.print("Freezer temperature: "); Serial.print(var_ds_temp1); Serial.print(" (");Serial.print(freezerTargetT);Serial.print(")");Serial.println();
  Serial.print("total uptime: "); Serial.print(freezer_totaluptime); Serial.println();
  Serial.print("turn on counter: ");Serial.print(freezer_turncounter); Serial.println();
  Serial.print("Freezer status: "); if (freezer_status) {Serial.print("ON");} else {Serial.print("OFF");} Serial.println();
  }


void coolerOn() {
  digitalWrite(COOLER_PIN, HIGH);
  freezer_status = true;
}

void webcoolerOn() {
  cooler_status = true;
  Serial.println("Web request to turn cooler ON");
  server.send(200, "text/html", SendHTML(true, freezer_status));
}

void coolerOff() {
  digitalWrite(COOLER_PIN, LOW);
  freezer_status = false;
}

void webcoolerOff() {
  cooler_status = false;
  Serial.println("Web request to turn cooler OFF");
  server.send(200, "text/html", SendHTML(false, freezer_status));
}

void printCoolerStatus();

void freezerOn() {
  digitalWrite(FREEZER_PIN, HIGH);
  freezer_status = true;
}
void webfreezerOn() {
  freezer_status = true;
  Serial.println("Web request to turn freezer ON");
  server.send(200, "text/html", SendHTML(cooler_status, true));
}

void freezerOff() {
  digitalWrite(FREEZER_PIN, LOW);
  freezer_status = false;
}
void webfreezerOff() {
  freezer_status = false;
  Serial.println("Web request to turn freezer OFF");
  server.send(200, "text/html", SendHTML(cooler_status, false));
}

void printUptime() {
  Serial.print(get_strUptime(uptime));
}

void printtemperature() {
  Serial.print("t1: ");
  Serial.print(var_ds_temp1);
  Serial.print(" | t2: ");
  Serial.print(var_ds_temp2);
  Serial.print(" | t3: ");
  Serial.print(var_ds_temp3);
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

void simulateCompressor(){ // **** SIMULATION
  String status;
  freezerTemp = simulated_temp; // <== Убрать!
  if (freezer_status) {
    simulated_temp = simulated_temp - simulate_gain;
    status = "Охаждаем";
  }
  else {
    simulated_temp = simulated_temp + simulate_gain;
    status = "Выключен";
  }
  var_ds_temp1 = simulated_temp;
  Serial.print("Compressor simulation hit: ");
  Serial.print(status);
  Serial.println();
}

void setupWebserver() {
  server.on("/", []() {
    server.send(200, "text/html", SendHTML(cooler_status, freezer_status));
  });
  server.on("/cooleron", webcoolerOn);
  server.on("/cooleroff", webcoolerOff);
  server.on("/freezeron", webfreezerOn);
  server.on("/freezeroff", webfreezerOff);


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
  snmp.addIntegerHandler(".1.3.6.1.4.1.5.2", &snmp_ds_temp3);
  snmp.begin();
}

void checkFreezer() { //Freezer routine
  if (freezer_status) { //если включён
    freezer_uptime++; // пошёл аптайм
    freezer_totaluptime++;
  }
  else {                //если выключен
    freezer_uptime = 0; //ресет аптайма
    if (freezer_cd > 0) { //если кд есть 
      freezer_cd--; //отсчитываем кулдаун.
    }
  }
  
  if ((freezer_cd == 0) and (freezer_status == false) and (freezerTemp >= freezerTargetT-freezerTolerance)) {
    //Если нет КД, и компрессор выключен, и температура выше, чем заданная, то
    freezerOn();
    freezer_cd = freezer_cd_target;
    Serial.println("Turning freezer ON");
  }
  else if (freezerTemp <= freezerTargetT and freezer_status == true)
  //в противносм случае, если температура нужная и компрессор включён
  {
    freezerOff();
    Serial.println("Turnin freezer OFF");
  }
  Serial.println("------");
  Serial.println("Переменные: ");
  Serial.print("freezer_cd: ");Serial.print(freezer_cd);Serial.print(" freezer_status: ");Serial.print(freezer_status);Serial.print(" freezerTemp: ");Serial.print(freezerTemp);
  Serial.println();
  Serial.println("------");

}
int counter1 = 1;
void printFreezerStatus() {
  if (counter1 == 10)  {
    counter1 = 0;
    Serial.print("Freezer temperature: "); Serial.print(var_ds_temp1); Serial.print(" (");Serial.print(freezerTargetT);Serial.print(")");Serial.println();
    Serial.print("total uptime: "); Serial.print(freezer_totaluptime); Serial.println();
    Serial.print("turn on counter: ");Serial.print(freezer_turncounter); Serial.println();
    Serial.print("Freezer status: "); if (freezer_status) {Serial.print("ON");} else {Serial.print("OFF");} Serial.println();
  
  }
}
String SendHTML(uint8_t led1stat,uint8_t led2stat){
  String ptr = "<!DOCTYPE html> <html>\n";
  ptr +="<head><meta name=\"viewport\" content=\"width=device-width, initial-scale=1.0, user-scalable=no\">\n";
  ptr +="<title>LED Control</title>\n";
  ptr +="<style>html { font-family: Helvetica; display: inline-block; margin: 0px auto; text-align: center;}\n";
  ptr +="body{margin-top: 50px;} h1 {color: #444444;margin: 50px auto 30px;} h3 {color: #444444;margin-bottom: 50px;}\n";
  ptr +=".button {display: block;width: 80px;background-color: #3498db;border: none;color: white;padding: 13px 30px;text-decoration: none;font-size: 25px;margin: 0px auto 35px;cursor: pointer;border-radius: 4px;}\n";
  ptr +=".button-on {background-color: #3498db;}\n";
  ptr +=".button-on:active {background-color: #2980b9;}\n";
  ptr +=".button-off {background-color: #34495e;}\n";
  ptr +=".button-off:active {background-color: #2c3e50;}\n";
  ptr +="p {font-size: 14px;color: #888;margin-bottom: 10px;}\n";
  ptr +="</style>\n";
  ptr +="</head>\n";
  ptr +="<body>\n";
  ptr +="<h1>ESP32 Web Server</h1>\n";
    ptr +="<h3>Using Station(STA) Mode</h3>\n";
  
   if(led1stat)
  {ptr +="<p>LED1 Status: ON</p><a class=\"button button-off\" href=\"/cooleroff\">OFF</a>\n";}
  else
  {ptr +="<p>LED1 Status: OFF</p><a class=\"button button-on\" href=\"/cooleron\">ON</a>\n";}

  if(led2stat)
  {ptr +="<p>LED2 Status: ON</p><a class=\"button button-off\" href=\"/freezeroff\">OFF</a>\n";}
  else
  {ptr +="<p>LED2 Status: OFF</p><a class=\"button button-on\" href=\"/freezeron\">ON</a>\n";}

  ptr +="</body>\n";
  ptr +="</html>\n";
  return ptr;
}

int counter2 = 1;
void eachSecond(){
  //checkFreezer();
  //printUptime();
  if (counter2 == 5) {
    counter2 = 0;
    printtemperature();
    Serial.println();
  }
  else counter2++;

  //printFreezerStatus();
  //Serial.println();
  //simulateCompressor();
}