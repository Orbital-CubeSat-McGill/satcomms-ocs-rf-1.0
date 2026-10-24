# Test locations

The two categories test the same driver at different levels. Their programs,
instructions and build directories are separate.

| Directory | Needs hardware? | What it establishes |
|---|---|---|
| [software/](software/README.md) | No | Driver behavior with simulated transports/chips in QEMU. |
| [hardware/](hardware/README.md) | Yes | Actual board behavior, observed through the serial terminal and, for RF, HackRF. |

Shared production driver code lives in `../src/radio/` and public headers in
`../include/ocs/`. Software tests substitute the physical adapter. Hardware
programs link the real Zephyr SPI/GPIO adapter.

Run Twister against `software/tests/software`, so its test discovery is
explicitly restricted to the simulated suites. Hardware programs are standalone
Zephyr applications, built/flashed separately; they are not part of that run.

No new RF hardware results have been recorded: hardware is currently unavailable.

After this reorganization, all 40 software cases passed in QEMU and both
hardware applications compiled for `ocs_rf_1/samd21g17a`. The compatibility
build from `software/` also compiled. These build results do not constitute
physical board-test results.
