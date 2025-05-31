#include <OneWire.h>
#include <DallasTemperature.h>
#include <time.h>
#include <WiFi.h>
//#include <WiFiClient.h>
#include <WebServer.h>
#include <WiFiUdp.h>
#include <SNMP_Agent.h>
#include <SNMPTrap.h>

const char* version = "0.1.1";

// Data wire is plugged into port 2 on the Arduino
#define ONE_WIRE_BUS 22
#define COOLER_PIN 12
#define FREEZER_PIN 13

const char* ssid = "bg-lsp";
const char* password = "19316632";
int GMTOffset = 3;

WiFiUDP udp;
// ***SNMP VARS
SNMPAgent snmp = SNMPAgent("public", "private");
// If we want to change the functionaality of an OID callback later, store them here.
ValueCallback* changingNumberOID;
ValueCallback* settableNumberOID;
ValueCallback* changingStringOID;
// TimestampCallback* timestampCallbackOID;
SNMPTrap* settableNumberTrap = new SNMPTrap("public", SNMP_VERSION_2C);

std::string deviceName = "Fridge module";
std::string firmwareVersion = "firmware 1.06.2025 00:46";
std::string freezerBanner = "Freezer";
std::string coolerBanner = "Cooler";
std::string globalBanner = "Fridge status";

WebServer server(80);

// Setup a oneWire instance to communicate with any OneWire devices (not just Maxim/Dallas temperature ICs)
OneWire oneWire(ONE_WIRE_BUS);

// Pass our oneWire reference to Dallas Temperature.
DallasTemperature sensors(&oneWire);

int count = 0;
DeviceAddress term1, term2, term3;
uint64_t uptime_seconds = 0;
uint64_t f_uptime = 0;
uint64_t c_uptime = 0;
int64_t uptime_minutes = 0;
int64_t s_basetimer = 0;
int64_t m_basetimer = 0;
int c_turnson = 0;
int f_turnson = 0;

int freezer_temperature = -255;
int freezer_target = -18;
int freezer_tolerance = -3;
boolean freezer_status = false;
int f_intstatus = 0;
int cooler_temperature = -255;
int cooler_target = 5;
boolean cooler_status = false;
int c_intstatus = 0;
int control_temperature = -255;
int control_minimum = -3; //вариаторы для выбора оптимальной температуры в камере
int control_maximum = 10; // контролька решает всё
int f_safeguard = 130; //предохранитель от включения-выклюячения-включения компрессоров
int f_cd = 5;
int c_safeguard = 125; //на 5 секунд раньше, чтобы 2 компрессора не включались одновременно
int c_cd = 15;

void eachSecond();
void printStatus();
void checkFreezer();
void checkCooler();
void coolerOn();
void coolerOff();
void freezerOn();
void freezerOff();
void WiFiSetup();
void WebServerSetup();
String WebStatus(u64_t s_uptime, boolean f_status, boolean c_status, int f_temp, int c_temp, int cont_temp);
String WebPage();
String WebPage2();
String serverurl;
void WebOnConnect();
void WebData();
void eachMinute();
void SNMPSetup();
std::string getTimestamp(int64_t uptime);
int testing = 1;

void setup(void)
{
  // start serial port
  pinMode(COOLER_PIN, OUTPUT);
  pinMode(FREEZER_PIN, OUTPUT);
  Serial.begin(9600);
  sensors.begin();
  WiFiSetup();
  WebServerSetup();
  snmp.setUDP(&udp);
  snmp.begin();
  SNMPSetup();
  delay(1000);
  Serial.print("Sensors detected: ");
  Serial.print(sensors.getDeviceCount(), DEC);
  if (!sensors.getAddress(term1, 0)) Serial.println("Unable to find addres for 'term1");
  if (!sensors.getAddress(term2, 0)) Serial.println("Unable to find addres for 'term2");
  if (!sensors.getAddress(term3, 0)) Serial.println("Unable to find addres for 'term3");
  Serial.println();
  int64_t upTimeUS = esp_timer_get_time();
  s_basetimer = upTimeUS / 1000000;
  m_basetimer = s_basetimer / 60;
  serverurl = WiFi.localIP().toString();
  delay(5000);
}

void loop()
{
  int64_t upTimeUS = esp_timer_get_time(); // in microseconds
  uptime_seconds = upTimeUS / 1000000;
  uptime_minutes = uptime_seconds / 60;
  if ((uptime_seconds - s_basetimer) >= 1) {
    s_basetimer = uptime_seconds;
    eachSecond();
  }
  if ((uptime_minutes - m_basetimer) >=1) {
    m_basetimer = uptime_minutes;
    eachMinute();
  }
  
  server.handleClient();
  snmp.loop();
}

