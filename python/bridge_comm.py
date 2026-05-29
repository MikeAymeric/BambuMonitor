"""
Bridge communication layer — sends display data to STM32 sketch.
One-directional: Python -> MCU only (no button callbacks needed).
"""

import json
import logging
import time
from arduino.app_utils import Bridge

log = logging.getLogger(__name__)

_MIN_INTERVAL = 3.0   # seconds between Bridge updates
_last_sent = 0.0


class BridgeComm:
    def connect(self):
        log.info("Bridge ready")

    def disconnect(self):
        pass

    def send_state(self, state) -> bool:
        global _last_sent
        now = time.monotonic()
        if now - _last_sent < _MIN_INTERVAL:
            return True
        _last_sent = now

        # p1: core print data (~145 bytes, 13 fields)
        p1 = json.dumps({
            "pct": state.mc_percent,
            "rem": state.mc_remaining_time,
            "ln":  state.layer_num,
            "tln": state.total_layer_num,
            "nt":  int(state.nozzle_temper),
            "ntt": int(state.nozzle_target_temper),
            "bt":  int(state.bed_temper),
            "btt": int(state.bed_target_temper),
            "ct":  int(state.chamber_temper),
            "cf":  state.cooling_fan_speed,
            "spd": state.spd_mag,
            "st":  state.gcode_state[:8],
            "nm":  state.subtask_name[:10],
        }, separators=(",", ":"))

        # p2: filament + nozzle (~75 bytes, 5 fields)
        p2 = json.dumps({
            "nd": state.nozzle_diameter[:3],
            "at": state.ams_type[:5],
            "as": state.ams_slot[:3],
            "ab": state.ams_brand[:8],
            "ar": state.ams_remain,
        }, separators=(",", ":"))

        try:
            Bridge.notify("upd1", p1)
            Bridge.notify("upd2", p2)
            return True
        except Exception as e:
            log.error("Bridge notify error: %s", e)
            return False
