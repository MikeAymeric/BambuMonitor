"""
MQTT client for BambuLab P2S.
Subscribes to printer reports and exposes parsed PrintState.
"""

import ssl
import json
import threading
import logging
import itertools
from dataclasses import dataclass, field
from typing import Callable
import paho.mqtt.client as mqtt

import config

log = logging.getLogger(__name__)


@dataclass
class PrintState:
    gcode_state: str = "IDLE"
    mc_percent: int = 0
    mc_remaining_time: int = 0      # minutes
    mc_elapsed_time: int = 0        # minutes (estimated)
    layer_num: int = 0
    total_layer_num: int = 0
    nozzle_temper: float = 0.0
    nozzle_target_temper: float = 0.0
    bed_temper: float = 0.0
    bed_target_temper: float = 0.0
    chamber_temper: float = 0.0
    cooling_fan_speed: int = 0
    big_fan1_speed: int = 0
    big_fan2_speed: int = 0
    spd_mag: int = 100
    subtask_name: str = ""
    wifi_signal: int = 0            # dBm
    nozzle_diameter: str = "0.4"   # mm as string
    nozzle_type: str = "--"         # e.g. "HS01"
    heatbreak_fan: int = 0          # heatbreak fan %
    ams_slot: str = "--"            # e.g. "1A"
    ams_type: str = "--"            # e.g. "PLA"
    ams_brand: str = "--"           # e.g. "PLA Basic"
    ams_remain: int = 0             # active tray remaining %
    ams_humidity: int = 0           # AMS humidity level (1-5)
    ams_temp: float = 0.0           # AMS box temperature °C


def _scale_fan(raw: int) -> int:
    return round(raw / 15 * 100) if raw > 0 else 0


def _estimate_elapsed(percent: int, remaining: int) -> int:
    """Estimate elapsed minutes from completion % and remaining time."""
    if percent <= 0 or percent >= 100:
        return 0
    total = remaining / (1.0 - percent / 100.0)
    return max(0, int(total - remaining))


class BambuClient:
    def __init__(self, on_state_update: Callable[[PrintState], None]):
        self._state = PrintState()
        self._on_update = on_state_update
        self._lock = threading.Lock()
        self._seq = itertools.count(1)

        self._topic_report  = f"device/{config.PRINTER_SERIAL}/report"
        self._topic_request = f"device/{config.PRINTER_SERIAL}/request"

        self._client = mqtt.Client(client_id="bambu-monitor", protocol=mqtt.MQTTv311)
        self._client.username_pw_set(config.MQTT_USERNAME, config.ACCESS_CODE)

        tls_ctx = ssl.create_default_context()
        tls_ctx.check_hostname = False
        tls_ctx.verify_mode = ssl.CERT_NONE
        self._client.tls_set_context(tls_ctx)

        self._client.on_connect    = self._on_connect
        self._client.on_disconnect = self._on_disconnect
        self._client.on_message    = self._on_message

    def connect(self):
        self._client.connect_async(config.PRINTER_IP, config.MQTT_PORT)
        self._client.loop_start()

    def disconnect(self):
        self._client.loop_stop()
        self._client.disconnect()

    @property
    def state(self) -> PrintState:
        with self._lock:
            return self._state

    def _on_connect(self, client, userdata, flags, rc):
        if rc == 0:
            log.info("Connected to printer MQTT broker")
            client.subscribe(self._topic_report)
        else:
            log.error("MQTT connect failed rc=%d", rc)

    def _on_disconnect(self, client, userdata, rc):
        log.warning("MQTT disconnected rc=%d", rc)

    def _on_message(self, client, userdata, msg):
        try:
            data = json.loads(msg.payload)
        except json.JSONDecodeError:
            return

        print_data = data.get("print", {})
        if not print_data:
            return

        with self._lock:
            s = self._state

            s.gcode_state          = print_data.get("gcode_state",          s.gcode_state)
            s.mc_percent           = print_data.get("mc_percent",           s.mc_percent)
            s.mc_remaining_time    = print_data.get("mc_remaining_time",    s.mc_remaining_time)
            s.layer_num            = print_data.get("layer_num",            s.layer_num)
            s.total_layer_num      = print_data.get("total_layer_num",      s.total_layer_num)
            s.nozzle_temper        = print_data.get("nozzle_temper",        s.nozzle_temper)
            s.nozzle_target_temper = print_data.get("nozzle_target_temper", s.nozzle_target_temper)
            s.bed_temper           = print_data.get("bed_temper",           s.bed_temper)
            s.bed_target_temper    = print_data.get("bed_target_temper",    s.bed_target_temper)
            # Chamber temp is in info.temp, not chamber_temper
            info_data = print_data.get("info", {})
            if isinstance(info_data, dict) and "temp" in info_data:
                s.chamber_temper = float(info_data["temp"])

            nt = print_data.get("nozzle_type")
            if nt is not None:
                s.nozzle_type = str(nt)

            hbf = print_data.get("heatbreak_fan_speed")
            if hbf is not None:
                s.heatbreak_fan = _scale_fan(int(hbf))
            s.spd_mag              = print_data.get("spd_mag",              s.spd_mag)
            s.subtask_name         = print_data.get("subtask_name",         s.subtask_name)
            ws_raw = print_data.get("wifi_signal")
            if ws_raw is not None:
                try:
                    s.wifi_signal = int(str(ws_raw).replace("dBm", "").strip())
                except ValueError:
                    pass

            nd = print_data.get("nozzle_diameter")
            if nd is not None:
                s.nozzle_diameter = str(nd)

            raw_cf = print_data.get("cooling_fan_speed")
            raw_f1 = print_data.get("big_fan1_speed")
            raw_f2 = print_data.get("big_fan2_speed")
            if raw_cf is not None: s.cooling_fan_speed = _scale_fan(int(raw_cf))
            if raw_f1  is not None: s.big_fan1_speed   = _scale_fan(int(raw_f1))
            if raw_f2  is not None: s.big_fan2_speed   = _scale_fan(int(raw_f2))

            # Elapsed time estimate
            s.mc_elapsed_time = _estimate_elapsed(s.mc_percent, s.mc_remaining_time)

            # AMS active tray
            ams_data = print_data.get("ams")
            if ams_data:
                tray_now = str(ams_data.get("tray_now", "255"))
                ams_list = ams_data.get("ams", [])
                for ams_unit in ams_list:
                    for tray in ams_unit.get("tray", []):
                        global_id = str(int(ams_unit.get("id", 0)) * 4 + int(tray.get("id", 0)))
                        if global_id == tray_now:
                            ftype = (tray.get("tray_type") or tray.get("type") or "--")
                            uid = int(ams_unit.get("id", 0)) + 1
                            tid = chr(ord("A") + int(tray.get("id", 0)))
                            s.ams_slot   = f"{uid}{tid}"
                            s.ams_type   = ftype or "--"
                            s.ams_brand  = tray.get("tray_sub_brands", "--") or "--"
                            s.ams_remain = int(tray.get("remain", 0))
                            # AMS box humidity and temp (from the ams unit, not the tray)
                            try:
                                s.ams_humidity = int(ams_unit.get("humidity", 0))
                                s.ams_temp = float(ams_unit.get("temp", 0))
                            except (ValueError, TypeError):
                                pass

        self._on_update(self._state)
