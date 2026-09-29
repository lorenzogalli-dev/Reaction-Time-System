#!/bin/bash
# Compila e carica EspNowLrTest su un ESP32-C3 SuperMini (stesso firmware per A e B).
# Il ruolo non si sceglie qui: lo imposta logger.py A|B al primo collegamento.
# Uso:  ./flash.sh          compila e carica sulla prima scheda trovata
#       ./flash.sh compile  solo compila
set -e
cd "$(dirname "$0")"
CLI="/Applications/Arduino IDE.app/Contents/Resources/app/lib/backend/resources/arduino-cli"
FQBN="esp32:esp32:esp32c3:CDCOnBoot=cdc"
# Il ctags di Arduino e' un binario Intel e su questo Mac, senza Rosetta, non
# parte. Serve solo a generare prototipi per i .ino, e il nostro e' vuoto:
# lo si sostituisce con un comando che non fa niente, solo per questo sketch.
NOCTAGS="tools.ctags.pattern=/usr/bin/true"

"$CLI" compile --fqbn "$FQBN" --build-property "$NOCTAGS" EspNowLrTest
[ "$1" = "compile" ] && exit 0

PORT=$(ls /dev/cu.usbmodem* 2>/dev/null | head -1)
if [ -z "$PORT" ]; then
  echo "Nessun ESP trovato su /dev/cu.usbmodem*"
  echo "Collega l'ESP32-C3 via USB-C. Se resta invisibile: tieni premuto BOOT,"
  echo "collega il cavo, rilascia BOOT, e rilancia."
  exit 1
fi
echo "porta: $PORT"
"$CLI" upload --fqbn "$FQBN" --port "$PORT" EspNowLrTest
echo
echo "fatto. Ora: python3 logger.py A   (oppure B)"
