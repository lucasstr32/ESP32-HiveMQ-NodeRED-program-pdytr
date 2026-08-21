#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <PubSubClient.h>
#include <LittleFS.h>
#include <ArduinoJson.h>

int cant = 0;

// --- Credential buffers ---
char wifissid_buffer[32];
char wifipass_buffer[64];
char mqttsv_buffer[64];
char mqttusername_buffer[32];
char mqttpassword_buffer[32];

/* --- Constants that links to credential buffers */
const char* MQTT_BROKER_OPTION = "Mosquitto"; // "HiveMQ" "Mosquitto"
const char* WIFISSID     = wifissid_buffer;
const char* WIFIPASSWORD = wifipass_buffer;
const char* MQTTSERVER   = mqttsv_buffer;
const char* MQTTUSERNAME = mqttusername_buffer;
const char* MQTTPASSWORD = mqttpassword_buffer;
const int   MQTT_TLS_PORT = 8883;
const int   MQTT_NO_TLS_PORT = 1883;

// --- Topics ---
const char* TOPIC_DATA     = "pdytr/tr";       // real time messages
const char* TOPIC_PING     = "pdytr/ping";     // ESP32 -> Node-RED
const char* TOPIC_PONG     = "pdytr/pong";     // Node-RED -> ESP32

// --- TLS ---
String ca_cert_content;
WiFiClientSecure wifiClientSecure; // para hivemq
WiFiClient wifiClient; // para mosquitto
PubSubClient mqttClient(wifiClient);


// --- Calibration algorithm variables ---
const int   CALIBRATION_COUNT    = 100;   // roundtrip count
const float SAMPLE_THRESHOLD    = 0.3f;  // Discard the lowest 30%

volatile bool  waitingPong      = false;
volatile long long t0Ping       = 0;   // Timestamp of first ping
long long offsetMs              = 0;   // Offset between ESP and Node-Red
bool  calibrationReady          = false;

volatile long long lastCalculatedRTT    = 0;
volatile long long lastCalculatedOffset = 0;


struct CalibrationSample {
  long long rtt;
  long long offset;
};


CalibrationSample rttSamples[CALIBRATION_COUNT];
int receivedSamples             = 0;
long previousTime               = 0;


void setup() {
  Serial.begin(115200);
  delay(1000);
  Serial.println("\n--- Initializing ESP32 ---");

  /* Loading WiFi and MQTT credentials and CA Certificate */
  if (!loadCredentials() || !loadCACertificate()) { // while?
    while (true) delay(1000);
  }

  /* WiFi init */
  WiFi.begin(WIFISSID, WIFIPASSWORD);
  while (WiFi.status() != WL_CONNECTED) { 
    delay(500); 
    Serial.print("."); 
  }
  Serial.printf("\n[WiFi OK] IP: %s\n", WiFi.localIP().toString().c_str());
  WiFi.setSleep(false);
  timeSynch();
  
  if(MQTT_BROKER_OPTION == "HiveMQ"){
    Serial.println("[ENV] Configurando entorno seguro para HiveMQ Cloud...");
    
    wifiClientSecure.setCACert(ca_cert_content.c_str());
    if (loadCACertificate()) {
      wifiClientSecure.setCACert(ca_cert_content.c_str());
    } else {
      Serial.println("[CRITICAL ERROR] No se pudo cargar el certificado para HiveMQ");
      while(1) delay(1000);
    }
    mqttClient.setClient(wifiClientSecure);
    mqttClient.setServer(MQTTSERVER, MQTT_TLS_PORT);
  } else {
    // mosquitto
    Serial.println("[ENV] Configurando entorno estándar para Mosquitto Local...");
    mqttClient.setClient(wifiClient);
    mqttClient.setServer(MQTTSERVER, MQTT_NO_TLS_PORT);
  }
  
  mqttClient.setCallback(mqttCallback);  // callback registration
  mqttClient.setBufferSize(256);

  reconnect();
  calibrateRTT();  
}


//////////////////////////////////
/// CONFIG AND SYNCH FUNCTIONS ///
//////////////////////////////////

