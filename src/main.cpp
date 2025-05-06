#include <OneWire.h>
#include <DallasTemperature.h>
#include <time.h>
#include <WiFi.h>
#include <WebServer.h>

// Data wire is plugged into port 2 on the Arduino
#define ONE_WIRE_BUS 22
#define COOLER_PIN 12
#define FREEZER_PIN 13

const char* ssid = "bg-lsp";
const char* password = "19316632";

WebServer server(80);

// Setup a oneWire instance to communicate with any OneWire devices (not just Maxim/Dallas temperature ICs)
OneWire oneWire(ONE_WIRE_BUS);

// Pass our oneWire reference to Dallas Temperature.
DallasTemperature sensors(&oneWire);

int count = 0;
DeviceAddress term1, term2, term3;
int64_t uptime_seconds = 0;
int64_t basetimer = 0;

int freezer_temperature = -255;
int freezer_target = -10;
int freezer_tolerance = -3;
boolean freezer_status = false;
int cooler_temperature = -255;
int cooler_target = 9;
boolean cooler_status = false;
int control_temperature = -255;
int control_minimum = 5; //вариаторы для выбора оптимальной температуры в камере
int control_maximum = 9; // контролька решает всё
int f_safeguard = 30; //предохранитель от включения-выклюячения-включения компрессоров
int f_cd = 5;
int c_safeguard = 30;
int c_cd = 5;

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
void WebOnConnect();
void WebData();

void setup(void)
{
  // start serial port
  pinMode(COOLER_PIN, OUTPUT);
  pinMode(FREEZER_PIN, OUTPUT);
  Serial.begin(9600);
  sensors.begin();
  WiFiSetup();
  WebServerSetup();
  delay(1000);
  Serial.print("Sensors detected: ");
  Serial.print(sensors.getDeviceCount(), DEC);
  if (!sensors.getAddress(term1, 0)) Serial.println("Unable to find addres for 'term1");
  if (!sensors.getAddress(term2, 0)) Serial.println("Unable to find addres for 'term2");
  if (!sensors.getAddress(term3, 0)) Serial.println("Unable to find addres for 'term3");
  Serial.println();
  int64_t upTimeUS = esp_timer_get_time();
  basetimer = upTimeUS / 1000000;
  delay(5000);
}


void loop()
{
  int64_t upTimeUS = esp_timer_get_time(); // in microseconds
  uptime_seconds = upTimeUS / 1000000;
  if ((uptime_seconds - basetimer) >= 1) {
    basetimer = uptime_seconds;
    eachSecond();
  }
  server.handleClient();
}

void WebServerSetup () {
  server.on("/", WebOnConnect);
  server.on("/data", WebData);
  server.begin();
  Serial.println("Webserver started.");
}

void WebOnConnect() {
  //server.send(200, "text/html", WebStatus(uptime_seconds, freezer_status, cooler_status, freezer_temperature, cooler_temperature, control_temperature));
  server.send(200, "text/html", WebPage());
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
  if (c_status) message += "ON)";
  else  message += "OFF) ";
  message += "control: ";
  message += String(cont_temp);
  message += "°C ";
  message += "freezer: ";
  message += String(f_temp);
  message += "°C (";
  if (f_status) message += "ON)";
  else message += "OFF)";
  return message;
}

String WebPage() {
  String message = "<!DOCTYPE html>";
  //message += WebStatus(uptime_seconds, freezer_status, cooler_status, freezer_temperature, cooler_temperature, control_temperature);
  message += "<html> <head> <title> Fridge module </title> <style>";
  message += "#dataContainer {font-family: monospace; white-space: pre-wrap;}";
  message += "</style></head>\n";
  message += "<body><div id='dataContainer'></div>\n";
  message += "<script>\n";
  message += "function fetchData() {\n";
  message += "fetch('http://192.168.31.53/data')\n";
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
  if (cnt_temp > control_maximum) {
    coolerOn();
  }
  else if (cnt_temp <= control_minimum)
  {
    coolerOff();
  }
}

void eachSecond() {
  printStatus();
  checkFreezer();
  checkCooler();
}
