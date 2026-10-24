# AT86RF215 packet radio implementation plan

> Historical execution record: the user subsequently requested separate software
> and hardware test folders. Current paths and commands are in
> `software/tests/README.md`; paths in the original steps below predate that move.

> **For agentic workers:** Execute inline with superpowers:executing-plans. Keep every change uncommitted; the user's explicit prohibition overrides all commit steps in skills.

**Goal:** Send and receive real 915 MHz packets using the SAMD21-controlled green board and a HackRF One/Mayhem Capture/Replay bench procedure.

**Architecture:** Keep the existing wired SPI API. A private packet engine owns the PHY register sequence and IRQ/state handling; the public facade holds its mutex across each whole packet operation. A separate button-operated sample exercises the radio without changing the default SPI diagnostic application.

**Tech stack:** Existing Zephyr 4.4.99 checkout, SAMD21G17D hardware with the existing G17A target, AT86RF215 PN34/VN03, Ztest/Twister/QEMU, HackRF One and Mayhem.

**Spec:** User approved the proposed 915 MHz bench configuration in chat; the detailed contract is below. Mayhem's exact version is unknown, so documentation must not promise identical menu labels.

## Contract and global constraints

- No commits, staging, pushes, merges or external messages.
- First RF profile: 915.000 MHz, 2-FSK, 50 ksymbol/s (50 kb/s without FEC), modulation index 1, BT=0.5, 16-octet preamble, uncoded IEEE mode, whitening disabled, hardware CRC-32.
- Profile uses the actual VN03 silicon's TXDFE.SR=8, RXDFE.SR=10 (datasheet table 6-51); reject unsupported silicon versions for RF configuration.
- Maximum application payload 32 bytes. Set TX length first (resets ADDRSPI), then write payload plus four placeholder CRC bytes required by section 6.13.4. Hardware generates the real CRC; callers supply only payload. RX returns payload only, after hardware CRC validation and FCS-type checking.
- Configuration accepts frequency 863..928 MHz on a 25 kHz grid and power level 0..31; sample fixes 915 MHz and minimum power level 0. These are hardware checks, not a regulatory policy.
- Public additions: `struct ocs_radio_config { uint32_t frequency_hz; uint8_t tx_power_level; };`, `ocs_radio_configure(const struct ocs_radio_config *)`, `ocs_radio_send(const void *, size_t, int32_t timeout_ms)`, `ocs_radio_receive(void *, size_t capacity, size_t *length, int32_t timeout_ms)`.
- Positive timeouts only. They bound mutex/event/state waits; underlying synchronous SPI calls and bounded cleanup may add time. All functions remain thread-only.
- Initialize first, configure second. Initialization invalidates packet configuration. Failed cleanup or radio/transport faults require reconfiguration. Normal receive timeout recovers to TRXOFF.
- Every operation drains stale interrupt reasons before starting. Check radio errors, TRXRDY/PLL lock, TXFE/RXFE, TX under-run and CRC. IRQ callback remains semaphore-only; reading status clears it.
- On failure receive must preserve caller data and length. On success copy from a temporary payload after successful cleanup.
- Keep the default bring-up app non-transmitting. Separate sample uses SW0 to send one Hello world packet on a fresh button press; otherwise it receives with short bounded waits.
- Physical RF success requires fresh board and HackRF evidence. Software models cannot establish that a signal was transmitted.

## Review focus

1. Stale TXFE must not make a new transmit succeed.
2. Missing ready/frame interrupts must time out and attempt TRXOFF cleanup.
3. Bad CRC, wrong FCS type, impossible lengths and undersized caller buffers must not expose partial data.
4. Cleanup failure must invalidate configuration; initialization must invalidate old configuration too.
5. Deadlines include contention; ISR calls must never acquire a mutex or access hardware.

## Task 1: Packet engine and public integration

Files: extend `software/include/ocs/radio.h`, `software/src/radio/ocs_radio.c`, `software/src/radio/at86rf215_regs.h`; add private `at86rf215_packet.h/.c`; make IRQ setup repeatable in `at86rf215_zephyr.c`; link engine in existing application and API tests.

