# Hardware and connections

The primary prototype uses three **NUCLEO-G474RE** boards. The optional price
bench uses an ESP8266 and a separate STM32 receiver. Pin roles below come from
the supplied sources and original notes; hardware was not available for retesting.

| Function | Source | Destination / peripheral | Configuration |
|---|---|---|---|
| Pointer and gestures | Sensor PC10 (USART3 TX) | Display PC11 (USART3 RX) | 921600 baud, 8N1, common GND |
| Weight | Scale PA4 (DAC1 OUT1) | Display PA4 (ADC2 IN17) | 0–VDDA corresponds to 0–1000 g, common GND |
| Audio commands | Display PC12 (UART5 TX) | YX5300 RX | 9600 baud; PD2 is the configured return pin |
| Load cell | HX711 DT | Scale PB4 | Bit-banged data input |
| Load cell | HX711 SCK | Scale PB10 | Bit-banged clock |
| Local digits | Scale PB5 | 74HC595 DIO | SPI1 MOSI |
| Local digits | Scale PB3 | 74HC595 SCK | SPI1 clock |
| Local digits | Scale PC0 | 74HC595 RCK | Latch |
| Optional prices | ESP8266 TX | Price-bench PC5 (USART1 RX) | 38400 baud, 8N1, common GND |

Sensor shield: **X-NUCLEO-53L7A1 / VL53L7CX**, connected through I2C1;
PA4 is used for sensor data-ready on the sensor board. It is a different MCU
from the scale/display boards using PA4 for the analog link.
Display path: **X-NUCLEO-GFX01M2 / ILI9341**, see `board_ILI9341.h` for SPI/GPIO
configuration. Do not reuse its pins based on the optional price-bench wiring.

The local display module uses two CL3641BH 4-digit common-anode displays and two
74HC595 devices. Its digit ordering and polarity were determined on the original
unit. Recalibrate the scale and check the module mapping when replacing hardware.
The detailed original notes include power connections and calibration constants:
[legacy/SCALE.md](legacy/SCALE.md).

Use a shared ground for inter-board signals. Keep the analog weight signal within
the destination ADC range. The 0–1000 g interval is the exported link range, not a
verified accuracy or a statement of the load cell's mechanical capacity.

Audio tracks are identified in `firmware/display-node/ui_macelleria/include/mp3_player.h`.
They must be supplied on the MP3 player's card; recordings were not included.