bool loadCredentials() {
  /* Function that loads WiFi and MQTT credentials from a JSON*/

  /* Initialization of LittleFS */
  if (!LittleFS.begin(true)) { 
    Serial.println("[ERROR] LittleFS could not be initialized"); 
    return false; 
  }

  /* Opening credential file */
  File f = LittleFS.open("/config.json", "r");
  if (!f) { 
    Serial.println("[ERROR] config.json not found");
    return false; 
  }
  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, f);
  f.close();

  if (err) { 
    Serial.println("[ERROR] invalid JSON"); 
    return false; 
  }

  /* Parsing of credentials */
  strlcpy(wifissid_buffer,    doc["ssid"]          | "", sizeof(wifissid_buffer));
  strlcpy(wifipass_buffer,    doc["password"]       | "", sizeof(wifipass_buffer));
  
  /* para hivemq */
  if(MQTT_BROKER_OPTION == "HiveMQ"){
    strlcpy(mqttsv_buffer,      doc["mqtt_server"]    | "", sizeof(mqttsv_buffer));
    strlcpy(mqttusername_buffer,doc["mqtt_username"]  | "", sizeof(mqttusername_buffer));
    strlcpy(mqttpassword_buffer,doc["mqtt_password"]  | "", sizeof(mqttpassword_buffer));

  } else {
    strlcpy(mqttsv_buffer,      doc["mosquitto_server"] , sizeof(mqttsv_buffer));
    strlcpy(mqttusername_buffer, ""  , sizeof(mqttusername_buffer));
    strlcpy(mqttpassword_buffer, ""  , sizeof(mqttpassword_buffer));
    
  }
  

  Serial.println("[OK] Credentials loaded successfully.");
  return true;
}

bool loadCACertificate() {
  /* Function that loads a CA Certificate 
  */
  File f = LittleFS.open("/hivemq_ca.pem", "r");
  if (!f) { 
    Serial.println("[ERROR] hivemq_ca.pem not found"); 
    return false; 
  }
  ca_cert_content = f.readString();
  f.close();
  Serial.println("[OK] CA Certificate successfully read.");
  return true;
}

void timeSynch() {
  /* Function that synchronizes time with NTP */
  Serial.print("Synchronizing time with NTP... ");
  configTime(0, 0, "pool.ntp.org", "time.nist.gov");
  time_t now = time(NULL);
  while (now < 8 * 3600 * 2) {
    /* While time is < 1970 */
    delay(500); 
    Serial.print("."); 
    now = time(NULL);
  }
  Serial.println("[OK] Time successfully synchronized");
}

void reconnect() {
  /* Function that connects to MQTT broker */

  while (!mqttClient.connected()) {
    Serial.print("Connecting MQTT... ");
    if (mqttClient.connect("ESP32Client", MQTTUSERNAME, MQTTPASSWORD)) {
      Serial.println("[OK] MQTT Connected");
      mqttClient.subscribe(TOPIC_PONG);  // subscribe to pong
    } else {
      Serial.printf("[FAILED] state=%d, retrying...\n", mqttClient.state());
      delay(5000);
    }
  }
}



/////////////////////////////////
/// RTT CALCULATION FUNCTIONS ///
/////////////////////////////////

