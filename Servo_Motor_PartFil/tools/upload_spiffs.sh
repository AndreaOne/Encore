#!/bin/zsh

set -euo pipefail

ROOT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
SCRIPT_NAME="$(basename "$0")"
DATA_DIR="$ROOT_DIR/generated/encore/data"
IMAGE_PATH="$ROOT_DIR/generated/encore/spiffs_image.bin"

PORT=""
BAUD="921600"
CHIP="esp32"
OFFSET="0x290000"
SIZE="0x160000"
BLOCK="4096"
PAGE="256"

MKSPIFFS="$HOME/Library/Arduino15/packages/esp32/tools/mkspiffs/0.2.3/mkspiffs"
ESPTOOL="$HOME/Library/Arduino15/packages/esp32/tools/esptool_py/5.1.0/esptool"

print_usage() {
  cat <<EOF
Usage:
  $SCRIPT_NAME --port /dev/cu.usbserial-XXXX

Optional flags:
  --port PATH        Serial port to flash.
  --baud RATE        Flash baud rate. Default: $BAUD
  --chip NAME        Chip name for esptool. Default: $CHIP
  --offset HEX       SPIFFS partition offset. Default: $OFFSET
  --size HEX         SPIFFS partition size. Default: $SIZE
  --data-dir PATH    Directory to pack into SPIFFS. Default: $DATA_DIR
  --image PATH       Output image path. Default: $IMAGE_PATH
  --help             Show this message.

Notes:
  - Defaults match the common ESP32 "default" partition table.
  - The data directory should contain files like score_bundle.json and particle_filter_config.yaml.
EOF
}

list_candidate_ports() {
  local candidate
  local -a ports
  ports=(
    /dev/cu.usb*(N)
    /dev/cu.usbserial*(N)
    /dev/cu.usbmodem*(N)
    /dev/cu.SLAB*(N)
    /dev/cu.wchusb*(N)
  )

  for candidate in "${ports[@]}"; do
    printf '%s\n' "$candidate"
  done | awk '!seen[$0]++'
}

auto_detect_port() {
  local ports_text
  local ports
  ports_text="$(list_candidate_ports)"
  if [[ -z "$ports_text" ]]; then
    return 1
  fi

  ports=("${(@f)ports_text}")

  if (( ${#ports[@]} == 1 )); then
    PORT="${ports[1]}"
    return 0
  fi

  return 1
}

while (( $# > 0 )); do
  case "$1" in
    --port)
      PORT="$2"
      shift 2
      ;;
    --baud)
      BAUD="$2"
      shift 2
      ;;
    --chip)
      CHIP="$2"
      shift 2
      ;;
    --offset)
      OFFSET="$2"
      shift 2
      ;;
    --size)
      SIZE="$2"
      shift 2
      ;;
    --data-dir)
      DATA_DIR="$2"
      shift 2
      ;;
    --image)
      IMAGE_PATH="$2"
      shift 2
      ;;
    --help|-h)
      print_usage
      exit 0
      ;;
    *)
      echo "Unknown argument: $1" >&2
      print_usage >&2
      exit 1
      ;;
  esac
done

if [[ ! -x "$MKSPIFFS" ]]; then
  echo "mkspiffs not found at $MKSPIFFS" >&2
  exit 1
fi

if [[ ! -x "$ESPTOOL" ]]; then
  echo "esptool not found at $ESPTOOL" >&2
  exit 1
fi

if [[ ! -d "$DATA_DIR" ]]; then
  echo "Data directory not found: $DATA_DIR" >&2
  exit 1
fi

if [[ ! -f "$DATA_DIR/score_bundle.json" ]]; then
  echo "Missing $DATA_DIR/score_bundle.json" >&2
  exit 1
fi

if [[ -z "$PORT" ]]; then
  if ! auto_detect_port; then
    echo "Could not auto-detect a single ESP32 serial port." >&2
    echo "Available USB-like ports:" >&2
    list_candidate_ports >&2 || true
    echo "Pass --port /dev/cu.usbserial-XXXX explicitly." >&2
    exit 1
  fi
fi

if [[ ! -e "$PORT" ]]; then
  echo "Serial port not found: $PORT" >&2
  exit 1
fi

mkdir -p "$(dirname "$IMAGE_PATH")"

echo "Packing SPIFFS image from: $DATA_DIR"
echo "Output image: $IMAGE_PATH"
"$MKSPIFFS" -c "$DATA_DIR" -b "$BLOCK" -p "$PAGE" -s "$SIZE" "$IMAGE_PATH"

echo "Flashing SPIFFS image to $PORT"
echo "Chip: $CHIP  Baud: $BAUD  Offset: $OFFSET  Size: $SIZE"
"$ESPTOOL" --chip "$CHIP" --port "$PORT" --baud "$BAUD" --before default-reset --after hard-reset \
  write-flash "$OFFSET" "$IMAGE_PATH"

echo "SPIFFS upload complete."
