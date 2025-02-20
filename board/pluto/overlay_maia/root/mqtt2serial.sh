#!/bin/bash

# =====================
# CONFIGURATION
# =====================
SERIAL_PORT="/dev/ttyACM0" # Adjust if needed
BAUDRATE="115200"
CHECK_INTERVAL=15          # Seconds between port checks

# =====================
# MQTT PUBLISH
# =====================
mqtt_publish () {
  /usr/bin/mosquitto_pub -t "$1" -m "$2"
}

# =====================
# OPEN SERIAL PORT
# =====================
open_serial_port() {
  stty -F ${SERIAL_PORT} ${BAUDRATE} cs8 -cstopb -parenb -ixon 2>/dev/null
}

# =====================
# LOG MESSAGES
# =====================
log_mqtt_message() {
  echo "$(date): $1" 
}

# =====================
# MQTT -> SERIAL
# =====================
mqtt_to_serial() {
  /usr/bin/mosquitto_sub -v -i "tezuka_sub" -t "#" | while read line; do
    # Split the line into key and value by the first space
    key=$(echo "$line" | awk '{print $1}')
    value=$(echo "$line" | sed 's/^[^ ]* //')

    if [[ -n "$key" && -n "$value" ]]; then
      # Log received key-value pairs
      log_mqtt_message "Key: $key, Value: $value"

      # Send to serial port (optional)
      if [ -e "${SERIAL_PORT}" ]; then
        echo "$key $value" > ${SERIAL_PORT}
      fi
    else
      log_mqtt_message "Invalid message received: $line"
    fi
  done
}

# =====================
# SERIAL -> MQTT
# =====================
serial_to_mqtt() {
  while true; do
    if [ -e "${SERIAL_PORT}" ]; then
      open_serial_port

      # Read serial data and publish to MQTT
      cat ${SERIAL_PORT} | while read line; do
        if [[ -n "$line" ]]; then
          topic="serial/data"  # Adjust topic if needed
		  log_mqtt_message "Key: $topic, Value: $line"
		  # todo if we like to control via serial port!!!!
          # mqtt_publish "$topic" "$line"
        fi
      done
    fi
    sleep ${CHECK_INTERVAL}
  done
}

# =====================
# START PROCESSES
# =====================
mqtt_to_serial &     # Start MQTT -> Serial
serial_to_mqtt &     # Start Serial -> MQTT

wait
