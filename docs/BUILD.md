# Build and test

## Host tests (verified)

Requirements: Python 3 and a C11-capable native compiler (`cc`, GCC or Clang).
From the repository root:

```sh
python3 scripts/test_host.py
```

Set `CC=clang` to select a different compiler. Binaries go to ignored
`build/host-tests/`. The runner compiles and runs every test, reports all failures
and returns a nonzero exit status if any compilation or test fails.
GitHub Actions runs the same command for pushes and pull requests.

## STM32 firmware (dependencies required)

1. Install GNU Make and the `arm-none-eabi` GCC toolchain.
2. Recover the competition-compatible ChibiOS 21.11 and uGFX trees. Place them
   under `external/` as described in [external/README.md](../external/README.md),
   or override their paths below. Exact SDK revisions were not supplied.
3. Build each node separately; never flash one node's image onto another node.

```sh
make -C firmware/sensor-node
make -C firmware/display-node
make -C firmware/scale-node
make -C firmware/price-bridge
```

With libraries installed elsewhere:

```sh
make -C firmware/sensor-node CHIBIOS=/absolute/path/chibios2111
make -C firmware/display-node CHIBIOS=/absolute/path/chibios2111 GFXLIB=/absolute/path/ugfx
make -C firmware/scale-node CHIBIOS=/absolute/path/chibios2111
make -C firmware/price-bridge CHIBIOS=/absolute/path/chibios2111
```

Use `make -C firmware/<node> clean` before changing build options or SDKs.
The Makefiles retain the original board, linker and CPU settings. The price test
bench retains the MSYS2 conversion workaround from its Windows build.

Program the resulting image with your existing ST-LINK workflow / STM32 programmer.
The source project uses the STM32G474xE linker script. Check the actual build
output directory; the original setup normally generates `build/ch.elf` and HEX/BIN.
When using a raw BIN, the original project documents flash base `0x08000000`.

No matching SDK trees or Arm compiler were available during repository preparation;
these commands are instructions, not a claim that the embedded build passed.

## ESP8266

Open `firmware/esp8266/Listino_Salumeria/Listino_Salumeria.ino` in an Arduino
ESP8266 environment. The sketch includes ESP8266WiFi, ESP8266WebServer,
WiFiManager and LittleFS. Use the board and flash layout matching the original
module, with space for LittleFS. Exact board model and library versions were not
recorded in the supplied project, so the Arduino build has not been verified.

Configure Wi-Fi with the WiFiManager flow, open the displayed address and press
Save to transmit all four prices. The receiver must be running. No automatic
price retransmission occurs after receiver reset. See the original
[price bench instructions](legacy/PRICE-BRIDGE.md) for protocol and wiring.
