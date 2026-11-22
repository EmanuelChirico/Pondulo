# Architecture and implementation

## Sensor node — STM32 A

`main.c` configures the VL53L7CX over I2C and signals frame readiness through an
interrupt and a binary semaphore. The user button switches between 8×8 / 15 Hz
and 4×4 / 60 Hz sensor modes. These are configured rates, not measured system latency.

`keypad/cursor.c` maps zones to a 320×240 screen, calculates a distance-weighted
centroid from the closest zone and admitted neighbours and applies fixed-point
smoothing. A separate UI-tick thread operates around 100 Hz, decoupling pointer
updates from ranging frames. It sends changed state and periodic heartbeats.

`keypad/gesture.c` uses a motion state machine, displacement, axis dominance,
trajectory straightness and cooldown checks for swipes. Depth thresholds and
hold times provide approach/retreat events. The sensor emits ASCII `$XY` and
`$GS` records through USART3 at 921600 baud.

## Display node — STM32 B

`pointer_link/src/xy_link.c` drains the serial stream, retains the latest pointer
state and posts gesture events without blocking the receiver. `$GS` swipe IDs
1–4 map to directions, 5 maps to PUSH, 6 is ignored by the display gesture parser,
and 7 maps to PRESENCE. The sensor gesture enum and presence-generation logic
should be read separately; this is not the legacy binary codec.

The pointer is a latest-value stream; gestures are discrete mailbox messages.
`main.c` drives a 20 ms UI tick (~50 Hz configured). `ui_macelleria` uses a graphics
mutex, incremental repainting, saved/restored cursor backgrounds and a 1500 ms
dwell threshold. Home entries are vertically stacked, not side by side.

Retail flow: product catalogue → live scale → quantity (1–99) → cart / receipt.
The quantity block is PUSH-only to avoid accidental activation after product
selection. The clinical demonstration uses hard-coded sample patient records;
there is no connection to a clinical backend.

A YX5300 player receives track commands over UART5 at 9600 baud. MP3 files live on
the player's own microSD, independently of the application's unimplemented SD
filesystem. The audio files are absent from the supplied project.

`gesture_link/` is a legacy binary gesture codec. `GESTURE_LINK_SELFTEST=1`
retains a B1-button test source; real ASCII pointer/gesture reception still runs
through the separate pointer-link thread. SD2 keyboard commands provide another
debug input. Do not switch the legacy UART receiver onto the same SD3 stream.

## Scale node — STM32 C

The HX711 driver reads the load cell and maintains a 16-sample moving average.
The application tares at startup, applies empirical calibration and controls
live/hold display updates. A 74HC595-based module drives eight local digits.
The original detailed calibration notes are in [legacy/SCALE.md](legacy/SCALE.md).

The scale clamps exported whole-gram readings to 0–1000 g and writes a 12-bit
DAC output on PA4. The display reads PA4 using ADC2 channel 17, scales the reading
to grams and applies an EMA (alpha 1/8). This is an analog voltage link with common
ground, not a serial protocol or PWM. Scale calibration and analog endpoint
calibration are separate matters.

## Price subsystem — separate prototype

The ESP8266 uses WiFiManager, an HTTP UI and LittleFS. Saving validates all four
prices, verifies a temporary file, renames it and emits `PRODUCT_ADD` /
`PRODUCT_CODE` records over UART. Values are integer cents/kg, up to 999999.
The STM32 receiver publishes only a fully validated four-product update.

There is no acknowledgement, CRC or automatic resynchronisation at STM32 boot.
The price receiver is not linked to the display. Display prices use 16-bit values,
so completing that integration requires explicit type/range handling and a cart
pricing policy. Detailed original notes are in [legacy/PRICE-BRIDGE.md](legacy/PRICE-BRIDGE.md).

## Storage boundaries

`storage/` contains a minimal article JSON parser, weak file APIs and two fixture
records. It is not linked by the display Makefile. The old `ui/` panel demo is
also excluded. A passing SD fixture test demonstrates software contracts only.
