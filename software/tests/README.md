# Test locations

The two categories test the same driver at different levels. Their programs,
instructions and build directories are separate.

| Directory | Needs hardware? | What it establishes |
|---|---|---|
| [software/](software/README.md) | No | Driver behavior with simulated transports/chips in QEMU. |
| [hardware/](hardware/README.md) | Yes | Actual board behavior, observed through the serial terminal and, for RF, HackRF. |

Radio drivers live in `../src/radio/`, CAN diagnostic cores in `../src/can/`,
and public headers in `../include/ocs/`. Software tests substitute the physical
adapter. Hardware programs link the real Zephyr SPI/GPIO adapter.

Run Twister against `software/tests/software`, so its test discovery is
explicitly restricted to the simulated suites. Hardware programs are standalone
Zephyr applications, built/flashed separately; they are not part of that run.

The software suites contain 67 cases. See each hardware application's README
for its separate build, flash, wiring and expected-output instructions.

The user has reported observing RF transmissions on an SDR and supplied a
passing read-only CAN SPI/clock diagnostic log. The new internal CAN loopback
application has not yet been tested on the physical board. Simulated test and
SAMD21 build results do not establish physical CANH/CANL communication.
