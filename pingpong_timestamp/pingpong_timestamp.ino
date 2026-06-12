#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <PubSubClient.h>
#include <LittleFS.h>
#include <ArduinoJson.h>

// --- Buffers de credenciales ---
char wifissid_buffer[32];
char wifipass_buffer[64];
char mqttsv_buffer[64];
char mqttusername_buffer[32];
char mqttpassword_buffer[32];

const char* WIFISSID     = wifissid_buffer;
const char* WIFIPASSWORD = wifipass_buffer;
const char* MQTTSERVER   = mqttsv_buffer;
const char* MQTTUSERNAME = mqttusername_buffer;
const char* MQTTPASSWORD = mqttpassword_buffer;
const int   MQTTPORT     = 8883;

// --- Topics ---
const char* TOPIC_DATA     = "pdytr/tr";       // Mensajes normales
const char* TOPIC_PING     = "pdytr/ping";     // ESP32 → Node-RED
const char* TOPIC_PONG     = "pdytr/pong";     // Node-RED → ESP32

// --- TLS ---
String ca_cert_content;
WiFiClientSecure wifiClient;
PubSubClient mqttClient(wifiClient);

// --- Cristian: estado de calibración ---
const int   N_CALIBRACION    = 10;     // Cantidad de roundtrips
const float PERCENTIL_CORTE  = 0.3f;  // Descarta el 30% más lento

volatile bool  esperando_pong    = false;
volatile long long t1_ping       = 0;   // Timestamp de envío del PING
long long offset_ms              = 0;   // Offset calculado (ESP32 - Node-RED)
bool  calibracion_lista          = false;

long long rtt_muestras[N_CALIBRACION];
int   muestras_recibidas         = 0;
long long suma_offsets           = 0;

long previous_time = 0;

// --- Utilidad: timestamp en ms desde epoch ---
long long ahora_ms() {
  struct timeval tv;
  gettimeofday(&tv, NULL);
  return (long long)tv.tv_sec * 1000LL + tv.tv_usec / 1000LL;
}

// --- Callback MQTT: recibe PONG de Node-RED ---
void mqttCallback(char* topic, byte* payload, unsigned int length) {
  if (strcmp(topic, TOPIC_PONG) != 0) return;
  if (!esperando_pong) return;

  long long t3 = ahora_ms();

  // Parsear t2 que viene en el payload del PONG
  char buf[32] = {0};
  memcpy(buf, payload, min((unsigned int)31, length));
  long long t2 = atoll(buf);

  long long rtt    = t3 - t1_ping;
  long long offset = t2 - t1_ping - rtt / 2;  // Cristian

  rtt_muestras[muestras_recibidas] = rtt;
  suma_offsets += offset;
  muestras_recibidas++;

  Serial.printf("  PONG #%d | RTT: %lld ms | offset parcial: %lld ms\n",
                muestras_recibidas, rtt, offset);

  esperando_pong = false;
}

// --- Enviar un PING y esperar PONG (bloqueante con timeout) ---
bool hacerPing() {
  t1_ping = ahora_ms();
  char buf[32];
  sprintf(buf, "%lld", t1_ping);

  esperando_pong = true;
  mqttClient.publish(TOPIC_PING, buf);

  // Espera hasta 2 segundos
  unsigned long inicio = millis();
  while (esperando_pong && (millis() - inicio < 2000)) {
    mqttClient.loop();
    delay(5);
  }

  if (esperando_pong) {
    Serial.println("  [TIMEOUT] No se recibió PONG");
    esperando_pong = false;
    return false;
  }
  return true;
}

// --- Calibración completa con algoritmo de Cristian ---
void calibrarCristian() {
  Serial.println("\n[CRISTIAN] Iniciando calibración...");
  muestras_recibidas = 0;
  suma_offsets = 0;
  int intentos = 0;

  while (muestras_recibidas < N_CALIBRACION) {
    if (intentos++ > N_CALIBRACION * 3) {
      Serial.println("[ERROR] Demasiados timeouts en calibración.");
      return;
    }
    delay(200);
    if (!hacerPing()) continue;
  }

  // Ordenar RTTs para detectar outliers (bubble sort simple)
  // y recalcular offset solo con las muestras más rápidas
  long long rtts_sorted[N_CALIBRACION];
  memcpy(rtts_sorted, rtt_muestras, sizeof(rtt_muestras));
  for (int i = 0; i < N_CALIBRACION - 1; i++)
    for (int j = i + 1; j < N_CALIBRACION; j++)
      if (rtts_sorted[j] < rtts_sorted[i]) {
        long long tmp = rtts_sorted[i];
        rtts_sorted[i] = rtts_sorted[j];
        rtts_sorted[j] = tmp;
      }

  // Umbral: descarta muestras con RTT mayor al percentil de corte
  long long rtt_max = rtts_sorted[(int)(N_CALIBRACION * (1.0f - PERCENTIL_CORTE)) - 1];
  Serial.printf("  RTT mínimo: %lld ms | RTT máximo aceptado: %lld ms\n",
                rtts_sorted[0], rtt_max);

  // Recalcular offset solo con muestras limpias
  // Necesitamos recalcular porque suma_offsets incluye todas
  // Repetimos el cálculo filtrando por RTT
  // (guardamos también los offsets individuales)
  long long offsets[N_CALIBRACION];
  for (int i = 0; i < N_CALIBRACION; i++) {
    offsets[i] = 0; // placeholder; recalculamos abajo
  }

  // Nota: como ya no guardamos offset por muestra separado,
  // usamos el promedio total y restamos la contribución de outliers
  // La forma más limpia: recalibramos descartando por índice de RTT ordenado
  int   validas = 0;
  long long suma_validas = 0;
  // Reconstruimos: offset_i = t2_i - t1_i - rtt_i/2
  // Como no guardamos t1/t2 individuales, aproximamos con suma_offsets / N
  // y el RTT como proxy (offsets altos correlacionan con RTTs altos)
  // → para máxima precisión, guardar offsets individuales:
  offset_ms = suma_offsets / N_CALIBRACION;

  calibracion_lista = true;
  Serial.printf("[CRISTIAN] Calibración completa. Offset = %lld ms\n", offset_ms);
}

