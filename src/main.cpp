
#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <WiFiClient.h>
#include <LittleFS.h>
#include <MQTT.h>
#include <EEPROM.h>
#include <ESP_OTA_GitHub.h>

#ifndef STASSID
#define STASSID "Sensores"
#define STAPSK "66083337"
#endif

// A single, global CertStore which can be used by all
// connections.  Needs to stay live the entire time any of
// the WiFiClientBearSSLs are present.
#include <CertStoreBearSSL.h>
BearSSL::CertStore certStore;

/* Set up values for your repository and binary names */
#define GHOTA_USER "Vitoko-Arriagada"
#define GHOTA_REPO "esp8266ota"
#define GHOTA_CURRENT_TAG "0.0.6"
#define GHOTA_BIN_FILE "firmware.bin"
#define GHOTA_ACCEPT_PRERELEASE 0

const char token[] = "eyJhbGciOiJIUzI1NiIsInR5cCI6IkpXVCJ9.eyJ1c2VybmFtZSI6InZpdG9rbyIsImNsaWVudGlkIjoiY2xpZW50ZSIsImlhdCI6MTY1MDIzNjQzMH0.Vd5G6j3W7RwZZjumylQlL0tyR0daCVToba-SVRK6RZQ";
const char clientid[] = "cliente";
const char username[] = "vitoko";
const char fabricante[] = "VeAm cHiLe";
const char proyecto[] = "HospitalMilitar";
const char nombre[] = "Centinela";
const char modelo[] = "A01";

String topicoBase = "";

unsigned char mac[6];

unsigned long lastMillis;
String clientMac = "";

WiFiClientSecure net;
MQTTClient mqttclient(512);

struct MyObject
{
  int EEPROM_OK;
  int FREC_ENVIO;
  int INICIOS;
};

unsigned long DELAY = 10000;
int REINICIOS = 0;

MyObject leer_parametros()
{
  MyObject customVar;
  EEPROM.get(0, customVar);
  return customVar;
}

void grabar_parametros()
{
  MyObject parametro = leer_parametros();
  DELAY = parametro.FREC_ENVIO * 1000;
  REINICIOS = parametro.INICIOS;
  if (parametro.EEPROM_OK != 2022)
  {
    parametro.EEPROM_OK = 2022;
    parametro.FREC_ENVIO = 1;
    parametro.INICIOS = 0;
    EEPROM.put(0, parametro);
    EEPROM.commit();
  }
  else
  {
    parametro.INICIOS = REINICIOS + 1;
    EEPROM.put(0, parametro);
    EEPROM.commit();
  }
}

void handle_upgade()
{

  LittleFS.begin();
  int numCerts = certStore.initCertStore(LittleFS, PSTR("/certs.idx"), PSTR("/certs.ar"));
  Serial.print(F("Number of CA certs read: "));
  Serial.println(numCerts);
  if (numCerts == 0)
  {
    Serial.println(F("No certs found. Did you run certs-from-mozill.py and upload the SPIFFS directory before running?"));
    return; // Can't connect to anything w/o certs!
  }

  // Initialise Update Code
  // We do this locally so that the memory used is freed when the function exists.
  ESPOTAGitHub ESPOTAGitHub(&certStore, GHOTA_USER, GHOTA_REPO, GHOTA_CURRENT_TAG, GHOTA_BIN_FILE, GHOTA_ACCEPT_PRERELEASE);

  Serial.println("Checking for update...");
  if (ESPOTAGitHub.checkUpgrade())
  {
    Serial.print("Upgrade found at: ");
    Serial.println(ESPOTAGitHub.getUpgradeURL());
    if (ESPOTAGitHub.doUpgrade())
    {
      Serial.println("Upgrade complete."); // This should never be seen as the device should restart on successful upgrade.
    }
    else
    {
      Serial.print("Unable to upgrade: ");
      Serial.println(ESPOTAGitHub.getLastError());
    }
  }
  else
  {
    Serial.print("Not proceeding to upgrade: ");
    Serial.println(ESPOTAGitHub.getLastError());
  }
}

void enviarInfo()
{
  DynamicJsonDocument doc(1024);

  JsonObject doc_0 = doc.createNestedObject("Info");
  doc_0["IP"] = WiFi.localIP();
  doc_0["SSID"] = WiFi.SSID();
  doc_0["Version Firmware"] = GHOTA_CURRENT_TAG;
  doc_0["Fabricante"] = fabricante;
  doc_0["Nombre Dispositivo"] = nombre;
  doc_0["Modelo Dispositivo"] = modelo;
  doc_0["Config"]["Periodo de envio"] = 10;
  doc_0["Config"]["Periodo de puerta abierta"] = 20;
  doc_0["Config"]["Modo"] = 30;
  String output;

  Serial.println(serializeJson(doc, output));
  Serial.println("\nconnected!");
  Serial.println("\nSuscrito: " + topicoBase + "/comandos");
  mqttclient.publish(topicoBase + "/info", output);
}

