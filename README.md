# Notifier

ESP32-S3 device that displays time, temperature, and humidity on a 1.54" e-paper screen,
publishes sensor data via MQTT for Home Assistant, and shows Uptime Kuma alerts.

## Hardware

| Component | Model |
| --- | --- |
| MCU | ESP32-S3 |
| Display | Waveshare 1.54" e-paper (GxEPD2_154_D67) |
| Sensor | SHTC3 (I2C) |

### Pinout

| Signal | ESP32-S3 pin |
| --- | --- |
| EPD DC | 10 |
| EPD CS | 11 |
| EPD SCK | 12 |
| EPD MOSI | 13 |
| EPD RST | 9 |
| EPD BUSY | 8 |
| EPD PWR (active-low) | 6 |
| VBAT PWR | 17 |
| I2C SDA | 47 |
| I2C SCL | 48 |

## How it works

Every 60 seconds:

1. Reads temperature and humidity from the SHTC3 sensor
2. Connects to WiFi and syncs time via NTP (CET/CEST timezone)
3. Publishes sensor data via MQTT with Home Assistant auto-discovery
4. Fetches Uptime Kuma RSS feeds to detect any down services
5. Updates the display: shows temp/humidity normally, or an alert if a service is down
6. Enters light sleep for 60 seconds

A full e-paper refresh runs every 10 cycles to prevent ghosting; other cycles use partial refresh.

## Configuration

Copy `config.h.example` to `config.h` and fill in your values:

```cpp
// WiFi
#define WIFI_SSID     "your_ssid"
#define WIFI_PASSWORD "your_password"

// MQTT broker
#define MQTT_HOST      "192.168.1.x"
#define MQTT_PORT      1883
#define MQTT_CLIENT_ID "notifier"
#define MQTT_USER      ""   // leave empty if not required
#define MQTT_PASS      ""
```

> `config.h` is git-ignored. Never commit credentials.

## Dependencies

Install with `make install-libs` or manually:

```text
GxEPD2
Adafruit SHTC3 Library
PubSubClient
```

`HTTPClient` and `WiFiClientSecure` are bundled with the ESP32 Arduino core — no extra install needed.

## Build and flash

Requires [arduino-cli](https://arduino.github.io/arduino-cli/).

```bash
make compile          # compile
make upload           # flash to /dev/ttyACM0
make all              # compile + upload
make monitor          # open serial monitor (115200 baud, quit with Ctrl+])
make board-list       # list connected boards
make install-libs     # install required libraries
```

Optional overrides:

```bash
make upload PORT=/dev/ttyUSB0
make compile FQBN=esp32:esp32:esp32s3
```

The serial monitor uses `python3 -m serial.tools.miniterm`. After flashing, physically
reset the board to catch output from boot.

## Uptime Kuma

Two RSS feeds are checked every cycle:

| Define | URL |
| --- | --- |
| `UPTIME_RSS_CASAVO` | `https://uptime.casavo.com/status/casavo/rss` |
| `UPTIME_RSS_HOME` | `https://uptime.alorenzi.eu/status/home/rss` |

Detection uses Uptime Kuma's native RSS format: if the channel `<description>` contains
`Current status: Degraded Service`, the feed items are scanned for the first title matching
`<name> is down`. The service name is extracted dynamically — no config needed when monitors change.

When a service is down the display shows:

```text
  10:45
──────────────
  ALLARME
  service-name
    DOWN
```

## MQTT and Home Assistant

The device publishes to:

| Topic | Payload |
| --- | --- |
| `notifier/sensor` | `{"temperature": 23.4, "humidity": 56.1}` |
| `homeassistant/sensor/notifier/temperature/config` | Discovery payload (retained) |
| `homeassistant/sensor/notifier/humidity/config` | Discovery payload (retained) |

With auto-discovery enabled in Home Assistant, the `Notifier Temperature` and
`Notifier Humidity` entities are created automatically on first boot.

## License

See [LICENSE.txt](LICENSE.txt).
