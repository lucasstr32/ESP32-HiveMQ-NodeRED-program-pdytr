// Librerías necesarias
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <PubSubClient.h>
#include <LittleFS.h>
#include <ArduinoJson.h>


char wifissid_buffer[32];
char wifipass_buffer[64];
char mqttsv_buffer[64];
char mqttusername_buffer[32];
char mqttpassword_buffer[32];

// Conexión WiFi y Broker MQTT
const char* WIFISSID = wifissid_buffer;
const char* WIFIPASSWORD = wifipass_buffer;
const char* MQTTSERVER = mqttsv_buffer;
const char* MQTTUSERNAME = mqttusername_buffer;
const char* MQTTPASSWORD = mqttpassword_buffer;
const int MQTTPORT = 8883;
const char* topic_publish = "pdytr/tr";

String ca_cert_content;

// Cliente MQTT
WiFiClientSecure wifiClient;
PubSubClient mqttClient(wifiClient);

long previous_time=0;


void sincronizarHora() {
  Serial.print("Sincronizando hora mediante NTP... ");
  // Configura la hora UTC. Usamos servidores públicos de pools NTP
  configTime(0, 0, "pool.ntp.org", "time.nist.gov");

  // Esperamos a que el sistema operativo del ESP32 reciba la hora real
  time_t now = time(NULL);
  while (now < 8 * 3600 * 2) { // Espera hasta que el año sea mayor a 1970
    delay(500);
    Serial.print(".");
    now = time(NULL);
  }
  Serial.println("\n[ÉXITO] Hora sincronizada correctamente.");
  
  // Imprime la hora actual en el monitor serie para verificar
  struct tm timeinfo;
  if (getLocalTime(&timeinfo)) {
    Serial.print("Fecha/Hora actual del sistema: ");
    Serial.println(asctime(&timeinfo));
  }
}

bool cargarCertificadoCA(){
  File caFile = LittleFS.open("/hivemq_ca.pem", "r");
  if(!caFile){
    Serial.println("[ERROR] No se encontró el archivo hivemq_ca.pem en LittleFS");
    return false;
  }
  ca_cert_content = caFile.readString();
  caFile.close();

  Serial.println("[ÉXITO] Certificado CA leído en memoria.");  return true;
}

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
  strlcpy(wifissid_buffer, doc["ssid"] | "", sizeof(wifissid_buffer));
  strlcpy(wifipass_buffer, doc["password"] | "", sizeof(wifipass_buffer));
  strlcpy(mqttsv_buffer, doc["mqtt_server"] | "", sizeof(mqttsv_buffer));
  strlcpy(mqttusername_buffer, doc["mqtt_username"] | "", sizeof(mqttusername_buffer));
  strlcpy(mqttpassword_buffer, doc["mqtt_password"] | "", sizeof(mqttpassword_buffer));


  Serial.println("[ÉXITO] Credenciales leídas correctamente de la memoria Flash.");
  return true;
}

// Función para conectar/reconectar a MQTT
void reconnect() {
  while (!mqttClient.connected()) {
    Serial.println(WiFi.localIP());

    Serial.print("Intentando conexión MQTT... ");
    
    // Intentamos conectar con un ID único
    if (mqttClient.connect("ESP32Client", MQTTUSERNAME, MQTTPASSWORD)) {
      Serial.println("[CONECTADO] Exitosamente al Broker MQTT");
    } else {
      Serial.print("[FALLÓ] Código de error: ");
      Serial.print(mqttClient.state());
      Serial.println(" -> Reintentando en 5 segundos...");
      delay(5000);
    }
  }
}

void setup() {
  Serial.begin(115200);
  delay(1000); // Pequeña pausa para que se estabilice el monitor serie
  
  Serial.println("\n--- Iniciando ESP32 ---");
  if (!cargarCredenciales() || !cargarCertificadoCA()) {
    Serial.println("[CRÍTICO] Deteniendo ejecución por fallo en archivos del sistema.");
    while (true) { delay(1000); }
  }


  // Conexión WiFi
  Serial.print("Conectando a la red WiFi: ");
  Serial.println(WIFISSID);
  
  WiFi.begin(WIFISSID, WIFIPASSWORD);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print("."); // Efecto visual de carga
  }
  

  
  Serial.println("\n[CONECTADO] WiFi activo");
  Serial.print("Dirección IP asignada: ");
  Serial.println(WiFi.localIP());

  sincronizarHora();

  //wifiClient.setInsecure();
  wifiClient.setCACert(ca_cert_content.c_str());
  Serial.println("[ÉXITO] Certificado CA aplicado al motor TLS.");

  // Configuración del servidor MQTT
  mqttClient.setServer(MQTTSERVER, MQTTPORT);
}

void loop() {
  // Si el ESP32 se desconecta del MQTT, intenta reconectar
  if (!mqttClient.connected()) {
    reconnect();
  }
  mqttClient.loop(); // Mantiene viva la conexión y procesa mensajes entrantes


  long now = millis();
  if (now - previous_time > 1000) { 
    previous_time = now;

    //unsigned long timestamp = millis();

    struct timeval tv;
    gettimeofday(&tv, NULL);
    // Convertimos los segundos y microsegundos a milisegundos de 64 bits (Epoch de 13 dígitos)
    unsigned long long timestamp_ntp_ms = (unsigned long long)(tv.tv_sec) * 1000 + (tv.tv_usec / 1000);

    char msg_buffer[50]; 

    sprintf(msg_buffer, "{\"tr\": %llu}", timestamp_ntp_ms);

    Serial.print("Timestamp a enviar: ");
    Serial.println(msg_buffer);

    mqttClient.publish(topic_publish, msg_buffer);
    }
}
