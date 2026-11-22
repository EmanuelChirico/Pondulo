# Validation record

Repository preparation: 28 October 2026. Native tests were compiled with the
installed host compiler and run using `python3 scripts/test_host.py`.

| Executable | What it checks | Result |
|---|---|---|
| cursor_glide | Adaptive alpha, symmetry, convergence, snap and bounds | PASS |
| gesture_link | Legacy binary frame encode/decode | PASS |
| xy_link_gesture | ASCII gestures, event mapping, malformed input, protocol separation | PASS |
| dwell_switch | Hover border restoration, gesture/hover conflict, carousel focus | PASS |
| audio_cues | Presence, scale entry, quantity keypad and receipt audio events | PASS |
| json_and_cart | Article JSON parsing, storage contract, standalone cart arithmetic model | PASS |
| sd_fixture | In-memory article fixture records and parsing | PASS |
| prices | Complete/partial lists, limits, malformed data, timeout, overflow and timer wrap | PASS |

The initial run exposed a missing `TIME_IMMEDIATE` in the host mock and two tests
written for earlier UI behaviour. The mock was completed, Home coordinates were
updated for the vertical layout, and the audio test now exercises the quantity
keypad rather than the removed price editor. Firmware behaviour was preserved.

## Limits

Host tests replace RTOS, serial and graphics operations with mocks. They do not
validate ADC/DAC accuracy, load-cell calibration, real serial reliability, graphical
appearance, thread scheduling, stack consumption or audio playback. The JSON/cart
suite models cart arithmetic separately from the production UI cart. The SD suite
uses fixtures and is not evidence of an SD driver. The binary gesture test covers
a legacy codec, not the primary ASCII serial link.

No embedded or Arduino build, flash, hardware-in-the-loop test or new physical
measurement was performed. ChibiOS/uGFX and the Arm toolchain were missing.
The GitHub workflow is configured but has not run until the repository is pushed.

## Suggested hardware acceptance run

1. Boot each primary node; tare the scale with an empty platform.
2. Check cursor acquisition/loss, both sensor modes and all implemented gestures.
3. Check Home dwell, clinical sample navigation and the complete retail flow.
4. Apply known masses, compare local and remote readings and record calibration
   error, settling behaviour and saturation at the analog link endpoints.
5. Exercise audio and UI during serial traffic and weight changes.
6. Check thread stacks and the shared ADC access called out in [REVIEW.md](REVIEW.md).
7. Separately save prices, reset the ESP and receiver, and repeat transmission;
   verify that incomplete input does not replace the last complete price list.