std::string getTimestamp(int64_t uptime) {
  //uptime = 141231;
  int days = (uptime / 86400);
  int hours = (uptime % 86400) / 3600;
  int minutes = ((uptime % 86400) % 3600) / 60;
  int seconds = ((uptime % 86400) % 3600) % 60;
  String timestamp = String(days)+"d "+ String(hours)+"h "+String(minutes)+"m "+String(seconds)+"s";
  std::string toreturn(timestamp.c_str(), timestamp.length());
  return toreturn;
}

void SNMPSetup() {
  //settableNumberOID = snmp.addIntegerHandler(".1.3.6.1.4.1.4.0", &cooler_target, true);
  //settableNumberOID = snmp.addIntegerHandler(".1.3.6.1.4.1.4.1", &freezer_target, true);

         snmp.addReadOnlyStaticStringHandler(".1.3.6.1.2.1.1.3.0", deviceName);
                    snmp.addCounter64Handler(".1.3.6.1.2.1.1.3.1", &uptime_seconds);
         snmp.addReadOnlyStaticStringHandler(".1.3.6.1.2.1.1.3.2", firmwareVersion);
                    snmp.addCounter64Handler(".1.3.6.1.2.1.1.4.0", &c_uptime);
                      snmp.addIntegerHandler(".1.3.6.1.2.1.1.4.1", &c_turnson);
                      snmp.addIntegerHandler(".1.3.6.1.2.1.1.4.2", &c_intstatus);
                      snmp.addIntegerHandler(".1.3.6.1.2.1.1.4.3", &cooler_temperature);
  settableNumberOID = snmp.addIntegerHandler(".1.3.6.1.2.1.1.4.4", &cooler_target, true);
                      snmp.addIntegerHandler(".1.3.6.1.2.1.1.5.0", &control_temperature);
                    snmp.addCounter64Handler(".1.3.6.1.2.1.1.6.0", &f_uptime);
                      snmp.addIntegerHandler(".1.3.6.1.2.1.1.6.1", &f_turnson);
                      snmp.addIntegerHandler(".1.3.6.1.2.1.1.6.2", &f_intstatus);
                      snmp.addIntegerHandler(".1.3.6.1.2.1.1.6.3", &freezer_temperature);
  settableNumberOID = snmp.addIntegerHandler(".1.3.6.1.2.1.1.6.4", &freezer_target, true);
  
  
//changingStringOID = snmp.addDynamicReadOnlyStringHandler(".1.3.6.1.2.1.1.3.0", deviceUptime);
//  snmp.addDynamicReadOnlyStringHandler(".1.3.6.1.2.1.1.3.0", deviceName);
// std::string deviceName = "Fridge module";
// std::string firmwareVersion = "firmware 30.05.2025 22:48";
// std::string freezerBanner = "Freezer";
// std::string coolerBanner = "Cooler";
// std::string globalBanner = "Fridge status";

  
  snmp.sortHandlers();
}

void WebServerSetup () {
  server.on("/", WebOnConnect);
  server.on("/data", WebData);
  server.begin();
  Serial.println("Webserver started.");
}

void WebOnConnect() {
  //server.send(200, "text/html", WebStatus(uptime_seconds, freezer_status, cooler_status, freezer_temperature, cooler_temperature, control_temperature));
  server.send(200, "text/html", WebPage2());
  //server.send(200, "text/plain", "hello world");
}
void WebData() {
  server.send(200, "text/plain", WebStatus(uptime_seconds, freezer_status, cooler_status, freezer_temperature, cooler_temperature, control_temperature));
}

String WebStatus(u64_t s_uptime, boolean f_status, boolean c_status, int f_temp, int c_temp, int cont_temp) {
  String message = String(s_uptime);
  message += "   cooler: ";
  message += String(c_temp);
  message += "°C (";
  if (c_status) message += "ON";
  else  message += "OFF";
  if (c_cd > 0) {message +="[";message +=String(c_cd);message +="]";}
  message += ") ";
  message += "control: ";
  message += String(cont_temp);
  message += "°C ";
  message += "freezer: ";
  message += String(f_temp);
  message += "°C (";
  if (f_status) message += "ON";
  else message += "OFF";
  if (f_cd > 0) {
    message +="[";message +=String(f_cd);message +="]";}
  message += ") ";
    return message;
}

String WebPage() {
  String message = "<!DOCTYPE html>";
  message += "<html> <head> <title> Fridge module </title> <style>";
  message += "#dataContainer {font-family: monospace; white-space: pre-wrap;}";
  message += "</style></head>\n";
  message += "<body><div id='dataContainer'></div>\n";
  message += "<script>\n";
  message += "function fetchData() {\n";
  message += "fetch('http://";
  message += serverurl;
  message += "/data')\n";
  message += ".then(response => {\n";
  message += "if (!response.ok) {\n";
  message += "throw new Error(`HTTP error! status: ${response.status}`);\n";
  message += "}\n";
  message += "return response.text();\n";
  message += "})\n";
  message += ".then(data => {\n";
  message += "const dataContainer = document.getElementById('dataContainer');\n";
  message += "dataContainer.innerHTML += data + '\\n';\n"; // Добавляем новую строку
  message += "})\n";
  message += ".catch(error => {\n";
  message += "console.error('Error reading data:', error);\n";
  message += "const dataContainer = document.getElementById('dataContainer');\n";
  message += "dataContainer.innerHTML += 'Error: ' + error + '\\n';\n";
  message += "});}\n";
  message += "setInterval(fetchData, 1000);\n";
  message += "</script></body></html>";
  return message;
}

