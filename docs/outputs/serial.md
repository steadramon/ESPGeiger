---
layout: page
title: Serial Output
permalink: /output/serial
parent: Outputs
nav_order: 10
---

# Serial Output

ESPGeiger has a software Serial interface allowing simple UART communication.

The default baud is `115200`

Each command should be terminated with `\r\n`

Note that Serial commands are __disabled__ on ESPGeiger-Log builds. This is due to the Serial RX pin being in use for the user input button.

## Commands

| Command | Description | Example |
|---|---|---|
| `reboot` | Reboot ESPGeiger | `reboot` |
| `resetwifi` | Reset the WiFi credentials | `resetwifi` |
| `ratio` | Set ratio for calculating μSv | `ratio 151.0` |
| `cpm` | Return current CPM value  |  |
| `usv` | Return current uSV value |  |
| `show` | Enable 1-second CPM output |  |
| `show N` | Enable periodic output every N seconds (1-65535). Implies CPM. | `show 5` |
| `show 0` | Disable periodic output (clears every field toggle) | `show 0` |
| `show off` | Same as `show 0` | `show off` |
| `show cpm` | Toggle CPM line inclusion in periodic output | `show cpm` |
| `show cps` | Toggle CPS on periodic output |  |
| `show usv` | Toggle uSV on periodic output |  |
| `show hv` | Toggle HV on periodic output (ESPGHW only) |  |
| `hv` | Return current HV value (ESPGHW only) |  |
| `duty` | Set duty cycle (0-254) (ESPGHW only) | `duty 50` |
| `freq` | Set PWM frequency (100-9000) - usually not needed (ESPGHW only) | `freq 6000` |
| `vratio` | Set ratio for calculating HV value (ESPGHW only) | `vratio 1000` |
| `voffset` | Set offset for calculating HV value (ESPGHW only) | `voffset -10` |
| `target` | Set target CPM value for Test builds only | `cpm 1234` |

## Example Output

`show 1` enables a line every second, CPM only by default:

```
CPM: 30
CPM: 32
CPM: 28
```

`show 5` for every 5 seconds:

```
CPM: 30
CPM: 32
```

Toggle additional fields with `show cps` / `show usv`:

```
CPM: 30 CPS: 0.50 uSv: 0.20
CPM: 32 CPS: 0.53 uSv: 0.21
```

With `show hv` enabled on ESPGeiger-HW:

```
CPM: 30 CPS: 0.50 uSv: 0.20 HV: 412
CPM: 32 CPS: 0.53 uSv: 0.21 HV: 413
```

Toggle CPM off with `show cpm` for CPS-only output:

```
CPS: 0.50
CPS: 0.53
```

Turn it all off with `show 0` — periodic output stops and the regular log verbosity returns.