void calibrateRTT() {

  Serial.println("\n[CALIBRATION] Initializing RTT calibration...");
  int intents = 0;
  const int TOTAL_PINGS = 50;

  int validSamplesCount = 0;
  while (receivedSamples < CALIBRATION_COUNT) {
    if (intents++ > TOTAL_PINGS * 3) {
      Serial.println("[ERROR] Too much calibration timeouts.");
      return;
    }
    if (doPing()) {
      // Ignoramos las primeras 3 muestras de la ráfaga (Precalentamiento de antena)
      if (intents <= 3) {
        Serial.printf("  [WARM-UP] Discarding initial sample #%d to wake up Wi-Fi\n", intents);
        continue;
      }
      
      rttSamples[validSamplesCount].rtt = lastCalculatedRTT;
      rttSamples[validSamplesCount].offset = lastCalculatedOffset;
      validSamplesCount++;
    }
  }

  /* Bubble sort algorithm to detect outliers and recalculate offset*/
  for (int i = 0; i < CALIBRATION_COUNT - 1; i++) {
    for (int j = i + 1; j < CALIBRATION_COUNT; j++) {
      if (rttSamples[j].rtt < rttSamples[i].rtt) {
        CalibrationSample tmp = rttSamples[i];
        rttSamples[i] = rttSamples[j];
        rttSamples[j] = tmp;
      }
    }
  }

  // En este punto, localSamples[0] contiene el mensaje MÁS RÁPIDO Y PRECISO del sistema.
  Serial.printf("  [STATS] Best RTT (Cleanest path): %lld ms\n", rttSamples[0].rtt);
  Serial.printf("  [STATS] Worst RTT accepted: %lld ms\n", rttSamples[CALIBRATION_COUNT - 1].rtt);

  /* 3. APLICAR FILTRO DE MEDIANA SOBRE LAS MEJORES MUESTRAS */
  // Descartamos el 30% de las muestras con los RTTs más altos (las que se trabaron)
  int strictSamplesCount = (int)(CALIBRATION_COUNT * (1.0f - SAMPLE_THRESHOLD)); // Ej: 10 * 0.7 = 7 muestras
  
  // Para encontrar la mediana de los offsets de estas muestras limpias, los ordenamos a ellos
  long long cleanOffsets[strictSamplesCount];
  for (int i = 0; i < strictSamplesCount; i++) {
    cleanOffsets[i] = rttSamples[i].offset;
  }
  
  // Ordenamos los offsets numéricamente
  for (int i = 0; i < strictSamplesCount - 1; i++) {
    for (int j = i + 1; j < strictSamplesCount; j++) {
      if (cleanOffsets[j] < cleanOffsets[i]) {
        long long tmp = cleanOffsets[i];
        cleanOffsets[i] = cleanOffsets[j];
        cleanOffsets[j] = tmp;
      }
    }
  }

  // Seleccionamos el valor central (Mediana)
  int middleIndex = strictSamplesCount / 2;
  offsetMs = cleanOffsets[middleIndex];

  calibrationReady = true;
  Serial.printf("[CALIBRATION] Completed RTT calibration. Offset = %lld ms\n", offsetMs);
}





bool doPing() {
  /* Send a ping and wait a pong (blocking) */
  t0Ping = msNow();
  char buf[32];
  sprintf(buf, "%lld", t0Ping);

  waitingPong = true;
  mqttClient.publish(TOPIC_PING, buf);

  // Espera hasta 2 segundos
  unsigned long start = millis();
  while (waitingPong && (millis() - start < 2000)) {
    mqttClient.loop();
    delay(5);
  }

  if (waitingPong) {
    Serial.println("  [TIMEOUT] No se recibió PONG");
    waitingPong = false;
    return false;
  }
  return true;
}



long long msNow() {
  /* Gets timestamp */

  struct timeval tv;
  gettimeofday(&tv, NULL);
  return (long long)tv.tv_sec * 1000LL + tv.tv_usec / 1000LL;
}



// --- Callback MQTT: receives pong from Node-RED ---
void mqttCallback(char* topic, byte* payload, unsigned int length) {
  if (strcmp(topic, TOPIC_PONG) != 0) return;
  if (!waitingPong) return;

  long long t2 = msNow();

  // Parsear t2 que viene en el payload del PONG
  char buf[32] = {0};
  memcpy(buf, payload, min((unsigned int)31, length));
  long long t1 = atoll(buf);

  long long rtt    = t2 - t0Ping;
  long long offset = t1 - t0Ping - rtt / 2;  

  lastCalculatedRTT = rtt;
  lastCalculatedOffset = offset;

  rttSamples[receivedSamples].rtt = rtt;
  rttSamples[receivedSamples].offset = offset;
  receivedSamples++;

  Serial.printf("  PONG #%d | RTT: %lld ms | offset percibido: %lld ms\n",
                receivedSamples, rtt, offset);

  waitingPong = false;
}






void loop() {
  if (!mqttClient.connected()) reconnect();
  mqttClient.loop();

  if (!calibrationReady) return;  

  long now = millis();
  if (now - previousTime > 1000) {
    previousTime = now;

    long long emissionTimestamp = msNow();

    long long emissionTimestampCorrected = emissionTimestamp + offsetMs;

    char msg[80];
    sprintf(msg, "{\"emission_ts\":%lld,\"corrected_ts\":%lld,\"offset\":%lld}",
            emissionTimestamp, emissionTimestampCorrected, offsetMs);
    Serial.printf("Enviados: %d\n", cant++);
    Serial.printf("Sending: %s\n", msg);
    mqttClient.publish(TOPIC_DATA, msg);
  }
}