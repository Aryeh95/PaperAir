# PaperAir

An air quality display for the living room, on the M5Stack PaperDIY's 4.7" e-paper screen.

Most air monitors show a wall of numbers. PaperAir says in plain words whether to do anything,
shows the reading behind that advice with its last 24 hours, and keeps every other reading
in a quiet row along the bottom.

![Open a window: CO₂ has been building for about 2 hours](preview/co2.png)

| | |
|---|---|
| ![The air is fresh](preview/fresh.png) | ![Run the air purifier](preview/particles.png) |
| ![Keep the windows shut](preview/smoke_outside.png) | ![24-hour trends](preview/trends.png) |

These pictures come from the desktop preview, which runs the same drawing code as the device and
reduces it to the panel's 16 greys.

## What it tells you

The headline is always one thing to do, most urgent first:

| Situation | Headline | Limit |
|---|---|---|
| CO₂ very high | Open a window now. | 1,500 ppm |
| Lots of fine particles | Run the air purifier. | PM2.5 35.5 µg/m³ |
| Formaldehyde high (with an SFA40) | Air out the room. | 80 ppb |
| Fine particles up | Run the air purifier. | PM2.5 15 µg/m³ |
| Strong smells or fumes | Air out the room. | VOC index 250 |
| Gas stove fumes | Turn on the kitchen fan. | NOx index 50 |
| CO₂ building up | Open a window. | 1,000 ppm |
| Smoke outside | Keep the windows shut. | outdoor AQI above 150 |
| Dry or humid | The air is dry. / The air is humid. | below 30% / above 60% |
| A little above usual | The air is fine. | CO₂ 800, PM2.5 9, HCHO 40, VOC 150, NOx 20 |
| Otherwise | The air is fresh. | |

Outdoor air changes the advice. When it's smoky outside, "Open a window" becomes "Air out briefly".
For indoor particles, it suggests opening a window only when the outside air is cleaner.

Where the limits come from:
- **CO₂:** German Federal Environment Agency guidance.
- **PM2.5:** US EPA AQI breakpoints and the WHO 2021 24-hour guideline.
- **Formaldehyde:** Health Canada and the WHO.
- **VOC and NOx:** Sensirion's index scales.

They're all in [`lib/air/src/advice.h`](lib/air/src/advice.h) if you'd like to change them.

## Hardware

- **M5Stack PaperDIY** (K159-B): an ESP32-S3 with 16 MB flash and 8 MB PSRAM, driving a 4.7" 960×540
  e-paper panel.
- **Sensirion SEN66:** PM1 to PM10, CO₂, VOC and NOx index, temperature and humidity.
- **Sensirion SFA40** (optional): formaldehyde. PaperAir finds it at startup. Without one, the
  formaldehyde reading and its chart are left out.

Both sensors share the PaperDIY's system I2C bus with its power chip:

| PaperDIY | SEN66 | SFA40 |
|---|---|---|
| 3V3 | VDD | VDD |
| GND | GND | GND |
| G41 (SDA) | SDA | SDA |
| G42 (SCL) | SCL | SCL |

The addresses don't clash (power chip 0x6E, SEN66 0x6B, SFA40 0x5D), and the bus already has
pull-up resistors. The SEN66 has a fan and a CO₂ sensor, so it draws more current than most I2C
parts. Power it from a 3.3 V supply pin, not a GPIO.

### Enclosures

[`enclosure/`](enclosure) has two 3D-printed designs, each with the SEN66 and SFA40 inside, plus
the FPC wiring:
- **A slim frame** for the bare board and panel.
- **A desk stand** for a PaperDIY in M5Stack's case.

![The slim frame](enclosure/render_frame.png)

## Setting it up

1. Install [PlatformIO](https://platformio.org/install) (the VS Code extension or the command line).
2. Copy `include/secrets.example.h` to `include/secrets.h` and fill in:
   - your Wi-Fi name and password
   - a PurpleAir read key and the sensor index of a PurpleAir sensor near you, plus a backup if you
     like
   - an AirNow key and your ZIP code, used as the fallback

   `secrets.h` is ignored by git, so none of this leaves your computer.
3. Look through [`include/config.h`](include/config.h). It sets the time zone, °F or °C, the
   12/24-hour clock, the temperature offset, flipping the screen, the red LED on the back and how
   often it refreshes.
4. Connect the PaperDIY by USB and run `pio run -t upload`. Use `pio device monitor` to watch the
   log.

### Buttons

| Button | Does |
|---|---|
| G4 | Switch between the main page and the 24-hour trends. The trends page goes back by itself after 2 minutes. |
| G3 | A full black-and-white refresh, to clear any ghosting. One also happens every hour. |

### Temperature offset

Once the SEN66 is in its enclosure, compare its temperature with a thermometer for an hour. Put the
difference in `kTemperatureOffsetC` in `config.h`. Humidity is corrected to match.

## Outdoor air

PaperAir tries three sources in order:

1. The main PurpleAir sensor.
2. The backup PurpleAir sensor.
3. AirNow.

The first recent, sensible reading wins. PurpleAir readings get the US EPA correction
(Barkjohn et al. 2021, with the smoke extension used on the AirNow Fire and Smoke Map). PaperAir
skips a PurpleAir sensor when it:
- is marked as indoor
- hasn't reported for 30 minutes
- has two laser channels that disagree

Outdoor air is fetched every 10 minutes, which is about 4,300 PurpleAir requests a month.

## Working on it

```sh
pio test -e native            # unit tests for the maths, parsing and advice
pio run -e preview -t exec    # redraw preview/*.png (needs SDL2: apt install libsdl2-dev / brew install sdl2)
python tools/make_fonts.py    # regenerate the fonts after changing a size (needs pillow and fonttools)
```

| Folder | Holds |
|---|---|
| `lib/air` | Plain C++ with no hardware: AQI, EPA correction, humidity compensation, PurpleAir and AirNow parsing, the 24-hour history, the Sensirion protocol checks, and the advice. It's all tested on a PC. |
| `lib/ui` | The screens, drawn with M5GFX into a 960×540 canvas. The device and the desktop preview share this code. |
| `src` | The device: sensor drivers, Wi-Fi and outdoor air, buttons, and when to refresh the screen. |
| `preview` | The desktop preview. |

## Licence

The code is under the MIT licence; see [LICENSE](LICENSE).

The fonts are [Fraunces](https://github.com/undercasetype/Fraunces) and
[Manrope](https://github.com/googlefonts/manrope), under the SIL Open Font License 1.1. Their
licence texts are in [`fonts/`](fonts). The bitmap versions in `lib/ui/src/fonts` are made from
them with `tools/make_fonts.py` and carry the same licence.
