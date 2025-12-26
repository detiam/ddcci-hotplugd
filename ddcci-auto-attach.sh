#!/usr/bin/env bash
set -euo pipefail

attached() {
  [[ -d "/sys/bus/ddcci/devices/ddcci$1" ]]
}

if [[ ! -d /sys/module/ddcci_backlight ]]; then
  echo 'Probing ddcci_backlight'
  modprobe ddcci delay=0
  modprobe ddcci_backlight
else
  for path in /sys/class/backlight/ddcci*; do
    if [[ ! -d $path ]]; then
      echo 'Warning: invaild ddcci_backlight interface detected, re-modprobe!'
      modprobe -r ddcci_backlight ddcci
      modprobe ddcci delay=0
      modprobe ddcci_backlight
    fi
  done
fi

# Clean up invaild interfaces.
for path in /sys/bus/i2c/devices/i2c-*/*-0037; do
  if [[ $(cat "$path/name") == "ddcci" ]] && [[ ! -d $path/driver ]]; then
    echo "Detaching '$path'"
    echo '0x37' > "$path/../delete_device"
  fi
done

ddcutil detect --skip-ddc-checks --disable-dynamic-sleep --brief \
| awk -F'/dev/i2c-' '/I2C bus:/ {print $2}' |
  while read -r bus_num; do
    if attached "$bus_num"; then
      echo "Already attached on i2c-$bus_num"
      continue
    fi
    echo "Attaching i2c-$bus_num"
    echo 'ddcci 0x37' > "/sys/bus/i2c/devices/i2c-$bus_num/new_device"
  done
