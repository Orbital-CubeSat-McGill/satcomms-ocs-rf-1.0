# usb-bridge (Green Bear)

One-way serial → radio bridge for the HAB launch path. Frames arrive on the
RB's SERCOM5 console UART through the Curiosity Nano's debugger (nEDBG) CDC
port; the target MCU's own USB pins (PA24/PA25) are not wired to the jack.

Protocol (byte layout, COBS, CRC-16):  
`~/Documents/GitHub/MPM-Ion0-software/docs/greenbear-protocol.md`

## Build / flash

From `software/` (west workspace). **Use the project venv** — Homebrew
`west` alone fails CMake with `Missing jsonschema dependency`.

```bash
cd ~/Documents/GitHub/satcomms-ocs-rf-1.0/software
./.venv/bin/west build -b ocs_rf_1 -d build-usb-bridge tests/usb-bridge --pristine=always
./.venv/bin/west flash -d build-usb-bridge
```

Equivalent: `source .venv/bin/activate` then bare `west …`.

## Console / input port

The RB console **is** the input: both ride SERCOM5 (115200 8N1) through the
Nano's virtual COM port.

- On the BBG it is `/dev/ttyACM0`. Green Bear writes frames to it and logs
  every line the RB prints back as `rb: …` in `greenbear-stdout.log`.
- On a laptop: `picocom -b 115200 /dev/tty.usbmodem*`.

Look for `setup PASS`, then `sent seq=…` per good frame, `drop …` otherwise.

Only one process may hold the port. Close picocom before starting Green
Bear (and vice versa), or frames and console lines will be split between
readers.

## Host COBS test (no Zephyr)

```bash
make -C tests/usb-bridge/host test
```

## Ground decode

Live, GUI + terminal together (from the repo root, in `nix develop .#ground`):

```bash
cd ground-station/GNUradio && python3 fsk_demod.py
```

Spectrum/demod plots open in a window; every frame prints in the terminal,
followed by `greenbear callsign=… seq=… time=… len=… crc16=ok` and
`GAP missed=N` when seq jumps. The embedded block defaults to
`expect='greenbear'`; switch it in GRC for tx-burst bit-error checks.

Offline, from a `hackrf_transfer -r capture.iq …` recording:

```bash
python3 ground-station/fsk_decode.py capture.iq --expect greenbear
```
