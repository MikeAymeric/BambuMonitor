# BambuMonitor

A real-time print monitor for **BambuLab P2S** running on an **Arduino UNO Q**.  
Displays live print data on an SH1106 OLED display, shows animated status on the built-in LED matrix, and lets you cycle through 6 information screens using two physical buttons.

---

## Hardware

| Component | Details |
|---|---|
| **Board** | Arduino UNO Q (Qualcomm QRB2210 Linux + STMicroelectronics STM32U585) |
| **Display** | 128×64 OLED I2C — SH1106 |
| **LED Matrix** | 13×8 built-in (on-board, no wiring needed) |
| **Buttons** | 2× tactile push button |

### OLED Wiring (all pins at 3.3 V — no level shifter needed)

| OLED Pin | Arduino UNO Q |
|----------|---------------|
| VCC | 3.3V |
| GND | GND |
| SCK | SCL (A5 on JDIGITAL, or Qwiic SCL) |
| SDA | SDA (A4 on JDIGITAL, or Qwiic SDA) |

### Button Wiring

Connect each button between the pin and **GND** (internal pull-up is used).

| Pin | Action |
|-----|--------|
| D2 | Previous screen |
| D3 | Next screen |

---

## Architecture

```
BambuLab P2S  ←── MQTT/TLS ──→  Arduino UNO Q (Linux / QRB2210)
                                       │
                                  Python app
                                  ├─ MQTT client (paho-mqtt)
                                  ├─ Data parsing & elapsed time
                                  └─ Bridge RPC → STM32
                                       │  (arduino:zephyr Router Bridge)
                                       ▼
                                   STM32U585 (Arduino sketch)
                                   ├─ SH1106 OLED — 6 info screens
                                   ├─ LED matrix — state animations
                                   └─ Buttons — screen navigation
```

**Communication between Linux and STM32** uses the Arduino Router Bridge RPC system (bundled in `arduino:zephyr > 0.54.1`). Data is split into two small JSON payloads sent every 3 seconds via `Bridge.notify`.

---

## Screens

Navigate with **D2** (previous) and **D3** (next). A row of dots at the bottom indicates the current screen.

| # | Screen | Content |
|---|--------|---------|
| 1 | **Progress** | Completion bar, remaining time, elapsed time, layer count |
| 2 | **Temperatures** | Nozzle actual/target, bed actual/target, chamber |
| 3 | **Fans & Speed** | Print speed %, cooling fan % |
| 4 | **Print Info** | Job name, elapsed time, estimated total time |
| 5 | **Filament** | AMS slot, material type, brand, remaining % |
| 6 | **System** | Print state, nozzle diameter, print speed |

---

## LED Matrix Animations

| Printer State | Animation |
|---|---|
| `RUNNING` | Diagonal wave scrolling across matrix |
| `PAUSE` | Two vertical bars slow-blinking |
| `FAILED` | X pattern fast-blinking |
| `FINISH` | Checkmark (static) |
| `IDLE` | All off |

---

## Project Structure

```
BambuMonitor/
├── app.yaml                 ← Arduino App Lab entry points
├── python/
│   ├── main.py              ← App Lab entry point (Python)
│   ├── bambu_client.py      ← MQTT client for BambuLab P2S
│   ├── bridge_comm.py       ← Bridge RPC wrapper (Python → STM32)
│   ├── config.py            ← Printer connection settings
│   └── requirements.txt     ← pip dependencies
└── sketch/
    ├── sketch.ino           ← Arduino sketch (STM32U585)
    └── sketch.yaml          ← Board, platform, library declarations
```

---

## Setup

### 1. Configure the printer connection

Edit `python/config.py`:

```python
PRINTER_IP     = "192.168.1.100"   # Your P2S local IP
PRINTER_SERIAL = "XXXXXXXXXXXXXXX"  # 15-char serial — Settings → Network → ⓘ
ACCESS_CODE    = "12345678"         # 8-digit access code — same screen
```

### 2. Load the Arduino sketch

Install the following libraries via the **App Lab Library Manager**:

| Library | Notes |
|---------|-------|
| `Arduino_RPClite` | Bridge dependency |
| `ArxContainer` | Bridge dependency |
| `ArxTypeTraits` | Bridge dependency |
| `DebugLog` | Bridge dependency |
| `MsgPack` | Bridge dependency |
| `U8g2` | OLED driver |
| `ArduinoJson` | JSON parsing (v6) |

> `Arduino_RouterBridge` and `Arduino_LED_Matrix` are bundled with the `arduino:zephyr` platform — do **not** add them manually.

Open the project in **Arduino App Lab** and upload the sketch to the board.

### 3. Start the Python app

The Python script runs automatically on the Linux side when the App Lab app is started.  
To install dependencies manually on the board:

```bash
pip3 install -r python/requirements.txt
```

---

## BambuLab MQTT Notes

- **Protocol:** MQTT over TLS, port **8883**
- **Credentials:** username `bblp`, password = access code
- **Certificate:** self-signed (verification disabled)
- **Subscribe:** `device/{serial}/report`
- **Fan scale:** reported as 0–15, converted to 0–100 %

---

## Known Limitations

- Fan speed control via MQTT is **not supported** — BambuLab P2S rejects local GCode commands without LAN Mode enabled in the printer settings.
- Chamber temperature sensor is present on P2S and reported via `info.temp` in the MQTT payload (not `chamber_temper`).
- Elapsed time is **estimated** from completion percentage and remaining time, not tracked from a real start timestamp.

---

## Tech Stack

| Side | Language | Key Libraries |
|------|----------|---------------|
| Linux (QRB2210) | Python 3 | paho-mqtt 1.6.1, arduino.app_utils (Bridge) |
| MCU (STM32U585) | C++ / Arduino on Zephyr | U8g2, ArduinoJson 6, Arduino_RouterBridge |

---

## License

MIT — see [LICENSE](LICENSE)
