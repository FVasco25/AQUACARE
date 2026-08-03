#include <SPI.h>
#include <LoRa.h>
#include <WiFi.h>
#include <Firebase_ESP_Client.h>
#include <addons/TokenHelper.h>
#include <addons/RTDBHelper.h>
#include <NTPClient.h>
#include <WiFiUdp.h>
#include <TimeLib.h>

#define WIFI_SSID       "iPhone de Horacio"
#define WIFI_PASSWORD   "lachito29"
#define API_KEY         "AIzaSyAjISAiiyBsZuVRszB5sPRLVN1Foz7T9Vo"
#define DATABASE_URL    "https://aquacare-iot-default-rtdb.firebaseio.com"

#define ss 5 
#define rst 14
#define dio0 2
#define sck 18
#define miso 19
#define mosi 23

#define PIN_ESTADO 22

String estadoAnterior = "";
String ultimoEstado  = "";

// Buffer para datos del minuto actual
String bufferedData = "";
int bufferedMinute = -1;

WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP, "europe.pool.ntp.org", -21600, 60000); // GMT-6
FirebaseData fbdo;
FirebaseAuth auth;
FirebaseConfig config;

void setup() {
  Serial.begin(9600);
  pinMode(PIN_ESTADO, INPUT);

  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  while (WiFi.status() != WL_CONNECTED) {
    delay(300); Serial.print(".");
  }
  Serial.println("\nWiFi conectado");

  config.api_key = API_KEY;
  config.database_url = DATABASE_URL;
  auth.user.email = "aquacare.ite@gmail.com";
  auth.user.password = "b5VrLp#$";
  config.token_status_callback = tokenStatusCallback;
  Firebase.begin(&config, &auth);
  Firebase.reconnectNetwork(true);

  LoRa.setPins(ss, rst, dio0);
  while (!LoRa.begin(433E6)) {
    delay(500); Serial.print(".");
  }
  LoRa.setTxPower(20);
  LoRa.setSpreadingFactor(12);
  LoRa.setSignalBandwidth(62.5E3);
  LoRa.setCodingRate4(8);
  LoRa.setSyncWord(0xC8);
  Serial.println("\nLoRa listo");

  timeClient.begin();
}

void loop() {
  // 1) Actualizar fecha y hora vía NTP
  timeClient.update();
  unsigned long epochTime = timeClient.getEpochTime();
  setTime(epochTime);

  // 2) Leer y subir estado local (Receptor) si cambió
  int valorPin = digitalRead(PIN_ESTADO);
  String estadoReceptor = (valorPin == HIGH) ? "Principal" : "Backup";
  if (estadoReceptor != estadoAnterior) {
    if (Firebase.RTDB.setString(&fbdo, "/estado/Receptor", estadoReceptor)) {
      Serial.println("Receptor → " + estadoReceptor);
      estadoAnterior = estadoReceptor;
    } else {
      Serial.println("Error Receptor: " + fbdo.errorReason());
    }
  }

  // --- Logica para subir datos SOLO al cambiar de minuto ---
  int currentMinute = minute();
  
  // 3) Leer paquete LoRa
  int packetSize = LoRa.parsePacket();
  if (packetSize) {
    String data = "";
    while (LoRa.available()) {
      data += (char)LoRa.read();
    }
    data.trim();
    Serial.println("LoRa: " + data);

    // 3a) Si viene con coma, asumimos datos de sensor (pH, NTU, OD)
    if (data.indexOf(',') > 0) {
      bufferedData = data; // Guardar el último dato recibido para este minuto
      bufferedMinute = currentMinute; // Guardar el minuto correspondiente

    } else {
      // 3b) Si no, asumimos cambio de estado del Emisor
      if (data != ultimoEstado) {
        ultimoEstado = data;
        if (Firebase.RTDB.setString(&fbdo, "/estado/Emisor", data)) {
          Serial.println("Emisor → " + data);
        } else {
          Serial.println("Error Emisor: " + fbdo.errorReason());
        }
      }
    }
  }

    // 4) Si el minuto cambió, subir el dato guardado y limpiar buffer
  static int lastMinute = -1;
  if (currentMinute != lastMinute) {
    if (bufferedData.length() > 0 && bufferedMinute == lastMinute) {
      // Procesar y subir a Firebase
      int commaIndex1 = bufferedData.indexOf(',');
      int commaIndex2 = bufferedData.indexOf(',', commaIndex1 + 1);
      String ph = bufferedData.substring(0, commaIndex1);
      String ntu = bufferedData.substring(commaIndex1 + 1, commaIndex2);
      String od = bufferedData.substring(commaIndex2 + 1);

      String fecha = String(day()) + "/" + String(month()) + "/" + String(year());
      // Cambia aquí:
      String hora  = String(hour()) + ":" + String(currentMinute) + ":00"; // <-- Usar currentMinute

      FirebaseJson json;
      json.add("fecha", fecha);
      json.add("hora", hora);
      json.add("pH", ph.toFloat());
      json.add("NTU", ntu.toFloat());
      json.add("oD", od.toFloat());
      if (Firebase.RTDB.pushJSON(&fbdo, "/datos", &json)) {
        Serial.println("Sensor (cada minuto) → OK");
      } else {
        Serial.println("Error Sensor: " + fbdo.errorReason());
      }
      bufferedData = ""; // Limpiar buffer para el nuevo minuto
    }
    lastMinute = currentMinute;
  }
}
