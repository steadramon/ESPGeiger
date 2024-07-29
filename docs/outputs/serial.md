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
| `ratio` | Set ratio for calculating μSv | `ratio 151.0` |
| `cpm` | Return current CPM value  |  |
| `usv` | Return current uSV value |  |
| `show` | Toggle 1 second output of CPM |  |
| `show cps` | Toggle CPS on 1 second output |  |
| `show usv` | Toggle uSV on 1 second output |  |
| `show hv` | Toggle HV on 1 second output (ESPGHW only) |  |
| `hv` | Return current HV value (ESPGHW only) |  |
| `duty` | Set duty cycle (0-254) (ESPGHW only) | `duty 50` |
| `freq` | Set PWM frequency (100-9000) - usually not needed (ESPGHW only) | `freq 6000` |
| `vratio` | Set ratio for calculating HV value (ESPGHW only) | `vratio 1000` |
| `voffset` | Set offset for calculating HV value (ESPGHW only) | `voffset -10` |
| `target` | Set target CPM value for Test builds only | `cpm 1234` |

## Example Output
