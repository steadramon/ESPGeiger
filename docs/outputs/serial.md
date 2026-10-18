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
| `get <module>.<key>` | Read any pref. Sensitive values (passwords, keys) print as `***`. | `get counter.ratio` |
| `set <module>.<key> <value>` | Write any pref. Validation (pattern/range/type) runs before the value sticks; live-apply prefs take effect immediately, reboot-required prefs need a `reboot`. | `set counter.ratio 151.0` |
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

Use `get` and `set` for any saved setting. Keys take the form `<group>.<key>`, using the same names as the [Config page](/configuration).

Examples:

| Command | Effect |
|---|---|
| `set counter.ratio 151.0` | CPM to µSv/h ratio |
| `set espghw.duty 50` | HV duty (HV builds) |
| `set espghw.freq 6000` | HV frequency (HV builds) |
| `set espghw.ratio 1000` | HV divider ratio (HV builds) |
| `set espghw.offset -10` | HV divider offset (HV builds) |

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

## Output format

The default output uses ESPGeiger's own labelled style, but the firmware can also emit lines in any of the protocols it supports on the input side. This is useful for piping data into existing tools that expect a particular counter's wire format, or for chaining ESPGeiger to a parent device that thinks it is talking to a real GC10 / MightyOhm.

Set the format with the `sout.format` pref. These IDs are separate from the input-side Serial Type IDs.

| `set sout.format N` | Wire format | Default baud |
|---|---|---|
| `0` | Labelled (default), driven by `show cpm` / `cps` / `usv` / `hv` | unchanged |
| `1` | MightyOhm | 9600 |
| `2` | User template, see below | 115200 |

When `sout.format` is non-zero the `show` field toggles are ignored. Output still only runs when `show N` (or `show 1`) has set a non-zero period.

Examples:

```
set sout.format 1       # MightyOhm
show 1                  # 1 line/sec
```

produces:

```
CPS, 0, CPM, 28, uSv/hr, 0.16, SLOW
CPS, 1, CPM, 30, uSv/hr, 0.17, SLOW
CPS, 0, CPM, 30, uSv/hr, 0.17, SLOW
```

The CPS field is the literal count in the most recent second, not an average. At background levels (~30 CPM) most seconds read `0` with the odd `1`, which is also how a real MightyOhm behaves.

The mode tag at the end of each line switches automatically:

* `SLOW` when CPS is 0-15 (typical background up to roughly 900 CPM)
* `FAST` when CPS is 16-255 (active source, faster sampling on the original hardware)
* `INST` when CPS exceeds 255. CPM is reported as `CPS * 60` since the 8-bit sample buffer on a real MightyOhm overflows in this regime.

## User template (format 2)

Format 2 emits whatever string you put in the `sout.tpl` pref, with `{var}` placeholders substituted on every line. Useful when you need to pipe data into a logger that expects its own line shape.

```
set sout.format 2
set sout.tpl cpm={cpm}|cps={cps}|usv={usv}
show 1
```

produces:

```
cpm=28|cps=0.50|usv=0.16
cpm=30|cps=0.53|usv=0.17
```

### Variables

<!-- gen:vars -->

| Name | Meaning |
|---|---|
| `{ut}` | Uptime in seconds since boot |
| `{cpm}` | 1-minute CPM (integer) |
| `{cpm5}` | 5-minute CPM rolling average |
| `{cpm15}` | 15-minute CPM rolling average |
| `{usv}` | Dose rate in microsieverts per hour |
| `{cps}` | Counts per second, rolling float |
| `{cps_i}` | Counts in the most recent whole second (integer) |
| `{tc}` | Total clicks since boot |
| `{life}` | Lifetime total clicks since first boot |
| `{mem}` | Free heap in bytes |
| `{rssi}` | WiFi RSSI in dBm |
| `{id}` | Six-hex chip id |
| `{name}` | Friendly name or hostname |
| `{mode}` | MightyOhm-style mode tag SLOW/FAST/INST |
| `{tube}` | 1 while pulses arrive, 0 after a silence long enough to suggest a tube, HV or wiring fault |
| `{sat}` | 1 when the rate is near the tube's dead-time limit |
| `{warn}` | 1 while CPM is over the warning level |
| `{alert}` | 1 while CPM is over the alert level |
| `{hv}` | Measured HV in volts (HV ADC builds only) |
| `{t}` | Temperature in user-preferred unit (env.unit) |
| `{h}` | Relative humidity % |
| `{p}` | Pressure in hPa |

<!-- /gen -->

A suffix after a colon picks the unit and decimals: `{t:F}` is Fahrenheit, `{usv:2}` two decimals, `{t:K1}` Kelvin to one decimal. Units work on `{t}` only; decimals on `{cps}`, `{cpm5}`, `{cpm15}`, `{usv}`, `{t}`, `{h}` and `{p}`. Any other suffix renders the placeholder verbatim.

Unknown placeholders render verbatim as `{whatever}`, so misspellings are visible on the wire. Sensor variables (`{hv}`, `{t}`, `{h}`, `{p}`) also render verbatim when the sensor is absent or does not report that value.

Visit `/vars.json` on the device to see the current rendered value of every variable - handy when authoring a template and checking that the field you want is actually populated on your build.

### Escapes

Backslash escapes work inside the template:

<!-- gen:escapes -->

| Sequence | Meaning |
|---|---|
| `\n` | Newline (0x0A) |
| `\r` | Carriage return (0x0D) |
| `\t` | Tab (0x09) |
| `\\` | Literal backslash |
| `\{` | Literal `{` |
| `\}` | Literal `}` |
| `\0` | NUL byte (0x00) |

<!-- /gen -->

If you leave off the trailing newline the firmware adds one for you. Multi-line templates are fine - e.g. `cpm={cpm}\ncps={cps}\n` emits two lines per emit period.

### Notes

* Template max length is 128 characters; a rendered line caps at 255 bytes.
* MightyOhm output (format 1) stays its own special case because the mode tag (`SLOW`/`FAST`/`INST`) and CPM extrapolation in `INST` mode can't be expressed as a static template. For a MightyOhm-like line with your own labels, combine `{cps_i}` + `{cpm}` + `{mode}` - note that in the `INST` regime the CPM won't match MightyOhm's wire-exact behaviour.

## Baud rate

Set `sout.baud` to override the serial baud rate at runtime. The change applies immediately on platforms that have a real UART (ESP8266 and classic ESP32 builds). On ESP32-S3 native USB builds the baud is virtual and the option is a no-op.

| `set sout.baud N` | Effect |
|---|---|
| `0` (default) | If `sout.format` is set to a known protocol, use that protocol's native baud. Otherwise keep the framework default (115200). |
| Any non-zero value | Force this baud regardless of format. |

After changing baud, reconnect your serial monitor at the new rate so it can decode the wire.

Examples:

```
set sout.format 1       # MightyOhm: drops baud to 9600 automatically
set sout.baud 19200     # override to 19200 regardless of format
set sout.baud 0         # back to format-default (or framework default)
```

The `sout` prefs (`format`, `baud`, `interval`, `flags`) are command-driven and do not appear on the Config page.
