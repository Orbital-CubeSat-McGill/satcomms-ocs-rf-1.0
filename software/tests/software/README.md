# Software tests: no boards needed

These automated tests run in QEMU on the Mac. QEMU supplies a simulated MCU;
the tests provide fake transports/chip memory and events. They cannot establish
that the physical RF board transmitted or received anything.

| Suite | Cases | Coverage |
|---|---|---|
| [radio_core/](radio_core/) | 10 | SPI command encoding, address/size validation and transport errors. |
| [radio_api/](radio_api/) | 9 | Public register/buffer API, initialization, failed outputs, ISR rejection and mutex serialization. |
| [radio_packet/](radio_packet/) | 25 | Packet profile/state sequencing, up to 2043-byte payloads, SPI chunking/failures, TX buffer/FCS handling, CRC/length checks, deadlines and recovery. |

Each suite has `src/main.c` containing its test cases, `testcase.yaml` for
Twister discovery, `prj.conf` for Zephyr options and `CMakeLists.txt` for its build.

Run all 44 cases:

```sh
source /Users/muhammadaliullah/zephyrproject/.venv/bin/activate
export ZEPHYR_BASE=/Users/muhammadaliullah/zephyrproject/zephyr
export OCS_RF_REPO=/Users/muhammadaliullah/Documents/satcomms-ocs-rf-1.0
cd "$ZEPHYR_BASE"
west twister -T "$OCS_RF_REPO/software/tests/software" -p qemu_cortex_m3 --inline-logs --outdir /tmp/ocs-rf-software-tests-separated
```

For one suite, append its directory name to the `-T` path. No flash or serial
monitor command is needed. Real board checks are in [../hardware/](../hardware/README.md).
