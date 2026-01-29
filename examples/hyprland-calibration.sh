#!/usr/bin/env bash
set -euo pipefail

if [[ $# -ne 3 ]]; then
  echo "Usage: $0 <wait_seconds> <step_distance> <max>"
  exit 1
fi

WAIT="$1" # Time between steps
STEP="$2" # How many nits to increase per-step
MAX="$3"  # Maximum brightness to test

DISPLAY="$(hyprctl monitors -j | jq -r '.[] | select(.focused==true) | .name')"

if [[ -z "$DISPLAY" ]]; then
  echo "Error: Could not determine focused monitor"
  exit 1
fi

val=0

while (( val <= MAX )); do
  echo "Setting $DISPLAY sdr_brightness = $val"

  hyprctl keyword "monitorv2[$DISPLAY]:sdr_max_luminance" "$val"
  sleep "$WAIT"

  hyprctl keyword "monitorv2[$DISPLAY]:sdr_max_luminance" 0
  sleep 1

  (( val += STEP ))
done

FINAL=203
echo "Final set: $DISPLAY sdr_brightness = $FINAL"
hyprctl keyword "monitorv2[$DISPLAY]:sdr_max_luminance" "$FINAL"
