# Notifier

Un dispositivo basato su ESP32-S3 che mostra ora, temperatura e umidità su un display
e-paper da 1.54", pubblica i dati via MQTT per Home Assistant, e segnala alert
da Uptime Kuma.

## Hardware

| Componente | Modello |
| --- | --- |
| Microcontrollore | ESP32-S3 |
| Display | Waveshare 1.54" e-paper (GxEPD2_154_D67) |
| Sensore T/U | SHTC3 (I2C) |

### Pinout

| Segnale | Pin ESP32-S3 |
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

## Funzionamento

Ad ogni ciclo (ogni 60 secondi):

1. Legge temperatura e umidità dal sensore SHTC3
2. Si connette al WiFi e sincronizza l'ora via NTP (fuso orario CET/CEST)
3. Pubblica i dati su MQTT con auto-discovery per Home Assistant
4. Controlla i feed RSS di Uptime Kuma per rilevare monitor down
5. Aggiorna il display: mostra temperatura/umidità in condizioni normali, o un alert con il nome del monitor down
6. Entra in light sleep per 60 secondi

Il display esegue un full refresh ogni 10 cicli per prevenire il ghosting;
negli altri cicli usa il partial refresh per maggiore velocità.

## Configurazione

Copia `config.h.example` in `config.h` e compila i valori:

```cpp
// WiFi
#define WIFI_SSID     "your_ssid"
#define WIFI_PASSWORD "your_password"

// MQTT broker
#define MQTT_HOST      "192.168.1.x"
#define MQTT_PORT      1883
#define MQTT_CLIENT_ID "notifier"
#define MQTT_USER      ""   // lascia vuoto se non richiesto
#define MQTT_PASS      ""

// Uptime Kuma
#define UPTIME_RSS_CASAVO  "https://uptime.casavo.com/status/casavo/rss"
#define UPTIME_RSS_HOME    "https://uptime.alorenzi.eu/status/home/rss"
```

> `config.h` è ignorato da git. Non committare mai le credenziali.

## Dipendenze (librerie Arduino)

Installa con `make install-libs` oppure manualmente:

```txt
GxEPD2
Adafruit SHTC3 Library
PubSubClient
```

## Build e flash

Richiede [arduino-cli](https://arduino.github.io/arduino-cli/).

```bash
make compile          # compila
make upload           # flasha su /dev/ttyACM0
make all              # compile + upload
make monitor          # apre il monitor seriale (115200 baud)
make board-list       # lista le schede connesse
make install-libs     # installa le librerie necessarie
```

Override opzionali:

```bash
make upload PORT=/dev/ttyUSB0
make compile FQBN=esp32:esp32:esp32s3
```

## Uptime Kuma

Ad ogni ciclo vengono controllati due feed RSS:

| Define | URL |
| --- | --- |
| `UPTIME_RSS_CASAVO` | `https://uptime.casavo.com/status/casavo/rss` |
| `UPTIME_RSS_HOME` | `https://uptime.alorenzi.eu/status/home/rss` |

Il rilevamento si basa sul formato nativo di Uptime Kuma: se la `<description>` del
canale contiene `Current status: Degraded Service`, viene cercato il primo item il cui
titolo segue il pattern `<nome> is down`. Il nome viene estratto dinamicamente, quindi
non serve configurare nulla se cambiano i monitor.

Se un servizio è down, il display mostra:

```
  10:45
──────────────
  ALLARME
  nome-monitor
    DOWN
```

Il monitor seriale (`make monitor`) usa `python3 -m serial.tools.miniterm` (uscita con
`Ctrl+]`). Dopo il flash, resettare fisicamente la board per vedere l'output dal boot.

## MQTT e Home Assistant

Il dispositivo pubblica su:

| Topic | Contenuto |
| --- | --- |
| `notifier/sensor` | `{"temperature": 23.4, "humidity": 56.1}` |
| `homeassistant/sensor/notifier/temperature/config` | Discovery payload (retained) |
| `homeassistant/sensor/notifier/humidity/config` | Discovery payload (retained) |

Con l'auto-discovery attivo su Home Assistant, le entità `Notifier Temperature`
e `Notifier Humidity` vengono create automaticamente al primo avvio.

## Licenza

Vedi [LICENSE.txt](LICENSE.txt).
