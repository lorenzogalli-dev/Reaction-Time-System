#!/bin/bash
# Compila e carica WifiFieldTest sull'ESP32-C3 SuperMini.
# Uso:  ./flash.sh          compila e carica
#       ./flash.sh monitor  ...e poi apre la seriale
set -e
cd "$(dirname "$0")"
CLI="/Applications/Arduino IDE.app/Contents/Resources/app/lib/backend/resources/arduino-cli"
FQBN="esp32:esp32:esp32c3:CDCOnBoot=cdc"
LIB="$HOME/Documents/Arduino/libraries/WebSockets"

PORT=$(ls /dev/cu.usbmodem* 2>/dev/null | head -1)
if [ -z "$PORT" ]; then
  echo "Nessun ESP trovato su /dev/cu.usbmodem*"
  echo "Collega l'ESP32-C3 via USB-C. Se resta invisibile: tieni premuto BOOT,"
  echo "collega il cavo, rilascia BOOT, e rilancia."
  exit 1
fi
echo "porta: $PORT"

"$CLI" compile --fqbn "$FQBN" --library "$LIB" WifiFieldTest
"$CLI" upload  --fqbn "$FQBN" --port "$PORT" WifiFieldTest
echo
echo "fatto. Rete \"PROSTART-TEST\", password \"prostart123\", poi http://192.168.4.1"

[ "$1" = "monitor" ] && "$CLI" monitor --port "$PORT" --config baudrate=115200
