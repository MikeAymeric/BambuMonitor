"""
BambuMonitor — App Lab entry point.
Monitor-only: receives data from BambuLab P2S and pushes it to the STM32 sketch.
Edit python/config.py with your printer IP, serial number and access code.
"""

import logging
import signal
import time
import threading

from bambu_client import BambuClient, PrintState
from bridge_comm import BridgeComm
import config

logging.basicConfig(
    level=logging.INFO,
    format="%(asctime)s [%(levelname)s] %(name)s: %(message)s",
)
log = logging.getLogger("main")


def on_state_update(state: PrintState):
    bridge.send_state(state)


def heartbeat_loop():
    while True:
        time.sleep(config.HEARTBEAT_INTERVAL)
        bridge.send_state(bambu.state)


bridge = BridgeComm()
bambu  = BambuClient(on_state_update=on_state_update)

bridge.connect()
bambu.connect()

threading.Thread(target=heartbeat_loop, daemon=True).start()
log.info("BambuMonitor running.")


def _shutdown(sig, frame):
    bambu.disconnect()
    raise SystemExit(0)

signal.signal(signal.SIGINT,  _shutdown)
signal.signal(signal.SIGTERM, _shutdown)
signal.pause()