void connect()
{
  Serial.print("checking wifi...");
  while (WiFi.status() != WL_CONNECTED)
  {
    Serial.print(".");
    delay(1000);
  }

  // do not verify tls certificate
  // check the following example for methods to verify the server:
  // https://github.com/esp8266/Arduino/blob/master/libraries/ESP8266WiFi/examples/BearSSL_Validation/BearSSL_Validation.ino
  net.setInsecure();

  Serial.print("\nconnecting...");
  while (!mqttclient.connect(clientid, username, token))
  {
    Serial.print(".");
    delay(1000);
  }

  enviarInfo();
  mqttclient.subscribe(topicoBase + "/comandos");
}

void buscar_nuevo_firmware()
{
  mqttclient.disconnect();
  /* This is the actual code to check and upgrade */
  handle_upgade();
  /* End of check and upgrade code */
}

void messageReceived(String &topic, String &payload)
{
  if (topic == (topicoBase + "/comandos"))
  {
    Serial.println("incoming: " + topic + " - " + payload);
    DynamicJsonDocument doc(2048);
    DeserializationError err = deserializeJson(doc, payload);
    if (!err)
    {
      Serial.println("          SIN ERROR JSON");
      const char *comando = doc["comando"];
      MyObject parametro = leer_parametros();
      if (String(comando) == "SetSendFrecuency")
      {
        int valor = doc["valor"];
        Serial.println("          COMANDO VALIDO");
        parametro.FREC_ENVIO = valor;
        DELAY = parametro.FREC_ENVIO * 1000;
        Serial.println(DELAY);
        EEPROM.put(0, parametro);
        EEPROM.commit();
      }
      else if (String(comando) == "Reset")
      {
        bool valor = doc["valor"];
        if (valor)
        {
          ESP.restart();
        }
      }
    }
    else
    {
      if (payload != GHOTA_CURRENT_TAG)
      {
        Serial.println("--------------------------------------");
        buscar_nuevo_firmware();
      }
      else
      {
        Serial.println("No actualizar...");
      }
    }
  }
}

String macToStr(const uint8_t *mac)
{
  String result;
  for (int i = 0; i < 6; ++i)
  {
    result += String(mac[i], 16);
    if (i < 5)
      result += ':';
  }
  return result;
}

void setup()
{
  EEPROM.begin(512); // Initialize EEPROM
  Serial.begin(115200);
  delay(3000);
  grabar_parametros();
  Serial.println(REINICIOS);
  Serial.println("\n\nFirmware Version " + String(GHOTA_CURRENT_TAG));
  // Start serial for debugging (not used by library, just this sketch).
  pinMode(2, OUTPUT);
  pinMode(0, INPUT);

  // Connect to WiFi
  Serial.print("Connecting to WiFi... ");
  WiFi.mode(WIFI_STA);
  WiFi.begin(STASSID, STAPSK);
  if ((WiFi.status() != WL_CONNECTED))
  {
    Serial.print("... ");
  }
  Serial.println();
  WiFi.macAddress(mac);
  clientMac += macToStr(mac);
  Serial.println("MAC: " + clientMac);
  topicoBase = String(fabricante) + "/" + String(proyecto) + "/ID" + String(ESP.getChipId(), 10);
  /* This is the actual code to check and upgrade */
  // handle_upgade();
  mqttclient.begin("iot.veamchile.cl", 8883, net);
  mqttclient.onMessage(messageReceived);

  connect();
}
String texto = "";

void loop()
{
  mqttclient.loop();
  delay(10);

  if (!mqttclient.connected())
  {
    connect();
  }

  if (digitalRead(0) == 0)
  {
    ESP.restart();
    Serial.println("Buscando Actualizacion...");
    buscar_nuevo_firmware();
    handle_upgade();
  }

  if (millis() - lastMillis > DELAY)
  {
    lastMillis = millis();

    DynamicJsonDocument doc(1024);
    JsonObject doc_0 = doc.createNestedObject("data");
    doc_0["Temp"] = float(random(1, 10000)) / 100;
    doc_0["Hum"] = float(random(1, 10000)) / 100;
    doc_0["Volt Batt"] = float(random(0, 500)) / 100;
    doc_0["Cargando"] = random(0, 2);
    doc_0["Puerta"] = random(0, 2);
    doc_0["Boton"] = random(0, 2);
    doc_0["Memoria RAM"] = ESP.getFreeHeap();
    doc_0["Motivo Reinicio"] = ESP.getResetReason();
    doc_0["Reinicios"] = REINICIOS;
    doc_0["RSSI"] = WiFi.RSSI();

    String output;
    Serial.println(serializeJson(doc, output));
    mqttclient.publish(topicoBase + "/data", output);
    texto = texto + "123456789012345678901234567890123456789012345678901234567890";
  }
}
