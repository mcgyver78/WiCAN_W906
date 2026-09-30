#!/bin/sh
# Build the firmware for the self-built ESP32-WROOM board (see DIY_ESP32.md).
# The ESP32-C3 sdkconfig stays the single source of settings: its target
# specific entries are stripped, sdkconfig.diy_esp32.defaults goes on top,
# and the result lands in build_diy/ without touching ./sdkconfig.
set -e
cd "$(dirname "$0")/../.."
B=build_diy
mkdir -p "$B"

sed '/^# Deprecated options for backward compatibility/,$d' sdkconfig \
  | grep -vE '^(# )?CONFIG_(IDF_TARGET|IDF_FIRMWARE_CHIP_ID|SOC_|ESP32C3|ESP_REV_|ESP_CONSOLE|ESP_SYSTEM_SINGLE_CORE_MODE|ESP_SYSTEM_MEMPROT|FREERTOS_UNICORE|FREERTOS_NUMBER_OF_CORES|BT_CTRL_|ESP_ROM_|ESP_TASK_WDT_CHECK_IDLE_TASK_CPU|PARTITION_TABLE_)' \
  > "$B/base.defaults"

# Regenerate from the defaults on every build, otherwise stale values stick
rm -f "$B/sdkconfig"
idf.py -B "$B" -DIDF_TARGET=esp32 -DWICAN_HW=DIY_ESP32 \
  -DSDKCONFIG="$PWD/$B/sdkconfig" \
  -DSDKCONFIG_DEFAULTS="$PWD/$B/base.defaults;$PWD/sdkconfig.diy_esp32.defaults" \
  build

# The settings the port depends on must have survived the merge
fail=0
check() { grep -qx "$1" "$B/sdkconfig" || { echo "sdkconfig check failed: $1" >&2; fail=1; }; }
check 'CONFIG_IDF_TARGET="esp32"'
check 'CONFIG_BTDM_CTRL_MODE_BLE_ONLY=y'
check 'CONFIG_ESP_CONSOLE_UART_DEFAULT=y'
check '# CONFIG_FREERTOS_UNICORE is not set'
check 'CONFIG_PARTITION_TABLE_CUSTOM_FILENAME="wican_partitions_diy_esp32.csv"'
check 'CONFIG_BT_BLUEDROID_ENABLED=y'
check 'CONFIG_ESPTOOLPY_FLASHSIZE="4MB"'
[ "$fail" = 0 ] || exit 1
ls -l "$B"/wican-fw_obd_diy_*.bin
