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
| `resetwifi` | Erase WiFi credentials and drop into setup mode | `resetwifi` |
| `resetnet` | Clear net.* prefs (static IP/gw/sn/dns) and reboot to DHCP. Recovery hatch when a bad static IP makes the web UI unreachable. | `resetnet` |
| `get <module>.<key>` | Read any pref. Sensitive values (passwords, keys) print as `***`. | `get sys.ratio` |
| `set <module>.<key> <value>` | Write any pref. Validation (pattern/range/type) runs before the value sticks; live-apply prefs take effect immediately, reboot-required prefs need a `reboot`. | `set sys.ratio 151.0` |
| `cpm` | Return current CPM value | |
| `usv` | Return current μSv/h value | |
| `show` | Enable 1-second CPM output | |
| `show N` | Enable periodic output every N seconds (1-65535). Implies CPM. | `show 5` |
| `show 0` | Disable periodic output (clears every field toggle) | `show 0` |
| `show off` | Same as `show 0` | `show off` |
| `show cpm` | Toggle CPM line inclusion in periodic output | `show cpm` |
| `show cps` | Toggle CPS on periodic output | |
| `show usv` | Toggle μSv on periodic output | |
| `show hv` | Toggle HV on periodic output (ESPGHW only) | |
| `hv` | Return current live HV value (ESPGHW only) | |
| `target` | Set target CPM value for Test builds only | `target 1234` |

### Setting prefs

Use `get` and `set` for every persisted pref. `get` with no `<module>.<key>` echoes nothing — the names match the `/egprefs` JSON dumps and the pref keys on the Configuration page.

Common examples:

| Was | Now |
|---|---|
| `ratio 151.0` | `set sys.ratio 151.0` |
| `duty 50` | `set espghw.duty 50` |
| `freq 6000` | `set espghw.freq 6000` |
| `vratio 1000` | `set espghw.ratio 1000` |
| `voffset -10` | `set espghw.offset -10` |

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

Turn it all off with `show 0` - periodic output stops and the regular log verbosity returns.