String WebPage2() {
  String message;
  message = "<!DOCTYPE html>\n";
  message +="<html> <head> <title> Fridge status </title><style>";
  message +="#dataContainer {font-family: monospace; white-space: pre-wrap; height: 300px; overflow-y: scroll; border: 1px solid #ccc; padding: 5px; }\n";
  message +="</style> </head>\n";
  message +="<body>\n";
  message +="<div id='dataContainer'></div>\n";
  message +="<script>\n";
  message +="function fetchData() {\n";
  message +="fetch('http://";
  message += serverurl;
  message += "/data')\n";
  message +=".then(response => {\n";
  message +="if (!response.ok) {\n";
  message +="throw new Error(`HTTP error! status: ${response.status}`);\n";
  message +="}\n";
  message +="return response.text();\n";
  message +="})\n";
  message +=".then(data => {\n";
  message +="const dataContainer = document.getElementById('dataContainer');\n";
  message +="dataContainer.innerHTML += data + '\\n';\n";
  message +="dataContainer.scrollTop = dataContainer.scrollHeight;\n";
  message +="})\n";
  message +=".catch(error => {\n";
  message +="console.error('Ошибка при чтении данных:', error);\n";
  message +="const dataContainer = document.getElementById('dataContainer');\n";
  message +="dataContainer.innerHTML += 'Ошибка: ' + error + '\\n';\n";
  message +="dataContainer.scrollTop = dataContainer.scrollHeight;\n";
  message +="});\n";
  message +="}\n";
  message +="// Запускаем функцию fetchData каждую секунду\n";
  message +="setInterval(fetchData, 1000);\n";
  message +="</script>\n";
  message +="</body>\n";
  message +="</html>\n";
  return message;
}

void WiFiSetup() {
  WiFi.begin(ssid, password);
  while (WiFi.status()!= WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("WiFi connected to bg-lsp");
  Serial.print("IP address: ");Serial.println(WiFi.localIP());
}

void printStatus() {
  sensors.requestTemperatures(); // Send the command to get temperatures
  Serial.print(uptime_seconds);
  Serial.print("  ");
  Serial.print("cooler: ");
  cooler_temperature = sensors.getTempCByIndex(0);
  Serial.print(cooler_temperature);
  Serial.print("°C (");
  if (cooler_status) Serial.print("ON)");
  else Serial.print("OFF)");
  Serial.print(" control: ");
  control_temperature = sensors.getTempCByIndex(1);
  Serial.print(control_temperature);
  Serial.print(" freezer: ");
  freezer_temperature = sensors.getTempCByIndex(2);
  Serial.print(freezer_temperature);
  Serial.print("°C (");
  if (freezer_status) Serial.print("ON)");
  else Serial.print("OFF)");
  Serial.println();
}

void freezerOn() {
  if (f_cd <= 0) { // safeguard from turn-on-off-on-off when temperature is on the edge
    digitalWrite(FREEZER_PIN, HIGH);
    freezer_status = true;
    f_turnson++;
  }
  else {
    f_cd--;
  }
  
}
void freezerOff() {
  digitalWrite(FREEZER_PIN, LOW);
  freezer_status = false;
  f_cd = f_safeguard;
}
void coolerOn() {
  if (c_cd <= 0) {
    digitalWrite(COOLER_PIN, HIGH);
    cooler_status = true;
    c_turnson++;
  }
  else {
    c_cd--;
  }
}
void coolerOff() {
  digitalWrite(COOLER_PIN, LOW);
  cooler_status = false;
  c_cd = c_safeguard;
}

void checkFreezer() {
  int f_temp = int(freezer_temperature);
  if (f_temp > freezer_target) {
    freezerOn();
  }
  
  else if (f_temp <= freezer_target - freezer_tolerance)
  {
    freezerOff();
  }
  
}

void checkCooler() {
  int c_temp = int(cooler_temperature);
  int cnt_temp = int(control_temperature);
  if (cooler_temperature > cooler_target) {
    coolerOn();
  }
  else if (cooler_temperature < cooler_target)
  {
    coolerOff();
  }
}

void eachSecond() {
  //printStatus();
  //checkFreezer();
  //checkCooler();
  if (cooler_status) {c_uptime++;c_intstatus =1;} else {c_intstatus = 0;}
  if (freezer_status) {f_uptime++;f_intstatus =1;} else {f_intstatus = 0;}
}

void eachMinute() {
}
