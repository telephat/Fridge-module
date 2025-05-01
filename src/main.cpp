#include <Arduino.h>
#include <DallasTemperature.h>
#include <OneWire.h>
#include <WiFi.h>               // ESP32 Core WiFi Library    
#include <WiFiUdp.h>
#include <SNMP_Agent.h>
#include <SNMPTrap.h>

#define ONE_WIRE_BUS 22

OneWire oneWire(ONE_WIRE_BUS);

// Pass our oneWire reference to Dallas Temperature.
DallasTemperature sensors(&oneWire);
DeviceAddress ds_term1, ds_term2;

const char* ssid = "bg-lsp";
const char* password = "19316632";

WiFiUDP udp;
SNMPAgent snmp = SNMPAgent("public");

int snmp_ds_temp1 = 255;
int snmp_ds_temp2 = 255;
char* str_ds_temp1;
char* str_ds_temp2;
void setupSNMP();
void setupWiFi();

void setup() {
  sensors.begin(); //initialise DS18b20 sensors
  Serial.begin(9600);
  Serial.print("Sensors detected: ");
  Serial.print(sensors.getDeviceCount(), DEC);
  if (!sensors.getAddress(ds_term1, 0)) Serial.println("Unable to find address for Device 0");
  if (!sensors.getAddress(ds_term2, 1)) Serial.println("Unable to find address for Device 0");
  Serial.println();

  str_ds_temp1 = (char*)malloc(6);
  str_ds_temp2 = (char*)malloc(6);
  setupWiFi();
  setupSNMP();
}

float get_ds_temperature(DeviceAddress deviceAddress);

void loop() {
  // int t1 = analogRead(A0);
  // int t2 = analogRead(A3);
  // Serial.print("t1: ");Serial.print(t1);Serial.print(" | t2: ");Serial.print(t2);
  // Serial.println();
  //28E01D87005949E7
  //2812CF870015278A
  sensors.requestTemperatures();
  Serial.println("DS18b20:");
  Serial.print("t1: ");
  Serial.print(get_ds_temperature(ds_term1));
  Serial.print("| t2: ");
  Serial.print(get_ds_temperature(ds_term2));
  Serial.println();
  
  snmp_ds_temp1=int(get_ds_temperature(ds_term1));
  snmp_ds_temp2=int(get_ds_temperature(ds_term2));
  snmp.loop();
  delay(100);
}

float get_ds_temperature(DeviceAddress deviceAddress) {
  float tempC = sensors.getTempC(deviceAddress);
  if (tempC == DEVICE_DISCONNECTED_C) {
    return 255;
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
