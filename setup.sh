#!/usr/bin/env bash

# Detener la ejecución si ocurre un error
set -e

echo "🚀 Iniciando la configuración del entorno para Node-RED y Mosquitto..."

# ==========================================
# 1. Preparar Directorios
# ==========================================
echo "📁 Creando estructura de directorios..."
mkdir -p node-red-data
mkdir -p node-red-data/results
mkdir -p mosquitto/config
mkdir -p pingpong_timestamp/data

touch -env
touch pingpong_timestamp/data/config.json

# ==========================================
# 2. Definir / Verificar archivo .env
# ==========================================


# ==========================================
# 3. Copia de archivos de configuración
# ==========================================
echo "📋 Copiando archivos de configuración..."

# Copiar archivos de Node-RED si existen
[ -f flows.json ] && cp flows.json ./node-red-data/ || echo "⚠️  flows.json no encontrado, omitiendo..."
[ -f flows_cred.json ] && cp flows_cred.json ./node-red-data/ || echo "⚠️  flows_cred.json no encontrado, omitiendo..."
[ -f settings.js ] && cp settings.js ./node-red-data/ || echo "⚠️  settings.js no encontrado, omitiendo..."

# Copiar archivo de Mosquitto si existe
[ -f mosquitto.conf ] && cp mosquitto.conf ./mosquitto/config/ || echo "⚠️  mosquitto.conf no encontrado, omitiendo..."

# ==========================================
# 4. Asignación de Permisos (UID 1000 para contenedores)
# ==========================================
echo "🔒 Ajustando permisos para Docker (UID 1000:1000)..."
sudo chown -R 1000:1000 ./node-red-data ./mosquitto 2>/dev/null || echo "ℹ️  Carpeta compartida detectada: chown omitido para la carpeta sincronizada."
sudo chmod -R 777 ./node-red-data ./mosquitto 2>/dev/null || true

echo "✅ ¡Configuración completada con éxito! Ya puedes ejecutar 'docker compose up -d'."
