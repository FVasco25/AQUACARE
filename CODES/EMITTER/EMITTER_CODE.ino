// Example for emissor
// Written by AquaCare IoT
// Last Update 14.05.2025
// More info: https://aquacare-iot.web.app/index.html

// REQUIRES the following Arduino libraries:
// - LoRa (by Sandeep Mistry): https://github.com/sandeepmistry/arduino-LoRa


#include <Arduino.h>
#include <SPI.h>
#include <LoRa.h>

// Pines LoRa
#define ss   10
#define rst  9
#define dio0 2
#define sck  13
#define miso 12
#define mosi 11

// Pin de estado digital
#define PIN_ESTADO 3

// Pines y calibración pH
const int analogInPin = A1;
int bufPH[10];
unsigned long inValue;
const float pH1 = 4.0;
const float pH2 = 7.0;
const float V1  = 1200.0;         // mV en pH1
const float V2  = 2100.0;         // mV en pH2

// Pin turbidez
const int sensorTurbPin = A0;

// Pines y calibración DO
#define DO_PIN A2
#define VREF 4200    // VREF en mV
#define ADC_RES 1024 // Resolución ADC

// Modo calibración: 0 = single-point, 1 = two-point
#define TWO_POINT_CALIBRATION 1

#define READ_TEMP 25 // Temperatura actual en °C (o función sensor)

// Valores de calibración
#define CAL1_V 1600 // mV en CAL1
#define CAL1_T 25   // °C en CAL1
#define CAL2_V 1300 // mV en CAL2
#define CAL2_T 15   // °C en CAL2

const uint16_t DO_Table[41] = {
  14460,14220,13820,13440,13090,12740,12420,12110,11810,11530,
  11260,11010,10770,10530,10300,10080,9860,9660,9460,9270,
  9080,8900,8730,8570,8410,8250,8110,7960,7820,7690,
  7560,7430,7300,7180,7070,6950,6840,6730,6630,6530,6410
};

// Prototipos
float readPH();
float readTurbidity();
int16_t readDO(uint32_t voltage_mv, uint8_t temperature_c);
void sendData(float PH, float ntu, float do_mgL);
float round_to_dp(float in_value, int decimal_place);

void setup() {
  pinMode(PIN_ESTADO, INPUT);
  Serial.begin(9600);
  while (!Serial);

  // Inicialización LoRa
  LoRa.setPins(ss, rst, dio0);
  while (!LoRa.begin(433E6)) {
    Serial.print(".");
    delay(500);
  }
  // Parámetros avanzados
  LoRa.setTxPower(20);
  LoRa.setSpreadingFactor(12);
  LoRa.setSignalBandwidth(125E3);
  LoRa.setCodingRate4(8);
  LoRa.setSyncWord(0xC8);
  LoRa.enableCrc(); 
  Serial.println("\nLoRa listo");

}

void loop() {
  // Leer estado digital
  int estado = digitalRead(PIN_ESTADO);
  String estadoEmisor = (estado == HIGH) ? "Principal" : "Backup";
  LoRa.beginPacket();
  LoRa.print(estadoEmisor);
  LoRa.endPacket();
  Serial.print("Enviado estado: ");
  Serial.println(estadoEmisor);

  // Leer sensores
  float PH = readPH();
  float ntu = readTurbidity();
    // Leer DO
  uint8_t temp = (uint8_t)READ_TEMP;
  uint16_t raw = analogRead(DO_PIN);
  uint32_t voltage = (uint32_t)VREF * raw / ADC_RES;
  int16_t do_raw = readDO(voltage, temp);
  float DO_mgL = do_raw / 1000.0;

  Serial.print("DO (mg/L): ");
  Serial.println(DO_mgL, 2);

  // Enviar datos
  sendData(PH, ntu, DO_mgL);

  delay(1000);
}

float readPH() {
  for (int i = 0; i < 10; i++) {
    bufPH[i] = analogRead(analogInPin);
    delay(10);
  }
  // Ordenar
  for (int i = 0; i < 9; i++) {
    for (int j = i + 1; j < 10; j++) {
      if (bufPH[i] > bufPH[j]) {
        int tmp = bufPH[i];
        bufPH[i] = bufPH[j];
        bufPH[j] = tmp;
      }
    }
  }
  // Promediar sin extremos
  inValue = 0;
  for (int i = 2; i < 8; i++) inValue += bufPH[i];

  // ADC → mV
  float voltage = inValue * 5.0 / 1024.0 / 6.0 * 1000.0;
  Serial.print("Voltaje pH (mV): ");
  Serial.println(voltage);

  // Calibración lineal
  float m = (pH2 - pH1) / (V2 - V1);
  float b = pH1 - m * V1;
  float phValue = m * voltage + b;
  Serial.print("pH = ");
  Serial.println(phValue, 2);
  delay(20);
  return phValue;
}

float readTurbidity() {
  float sum = 0;
  for (int i = 0; i < 800; i++) {
    sum += (analogRead(sensorTurbPin) / 1023.0) * 4.2;
  }
  float volt = round_to_dp(sum / 800.0, 2);
  float ntu;
  if (volt < 2.1) {
    ntu = 3000.0;
  } else {
    float a = -1120.4 * volt * volt + 5742.3 * volt - 4353.8;
    ntu = (a /1000);
  }
  Serial.print("Turbidez: ");
  Serial.print(volt);
  Serial.print(" V, ");
  Serial.print(ntu);
  Serial.println(" NTU");
  delay(10);
  return ntu;
}

int16_t readDO(uint32_t voltage_mv, uint8_t temperature_c) {
#if TWO_POINT_CALIBRATION == 0
  uint16_t Vsat = CAL1_V + 35 * temperature_c - CAL1_T * 35;
#else
  int16_t Vsat = (int16_t)((int8_t)temperature_c - CAL2_T) * (CAL1_V - CAL2_V) / (CAL1_T - CAL2_T) + CAL2_V;
#endif
  uint16_t table_val = DO_Table[temperature_c];
  return (voltage_mv * table_val) / Vsat;
}

void sendData(float PH, float ntu, float do_mgL) {
  if (isnan(PH) || isnan(ntu) || isnan(do_mgL)) {
    Serial.println("¡Lectura fallida de sensores!");
    return;
  }
  String data = String(PH, 2) + "," + String(ntu, 2) + "," + String(do_mgL, 2);
  LoRa.beginPacket();
  LoRa.print(data);
  LoRa.endPacket();
  Serial.print("Sending packet: ");
  Serial.println(data);
}

float round_to_dp(float in_value, int decimal_place) {
  float mult = pow(10.0, decimal_place);
  return round(in_value * mult) / mult;
}