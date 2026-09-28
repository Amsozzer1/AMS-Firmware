# AMS-Firmware

ESP32 firmware for the spool modules of [AMS-X](https://amsozzer.com/projects/ams-x), an
open multi-material system for Bambu Lab printers. A stock printer caps you at four
spools and wants a human standing next to it for every change past that. This is the
board that does the standing.

One ESP32 drives a cluster of up to eight spool modules over a shared step/dir bus. The
server picks *which* spool. The firmware owns everything after that: the stepper, the
sensors, the timeouts, and saying so when a move does not finish.

[One ESP32, eight spools, two legs per filament move](https://amsozzer.com/writing/a-filament-move-is-two-legs)
is the write-up for this firmware: why a move is split in two, how the MQTT connection
survives a multi-second load, and what the shared step bus buys.

The server side is [Amsozzer1/AMS](https://github.com/Amsozzer1/AMS), and
[Driving a Bambu Lab printer over MQTT](https://amsozzer.com/writing/driving-a-bambu-lab-printer-over-mqtt)
covers how the printer's half of the protocol was worked out.

## Hardware

| | |
| --- | --- |
| Board | MH-ET Live ESP32 DevKit (`mhetesp32devkit`) |
| Driver | TMC2209, one per module |
| Motor | NEMA 17, one per module |
| Sensing | One filament sensor per module, plus the printer's own over MQTT |
| Cluster size | 8 modules, `Constants::CLUSTER_SIZE` |

Step and direction are shared across the whole cluster. A module's own enable pin is the
only thing deciding which motor turns, and it is asserted for the length of a move and
released the moment the move ends, so exactly one spool is ever energised. TMC2209 `EN`
is active low, so parked is `HIGH`.

## How a move works

A move is two legs, and the difference between them is the point:

1. **Cross the near sensor.** If the filament does not reach the module's own sensor
   inside `ENGAGE_TIMEOUT_MS`, the spool is empty or the path is jammed. This leg is a
   health check, and it is the one that catches the failure early.
2. **Run to the far one.** Keep feeding until the printer reports filament at its own
   sensor, then stop. This leg is where the move actually ends, bounded by
   `LOAD_TIMEOUT_MS`.

Unload is the same walk backwards: leave the printer's sensor first, then run until the
module's sensor lets go.

`Module::tick()` advances a move by a bounded burst of `STEPS_PER_TICK` pulses and
returns whether the move is still running. The MQTT loop calls it once per pass and
shortens its own yield while something is moving, so a load never blocks the connection
for the seconds it takes to finish.

## Topics

Every topic is keyed by the board's Wi-Fi MAC, read at boot. Two boards on one broker
cannot collide, and nothing has to be provisioned by hand — flash it, read the MAC off
the serial monitor, address it.

| Topic | Direction | Payload |
| --- | --- | --- |
| `esp/<mac>/setup` | in | the pin map and printer id, below |
| `esp/<mac>/request` | in | `{"cmd": "load", "slot": 3}` |
| `esp/<mac>/report` | out | `{"connected", "idle", "configRev", "error"}`, every 5 s |
| `printer/<id>/report` | in | `{"sensed": true}`, the printer's filament sensor |

The commands are `load`, `unload`, `abort`, and `resolve` to clear a latched fault.
`load` and `unload` carry a slot.

Everything that can go wrong on the wire is answered rather than crashed: malformed JSON,
a missing or unknown verb, a slot outside the cluster, or any command other than `abort`
arriving while a move is already running. Each one latches a fault code and rides out in
the next report.

| Code | Fault |
| --- | --- |
| `-1` | `LOAD_TIMEOUT` — a leg of a move ran out of time |
| `-2` | `REQ_UNPROCESSABLE` — the payload was not JSON |
| `-3` | `MISSING_OR_NO_CMD` — no `cmd`, or a verb nobody knows |
| `-4` | `MISSING_SLOT` — no slot, out of range, or an unpopulated one |
| `-5` | `BUSY` — the cluster is mid-move |
| `-6` | `PIN_REFUSED` — the config asked for a pin the board will not give up |
| `-7` | `CONFIG_UNPROCESSABLE` — the config was malformed or incomplete |

## Configuration

Nothing about the wiring is compiled in. The pin map arrives on `esp/<mac>/setup`:

```json
{
  "configRev": 4,
  "printerId": "01P00A000000000",
  "sharedStepPin": 26,
  "sharedDirPin": 27,
  "modules": [
    { "enablePin": 16, "sensorPin": 17 },
    { "enablePin": 18, "sensorPin": 19 }
  ]
}
```

Publishing again re-applies it live: the current move aborts, every module is destroyed
while its pins are still owned — which parks its driver on the way out — the pins are
released, and the cluster is rebuilt from the new map. `configRev` comes straight back in
the status report, so the server can tell which revision the board is actually running.

That config comes off the network, so it is not trusted. `Config::usePin` refuses GPIO 0
(boot strapping), 1 and 3 (the UART console), and 6 through 11 (SPI flash, where
reconfiguring a pin bricks the run), along with anything the SoC does not consider a
valid output. A refused pin raises `PIN_REFUSED` instead of taking the board down with
it.

## Build

The four values that describe your bench — Wi-Fi, and where the broker is — are not
tracked, so write them once before the first build:

```bash
cp include/secrets.example.h include/secrets.h
```

```bash
pio run                 # build
pio run -t upload       # flash
pio device monitor      # 115200 — prints the MAC address the topics are keyed by
```

C++17 on the Arduino framework, with [ArduinoJson](https://arduinojson.org) and
[ArduinoMqtt](https://github.com/monstrenyatko/ArduinoMqtt) as the only dependencies.
PlatformIO pulls both.

The monitor is configured with `monitor_dtr = 0` and `monitor_rts = 0`. The devkit's
auto-reset circuit pulses `EN` when a monitor asserts either on open, which otherwise
prints the ROM banner about thirty times, half-finished, before a boot sticks.

## Layout

| File | What it owns |
| --- | --- |
| `module.cpp` | One spool: the two-leg move, the step pulses, the timeouts |
| `cluster.cpp` | Which module is active, the status report, `abort` |
| `config.cpp` | Applying a config: validating pins, building and tearing down modules |
| `request.cpp` | Parsing a command off the wire before anything touches a pin |
| `fault.cpp` | One latched error code, cleared by `resolve` |
| `mqtt_esp_client.cpp` | Connect, subscribe, reconnect with backoff, publish the report |
| `network.hpp` | Wi-Fi association and the TCP socket the MQTT client writes over |
| `topicRegistry.hpp` | Every topic string, built from the MAC at boot |
| `main.cpp` | Binds the two inbound handlers and runs the loop |

The connection is rebuilt from the first step that fails, with a 5 s backoff, and a move
in flight is aborted before the retry rather than left energised against a broker that
cannot be told to stop. `isConnected()` only reports the MQTT session flag, so the socket
is checked as well — otherwise a dropped TCP link looks healthy and the board publishes
into a void.

## Status

Running on hardware, with three things still open:

- **The per-module filament sensor is not read yet, and unloading does not work because of
  it.** `Module::sensedFilament()` returns `false`. On a load that only means the first leg
  is a timer rather than a real check, because the printer's sensor still ends the move. On
  an unload `arrived()` is `!sensedFilament()`, which is `true` the first time `tick()`
  evaluates it — so `stop()` runs before `pulse()` ever does and the motor never turns. The
  pin is configured and pulled up; nothing reads it.
- **Wi-Fi credentials and the broker address are still compile-time constants**, now in
  an untracked `secrets.h`. Keeping them out of git is not the same as provisioning: they
  are baked into the image, so moving the board to another network means a rebuild. They
  belong in NVS.
- **Travel-time anomaly detection.** The time between the two sensors is a running
  average away from catching a grind before it becomes a jam.

## License

[MIT](LICENSE)
