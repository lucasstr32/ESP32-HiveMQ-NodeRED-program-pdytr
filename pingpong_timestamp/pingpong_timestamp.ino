#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <PubSubClient.h>
#include <LittleFS.h>
#include <ArduinoJson.h>

// --- Credential buffers ---
char wifissid_buffer[32];
char wifipass_buffer[64];
char mqttsv_buffer[64];
char mqttusername_buffer[32];
char mqttpassword_buffer[32];

/* --- Constants that links to credential buffers */
const char* WIFISSID     = wifissid_buffer;
const char* WIFIPASSWORD = wifipass_buffer;
const char* MQTTSERVER   = mqttsv_buffer;
const char* MQTTUSERNAME = mqttusername_buffer;
const char* MQTTPASSWORD = mqttpassword_buffer;
const int   MQTTPORT     = 8883;

// --- Topics ---
const char* TOPIC_DATA     = "pdytr/tr";       // real time messages
const char* TOPIC_PING     = "pdytr/ping";     // ESP32 -> Node-RED
const char* TOPIC_PONG     = "pdytr/pong";     // Node-RED -> ESP32

// --- TLS ---
String ca_cert_content;
WiFiClientSecure wifiClient;
PubSubClient mqttClient(wifiClient);

// --- Cristian algorithm variables ---
const int   CALIBRATION_COUNT    = 10;     // roundtrip count
const float SAMPLE_THRESHOLD    = 0.3f;  // Discard the lowest 30%

volatile bool  waitingPong      = false;
volatile long long t0Ping       = 0;   // Timestamp of first ping
long long offsetMs              = 0;   // Offset between ESP and Node-Red
bool  calibrationReady          = false;

long long rtt_muestras[CALIBRATION_COUNT];
int   receivedSamples         = 0;
long long offsetSum           = 0;

long previousTime = 0;


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

  timeSynch();

  wifiClient.setCACert(ca_cert_content.c_str());
  mqttClient.setServer(MQTTSERVER, MQTTPORT);
  mqttClient.setCallback(mqttCallback);  // callback registration
  mqttClient.setBufferSize(256);

  reconnect();
  calibrateCristian();  
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
  strlcpy(mqttsv_buffer,      doc["mqtt_server"]    | "", sizeof(mqttsv_buffer));
  strlcpy(mqttusername_buffer,doc["mqtt_username"]  | "", sizeof(mqttusername_buffer));
  strlcpy(mqttpassword_buffer,doc["mqtt_password"]  | "", sizeof(mqttpassword_buffer));
  
  Serial.println("[OK] Credentials loaded successfully.");
  return true;
}

bool loadCACertificate() {
  /* Function that loads a CA Certificate 
  */
  File f = LittleFS.open("/hivemq_ca.pem", "r");
  if (!f) { 
    Serial.println("[ERROR] hivemq_ca.pem not found"); r
    eturn false; 
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

void calibrateCristian() {

  Serial.println("\n[CRISTIAN] Initializing Cristian calibration...");
  receivedSamples = 0;
  OffsetSum = 0;
  int intents = 0;

  while (receivedSamples < CALIBRATION_COUNT) {
    if (intents++ > CALIBRATION_COUNT * 3) {
      Serial.println("[ERROR] Too much calibration timeouts.");
      return;
    }
    delay(200);
    if (!doPing()) continue;
  }

  /* Bubble sort algorithm to detect outliers and recalculate offset*/
  long long rttsSorted[CALIBRATION_COUNT];
  memcpy(rttsSorted, rttSamples, sizeof(rttSamples));
  for (int i = 0; i < CALIBRATION_COUNT - 1; i++)
    for (int j = i + 1; j < CALIBRATION_COUNT; j++)
      if (rttsSorted[j] < rttsSorted[i]) {
        long long tmp = rttsSorted[i];
        rttsSorted[i] = rttsSorted[j];
        rttsSorted[j] = tmp;
      }

  /* Discarding samples by threshold */
  long long rttMax = rttsSorted[(int)(CALIBRATION_COUNT * (1.0f - SAMPLE_THRESHOLD)) - 1];
  Serial.printf("  Minimum RTT: %lld ms | Maximum RTT accepted: %lld ms\n",
                rttsSorted[0], rttMax);

  /* Recalculate offset with clean samples*/

  long long offsets[CALIBRATION_COUNT];
  for (int i = 0; i < CALIBRATION_COUNT; i++) {
    offsets[i] = 0; 
  }

  int   valids = 0;
  long long sumOfValids = 0;
  // Reconstruimos: offset_i = t2_i - t1_i - rtt_i/2
  // Como no guardamos t1/t2 individuales, aproximamos con suma_offsets / N
  // y el RTT como proxy (offsets altos correlacionan con RTTs altos)
  // → para máxima precisión, guardar offsets individuales:
  offsetMS = offsetSum / CALIBRATION_COUNT;

  calibrationReady = true;
  Serial.printf("[CRISTIAN] Completed Cristian calibration. Offset = %lld ms\n", offsetMs);
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

// --- Callback MQTT: recibe PONG de Node-RED ---
void mqttCallback(char* topic, byte* payload, unsigned int length) {
  if (strcmp(topic, TOPIC_PONG) != 0) return;
  if (!waitingPong) return;

  long long t2 = nowMs();

  // Parsear t2 que viene en el payload del PONG
  char buf[32] = {0};
  memcpy(buf, payload, min((unsigned int)31, length));
  long long t1 = atoll(buf);

  long long rtt    = t3 - t0Ping;
  long long offset = t2 - t0Ping - rtt / 2;  // Cristian

  rttSamples[receivedSamples] = rtt;
  offsetSum += offset;
  receivedSamples++;

  Serial.printf("  PONG #%d | RTT: %lld ms | offset parcial: %lld ms\n",
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

    long long emissionTimestamp = nowMs();

    long long emissionTimestampCorrected = emissionTimestamp + offsetMs;

    char msg[80];
    sprintf(msg, "{\"tr\":%lld,\"corrected_tr\":%lld,\"offset\":%lld}",
            emissionTimestamp, emissionTimestampCorrected, offsetMs);

    Serial.printf("Sending: %s\n", msg);
    mqttClient.publish(TOPIC_DATA, msg);
  }
}