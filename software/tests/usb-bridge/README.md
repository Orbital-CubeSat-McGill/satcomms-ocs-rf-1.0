# usb-bridge (Green Bear)

One-way USB → radio bridge for the HAB launch path.

Protocol (byte layout, COBS, CRC-16):  
`~/Documents/GitHub/MPM-Ion0-software/docs/greenbear-protocol.md`

## Build / flash

From `software/` (west workspace):

```bash
west build -b ocs_rf_1 -d build-usb-bridge tests/usb-bridge --pristine=always
west flash -d build-usb-bridge
```

Console: SERCOM5 @ 115200. Look for `setup PASS`, then `sent seq=…` per good frame.

## Host COBS test (no Zephyr)

```bash
make -C tests/usb-bridge/host test
```

## Ground decode

```bash
python3 ground-station/fsk_decode.py capture.iq --expect greenbear
```
