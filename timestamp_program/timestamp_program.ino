// Librerías necesarias
#include <WiFi.h>
#include <PubSubClient.h>
#include <LittleFS.h>
#include <ArduinoJson.h>


char ssid_buffer[32];
char pass_buffer[64];
char mqtt_buffer[32];

// Conexión WiFi y Broker MQTT
const char* SSID = ssid_buffer;
const char* PASSWORD = pass_buffer;
const char* MQTTSERVER = mqtt_buffer;

// Cliente MQTT
WiFiClient espClient;
PubSubClient client(espClient);


bool cargarCredenciales() {
  // Inicializar LittleFS
  if (!LittleFS.begin(true)) {
    Serial.println("[ERROR] No se pudo montar el sistema de archivos LittleFS");
    return false;
  }

  // Abrir el archivo config.json en modo lectura ("r")
  File configFile = LittleFS.open("/config.json", "r");
  if (!configFile) {
    Serial.println("[ERROR] No se encontró el archivo config.json");
    return false;
  }

  // Reservar memoria para parsear el JSON
  JsonDocument doc;

  // Deserializar el archivo JSON
  DeserializationError error = deserializeJson(doc, configFile);
  configFile.close(); // Cerramos el archivo inmediatamente para liberar recursos

  if (error) {
    Serial.print("[ERROR] Falló el parseo del JSON: ");
    Serial.println(error.c_str());
    return false;
  }

  // Asignar los valores del JSON a nuestras variables globales
  strlcpy(ssid_buffer, doc["ssid"] | "", sizeof(ssid_buffer));
  strlcpy(pass_buffer, doc["password"] | "", sizeof(pass_buffer));
  strlcpy(mqtt_buffer, doc["mqtt_server"] | "", sizeof(mqtt_buffer));

  Serial.println("[ÉXITO] Credenciales leídas correctamente de la memoria Flash.");
  return true;
}

// Función para conectar/reconectar a MQTT
void reconnect() {
  while (!client.connected()) {
    Serial.println(WiFi.localIP());

    Serial.print("Intentando conexión MQTT... ");
    
    // Intentamos conectar con un ID único
    if (client.connect("ESP32Client")) {
      Serial.println("[CONECTADO] Exitosamente al Broker MQTT");
    } else {
      Serial.print("[FALLÓ] Código de error: ");
      Serial.print(client.state());
      Serial.println(" -> Reintentando en 5 segundos...");
      delay(5000);
    }
  }
}

void setup() {
  Serial.begin(115200);
  delay(1000); // Pequeña pausa para que se estabilice el monitor serie
  
  Serial.println("\n--- Iniciando ESP32 ---");


  if(cargarCredenciales()){
    // Conexión WiFi
    Serial.print("Conectando a la red WiFi: ");
    Serial.println(SSID);
    
    WiFi.begin(SSID, PASSWORD);
    while (WiFi.status() != WL_CONNECTED) {
      delay(500);
      Serial.print("."); // Efecto visual de carga
    }
  }
  
  Serial.println("\n[CONECTADO] WiFi activo");
  Serial.print("Dirección IP asignada: ");
  Serial.println(WiFi.localIP());

  // Configuración del servidor MQTT
  client.setServer(MQTTSERVER, 1883);
}

void loop() {
  // Si el ESP32 se desconecta del MQTT, intenta reconectar
  if (!client.connected()) {
    reconnect();
  }
  client.loop(); // Mantiene viva la conexión y procesa mensajes entrantes


  char msg[100];
  unsigned long timestamp = millis();
  sprintf(msg, "{\"tr\": %.2lu}", timestamp);
  
  // Intentar publicar el mensaje
  Serial.print("Enviando datos al tópico 'pdytr/tr': ");
  Serial.println(msg);

  if (client.publish("pdytr/tr", msg)) {
    Serial.println("[ÉXITO] Mensaje enviado correctamente");
  } else {
    Serial.println("[ERROR] No se pudo enviar el mensaje");
  }

  Serial.println("-------------------------------------------");
  delay(1000); // Espera 1 segundos antes del próximo envío
}