- [x] Write `software/tests/radio_packet/` with a chip/event model replacing only the physical adapter. Keep the real facade, mutex, SPI protocol and packet engine.
- [x] Observe failure with the packet engine unimplemented.
- [x] Configure from reset using datasheet sections 6.3, 6.9, 6.10, 6.13 and 7.2. Confirm PLL readiness without issuing TX.
- [x] Implement send: write total length first, then payload and four FCS placeholders; TXPREP; TRXRDY/PLL; TX; TXFE/under-run check; TRXOFF cleanup.
- [x] Implement receive: RX; RXFE; CRC/type/length/capacity checks; payload read; TRXOFF; publish output.
- [x] Cover invalid configuration, wrong version, use before configure, stale flags, timeouts, transport errors, cleanup invalidation, CRC/type/length/capacity errors and preservation of failed outputs.
- [x] Run all suites using `west twister -T "$OCS_RF_REPO/software/tests" -p qemu_cortex_m3 --inline-logs --outdir /tmp/ocs-rf-packet-tests`.

## Task 2: Hardware sample and user guide

Files: add `software/samples/radio_packet/` CMake/config/main and `software/docs/radio-packets.md`; update software README.

- [x] Build the sample with `west build -p always -b ocs_rf_1/samd21g17a "$OCS_RF_REPO/software/samples/radio_packet" -d /tmp/ocs-rf-packets`.
- [x] Check the original `/tmp/ocs-rf-mounted` app still builds.
- [x] Document TX/RX APIs, byte counts, state/IRQ behavior, timeout and recovery semantics, sample controls and meaningful pass/fail evidence.
- [x] Document Mayhem Capture of green-board packets, then replay while the green board receives. Raw I/Q capture is waveform recording, not payload decoding; board-side CRC-valid reception is the proposed verification.
- [x] Include antenna/attenuation requirements before any RF cable connection or transmission instructions. Exact output power needs measurement; power level is not dBm.

## Task 3: Review and physical verification

- [x] Request one fresh read-only review of code/tests/documents; resolve material issues and rerun affected checks.
- [ ] Physical verification pending: user says hardware is unavailable. No flash, PLL measurement or operator-assisted HackRF test has been performed.
- [x] Otherwise report physical RF verification pending, with exact commands and prerequisites.
- [x] Confirm HEAD remains `b3ef7b8045da4c05c2321d2c6c97bc945f22302d` and the index remains empty.

## Execution ledger

- Plan recorded after user specified HackRF One/PortaPack, Mayhem and 915 MHz.
- Prior SPI facade: 19/19 QEMU tests and SAMD21 build passed; its physical run remains pending since USB was disconnected.
- Task 1: complete. Initial unimplemented engine failed 18/19 cases; subsequent profile/timeout/raw-write regression tests failed as expected, then passed after fixes. Full suite 40/40 passed in `/tmp/ocs-rf-packet-tests-final`.
- Ruling: any raw SPI write attempt invalidates packet configuration, including an invalid write request. Arbitrary writes can change PHY/state or partly reach hardware; callers must configure again before packet operations. Cost: an extra configuration step after raw-write diagnostics.
- Task 2 builds passed: packet sample 19080 B flash / 5152 B RAM; default diagnostic 17876 B flash / 5152 B RAM. Documentation records hardware as unverified.
- Hardware unavailable per user reply. No new firmware was flashed and no RF transmission/reception was attempted.

- Final independent review: one material TX underrun finding. Datasheet section 6.13.4 resets ADDRSPI on TXFLL and requires FCS placeholder bytes. Added modeled address tracking; existing send, maximum-payload and post-timeout recovery tests failed (18/21 passed) before the fix in `/tmp/ocs-rf-packet-review-red.log`.
- Final: fixed TX length/buffer ordering and missing FCS placeholders — `test_send_writes_payload_and_crc_inclusive_length`, `test_maximum_binary_payload`, and `test_receive_timeout_preserves_output_and_recovers` RED to GREEN. Complete suite 40/40 passed with no warnings in `/tmp/ocs-rf-packet-review-green.log`. Both SAMD21 builds passed after the fix; packet 19080 B flash / 5152 B RAM, default 17876 B flash / 5152 B RAM.
- Documentation source-count correction included while documenting the required packet-engine source: four driver sources. No deferred review findings remain.
- Final build inspection: mounted pin mapping retained (CS PA18, reset PA14, IRQ PA20, DIPO0/DOPO2, 1 MHz); packet ELF links the real Zephyr adapter, not a chip model. HEAD remains b3ef7b8045da4c05c2321d2c6c97bc945f22302d; index empty.