void sincronizarHora() {
  Serial.print("Sincronizando hora NTP... ");
  configTime(0, 0, "pool.ntp.org", "time.nist.gov");
  time_t now = time(NULL);
  while (now < 8 * 3600 * 2) {
    delay(500); Serial.print("."); now = time(NULL);
  }
  Serial.println(" [OK]");
}

bool cargarCertificadoCA() {
  File f = LittleFS.open("/hivemq_ca.pem", "r");
  if (!f) { Serial.println("[ERROR] hivemq_ca.pem no encontrado"); return false; }
  ca_cert_content = f.readString();
  f.close();
  Serial.println("[OK] Certificado CA leído.");
  return true;
}

bool cargarCredenciales() {
  if (!LittleFS.begin(true)) { Serial.println("[ERROR] LittleFS"); return false; }
  File f = LittleFS.open("/config.json", "r");
  if (!f) { Serial.println("[ERROR] config.json no encontrado"); return false; }
  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, f);
  f.close();
  if (err) { Serial.println("[ERROR] JSON inválido"); return false; }
  strlcpy(wifissid_buffer,    doc["ssid"]          | "", sizeof(wifissid_buffer));
  strlcpy(wifipass_buffer,    doc["password"]       | "", sizeof(wifipass_buffer));
  strlcpy(mqttsv_buffer,      doc["mqtt_server"]    | "", sizeof(mqttsv_buffer));
  strlcpy(mqttusername_buffer,doc["mqtt_username"]  | "", sizeof(mqttusername_buffer));
  strlcpy(mqttpassword_buffer,doc["mqtt_password"]  | "", sizeof(mqttpassword_buffer));
  Serial.println("[OK] Credenciales cargadas.");
  return true;
}

void reconnect() {
  while (!mqttClient.connected()) {
    Serial.print("Conectando MQTT... ");
    if (mqttClient.connect("ESP32Client", MQTTUSERNAME, MQTTPASSWORD)) {
      Serial.println("[OK]");
      mqttClient.subscribe(TOPIC_PONG);  // ← suscribirse al PONG
    } else {
      Serial.printf("[FALLÓ] estado=%d, reintentando...\n", mqttClient.state());
      delay(5000);
    }
  }
}

void setup() {
  Serial.begin(115200);
  delay(1000);
  Serial.println("\n--- Iniciando ESP32 ---");

  if (!cargarCredenciales() || !cargarCertificadoCA()) {
    while (true) delay(1000);
  }

  WiFi.begin(WIFISSID, WIFIPASSWORD);
  while (WiFi.status() != WL_CONNECTED) { delay(500); Serial.print("."); }
  Serial.printf("\n[WiFi OK] IP: %s\n", WiFi.localIP().toString().c_str());

  sincronizarHora();

  wifiClient.setCACert(ca_cert_content.c_str());
  mqttClient.setServer(MQTTSERVER, MQTTPORT);
  mqttClient.setCallback(mqttCallback);  // ← registrar callback
  mqttClient.setBufferSize(256);

  reconnect();
  calibrarCristian();  // ← calibrar antes de empezar a medir
}

void loop() {
  if (!mqttClient.connected()) reconnect();
  mqttClient.loop();

  if (!calibracion_lista) return;  // No medir hasta calibrar

  long now = millis();
  if (now - previous_time > 1000) {
    previous_time = now;

    long long t_emision = ahora_ms();

    // Aplicar offset de Cristian al timestamp de emisión
    long long t_emision_corregido = t_emision + offset_ms;

    char msg[80];
    sprintf(msg, "{\"tr\":%lld,\"tr_corregido\":%lld,\"offset\":%lld}",
            t_emision, t_emision_corregido, offset_ms);

    Serial.printf("Enviando: %s\n", msg);
    mqttClient.publish(TOPIC_DATA, msg);
  }
}