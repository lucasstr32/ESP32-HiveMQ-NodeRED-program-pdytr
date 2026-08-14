# ESP32 & Node-RED: Latency & RTT Calibration System

## 📌 Propósito General
Este repositorio proporciona una infraestructura automatizada mediante **Vagrant y Docker Compose** para desplegar un entorno local de procesamiento de datos IoT. Su objetivo principal es sincronizar marcas de tiempo (timestamps) y medir la latencia y el RTT (Round-Trip Time) entre placas **ESP32** y un flujo de **Node-RED**, permitiendo la alternancia transparente entre un broker **Mosquitto** local y un broker en la nube como **HiveMQ Cloud**.

---

## 🚀 Guía de Uso y Automatización

Toda la infraestructura (Docker, Mosquitto, Node-RED) se despliega dentro de una Máquina Virtual Ubuntu gestionada por Vagrant. **No necesitas instalar ni clonar nada dentro de la máquina virtual**; todas las acciones se ejecutan desde la terminal de tu computadora (Host).

### 📋 Requisitos Previos
* [VirtualBox](https://www.virtualbox.org/) instalado.
* [Vagrant](https://www.vagrantup.com/) instalado.
* Git.

---

### 🛠️ Comandos Principales

#### 1. Iniciar el Entorno
Para crear y encender la máquina virtual, instalar Docker y desplegar automáticamente los contenedores de Node-RED y Mosquitto:
```bash
vagrant up

```

**Nota:** La carpeta `./node-red-data` de tu máquina local se sincroniza automáticamente con la VM. Cualquier flujo o archivo generado (`lecturas.csv`, `medianas.json`) aparecerá directamente en tu computadora.



#### 2. Reaplicar Cambios o Provisionamiento

Si modificaste scripts como `setup.sh` o configuraciones en el `Vagrantfile`:

```bash
vagrant reload --provision

```

#### 3. Ver Logs de Node-RED en Tiempo Real

Para monitorear los mensajes de depuración de Node-RED y ver las lecturas capturadas sin entrar a la VM:

```bash
vagrant ssh -c "docker compose -f /vagrant/docker-compose.yml logs -f nodered"

```

#### 4. Apagar la Máquina Virtual

Para suspender o apagar la VM cuando termines de trabajar:

```bash
vagrant halt

```

#### 5. Destruir la VM (Limpieza completa)

Si deseas borrar la máquina virtual por completo (tus archivos locales en el repositorio **no** se borrarán):

```bash
vagrant destroy -f

```

---

### 🌐 Accesos Locales

Una vez ejecutado `vagrant up`, puedes acceder a los servicios desde el navegador de tu computadora:

* **Node-RED UI:** `http://localhost:1880`

---

## 🔄 Configuración del Firmware ESP32 (`pingpong_timestamp.ino`)

El firmware de la ESP32 soporta comunicación MQTT contra dos entornos distintos: **Mosquitto (Local)** y **HiveMQ Cloud (TLS)**.

### 1. Cambio de Broker desde el Código `.ino`

En la parte superior del archivo `pingpong_timestamp.ino`, busca la constante `MQTT_BROKER_OPTION`:

```cpp
// Opciones válidas: "Mosquitto" o "HiveMQ"
const char* MQTT_BROKER_OPTION = "Mosquitto"; 

```

* **Para usar Mosquitto Local:** Configura `"Mosquitto"`. La ESP32 usará comunicación estándar sin TLS por el puerto `1883`.


* **Para usar HiveMQ Cloud:** Configura `"HiveMQ"`. La ESP32 sincronizará la hora por NTP e iniciará la conexión cifrada TLS por el puerto `8883`.



---

### 2. Configuración de Credenciales (`config.json`)

El firmware lee sus credenciales desde el sistema de archivos **LittleFS** mediante el archivo `/config.json`.

#### Estructura requerida de `/config.json`:

```json
{
  "ssid": "NOMBRE_DE_TU_RED_WIFI",
  "password": "CONTRASEÑA_WIFI",
  "mosquitto_server": "192.168.1.X",
  "mqtt_server": "xxxxxx.s1.eu.hivemq.cloud",
  "mqtt_username": "usuario_hivemq",
  "mqtt_password": "password_hivemq"
}

```

> ⚠️ **IMPORTANTE - Dirección IP para Mosquitto:**
> En el campo `"mosquitto_server"` **no** debes poner `localhost`, `127.0.0.1` ni `mosquitto_local`. Debes colocar la **IP IPv4 local de tu computadora física** en la red Wi-Fi (ejemplo: `192.168.1.15`), ya que la ESP32 se conecta como un dispositivo externo en tu red local.

---

### 3. Requisitos de Certificados (HiveMQ TLS)

Para que la conexión con HiveMQ funcione correctamente:

1. Debes incluir el certificado CA en la carpeta `data` de la ESP32 con el nombre `hivemq_ca.pem`.


2. Flashea la carpeta de datos utilizando la herramienta **LittleFS Data Upload** de tu IDE (VS Code PlatformIO / Arduino IDE).

---

### 📊 Almacenamiento de Datos

El flujo de Node-RED está preparado para capturar los datos enviados por la ESP32 y guardarlos en el Host:

* **`node-red-data/resutls/lecturas_*.csv`**: Almacena en tiempo real la timestamp de emisión, corrección, offset y latencia por cada mensaje.
* **`node-red-data/results/medianas_*.json`**: Guarda un registro estructurado cada 200 mensajes con el cálculo de la mediana, piso y techo de latencia.

```

