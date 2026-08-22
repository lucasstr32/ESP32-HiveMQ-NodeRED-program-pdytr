#!/usr/bin/env bash

# Detener la ejecución si ocurre un error
set -e

echo "🚀 Iniciando la configuración del entorno para Node-RED y Mosquitto..."

# ==========================================
# 1. Preparar Directorios
# ==========================================
echo "📁 Creando estructura de directorios..."
mkdir -p pingpong_timestamp/data

touch pingpong_timestamp/data/config.json
touch .env


