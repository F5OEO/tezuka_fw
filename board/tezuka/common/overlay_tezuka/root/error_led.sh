#!/bin/sh
# led_status.sh — LED error/status code manager for Zynq
# Usage:
#   led_status ok              → heartbeat (system healthy)
#   led_status warn            → slow blink (warning)
#   led_status error <code>    → N flashes + pause (error code 1-9)
#   led_status critical        → solid on (fatal)
#   led_status off             → LED off
#   led_status list            → show error code table
#   led_status stop            → stop any running blink pattern, restore heartbeat
#
# Error code table (customize to your needs):
#   1 = Memory error
#   2 = Network error
#   3 = Filesystem error
#   4 = Peripheral / I2C / SPI error
#   5 = Configuration error
#   6 = Sensor error
#   7 = Communication timeout
#   8 = Overtemperature
#   9 = Application crash

# --- Configuration ---
LED_PATH="/sys/class/leds/led0:green"
PIDFILE="/tmp/led_status.pid"
FLASH_ON_MS=150000   # usleep microseconds (150ms)
FLASH_OFF_MS=150000
PAUSE_MS=1000000     # pause between series (1s)

# --- Error code descriptions ---
error_desc() {
    case "$1" in
        1) echo "Memory error" ;;
        2) echo "Network error" ;;
        3) echo "Filesystem error" ;;
        4) echo "Peripheral / I2C / SPI error" ;;
        5) echo "Configuration error" ;;
        6) echo "Sensor error" ;;
        7) echo "Communication timeout" ;;
        8) echo "Overtemperature" ;;
        9) echo "Application crash" ;;
        *) echo "Unknown" ;;
    esac
}

# --- Helpers ---
led_write() {
    echo "$2" > "${LED_PATH}/$1" 2>/dev/null
}

stop_blink() {
    if [ -f "$PIDFILE" ]; then
        pid=$(cat "$PIDFILE")
        if kill -0 "$pid" 2>/dev/null; then
            kill "$pid" 2>/dev/null
            wait "$pid" 2>/dev/null
        fi
        rm -f "$PIDFILE"
    fi
}

check_led() {
    if [ ! -d "$LED_PATH" ]; then
        echo "Error: LED not found at ${LED_PATH}" >&2
        exit 1
    fi
}

# --- Blink pattern: N flashes + pause, loop forever ---
blink_pattern() {
    code=$1
    led_write trigger none
    while true; do
        i=0
        while [ "$i" -lt "$code" ]; do
            led_write brightness 1
            usleep "$FLASH_ON_MS"
            led_write brightness 0
            usleep "$FLASH_OFF_MS"
            i=$((i + 1))
        done
        usleep "$PAUSE_MS"
    done
}

# --- Main ---
check_led

case "$1" in
    ok)
        stop_blink
        led_write trigger heartbeat
        echo "LED: heartbeat (system OK)"
        ;;

    warn)
        stop_blink
        led_write trigger timer
        led_write delay_on 500
        led_write delay_off 500
        echo "LED: slow blink (warning)"
        ;;

    error)
        code="${2:-1}"
        if [ "$code" -lt 1 ] || [ "$code" -gt 9 ] 2>/dev/null; then
            echo "Error: code must be 1-9" >&2
            exit 1
        fi
        stop_blink
        desc=$(error_desc "$code")
        blink_pattern "$code" &
        echo $! > "$PIDFILE"
        echo "LED: ${code} flashes (${desc})"
        ;;

    critical)
        stop_blink
        led_write trigger default-on
        echo "LED: solid ON (critical error)"
        ;;

    off)
        stop_blink
        led_write trigger none
        led_write brightness 0
        echo "LED: off"
        ;;

    stop)
        stop_blink
        led_write trigger heartbeat
        echo "LED: restored heartbeat"
        ;;

    list)
        echo "LED error codes:"
        echo "  Code  Description"
        echo "  ----  -----------"
        i=1
        while [ "$i" -le 9 ]; do
            desc=$(error_desc "$i")
            printf "  %d     %s\n" "$i" "$desc"
            i=$((i + 1))
        done
        echo ""
        echo "States:"
        echo "  ok        heartbeat (system healthy)"
        echo "  warn      slow blink (500ms)"
        echo "  error N   N flashes + pause"
        echo "  critical  solid on"
        echo "  off       LED off"
        echo "  stop      stop pattern, restore heartbeat"
        ;;

    *)
        echo "Usage: $(basename "$0") {ok|warn|error <1-9>|critical|off|stop|list}" >&2
        exit 1
        ;;
esac

exit 0