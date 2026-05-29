# BambuLab P2S connection settings
# Find these on the printer: Settings → Network → (info icon)
PRINTER_IP      = "192.168.1.100"   # change to your printer's IP
PRINTER_SERIAL  = "XXXXXXXXXXXXXXX"  # 15-char serial number
ACCESS_CODE     = "12345678"         # 8-digit access code

MQTT_PORT       = 8883
MQTT_USERNAME   = "bblp"

# How often (seconds) to push data to STM32 even without new MQTT messages
HEARTBEAT_INTERVAL = 2.0
