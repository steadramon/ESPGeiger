---
layout: page
title: Serial Output
permalink: /output/serial
parent: Outputs
nav_order: 10
---

# Serial Output

ESPGeiger has a serial console on its USB / UART port. The same commands also work in the console box on the Status page.

The default baud is `115200`

Each command ends with a newline (`\r`, `\n` or both).

Serial commands are __disabled__ on ESPGeiger-Lite and ESPGeiger-Log builds: the serial RX pin is the user button. Use the console box on the Status page there.

## Commands

| Command | Description | Example |
|---|---|---|
| `reboot` | Restart ESPGeiger | `reboot` |
| `safe` | Restart into [safe mode](/configuration/safemode) | `safe` |
| `ver` | Print the name, version and build | `ver` |
| `resetwifi` | Erase WiFi credentials, clear the `net.*` settings (back to DHCP) and restart into the setup portal | `resetwifi` |
| `resetnet` | Clear net.* settings (static IP/gw/sn/dns) and restart to DHCP. Recovery hatch when a bad static IP makes the web UI unreachable. | `resetnet` |
| `offline` | Start this boot without WiFi, as holding the button at power-up does. Only while it boots: in the setup portal or while it joins the saved network. The next boot is normal. | `offline` |
| `get <module>.<key>` | Read any pref. Sensitive values (passwords, keys) print as `***`. | `get counter.ratio` |
| `set <module>.<key> <value>` | Write any pref. Validation (pattern/range/type) runs before the value sticks; live-apply prefs take effect immediately, restart-required prefs need a `reboot`. `net.*` (static IP) needs a `reboot` too; the old network settings are kept first, so a static IP that can't reach the gateway goes back at boot, as the web WiFi page does. | `set counter.ratio 151.0` |
| `reset <module>.<key>` | Put one pref back to its default, same rules as `set`. `reset <module>` does the whole group. Never restarts or wipes anything else. | `reset counter.ratio` |
| `pause [s]` | Stop posts to external services for s seconds, `pause 0` resumes, `pause` alone prints what's left. See [Pause](/output/pause) | `pause 600` |
| `cpm` | Return current CPM value | |
| `usv` | Return current µSv/h value | |
| `show` | Toggles the 1 second output: on when it is off, off when it is on | |
| `show N` | Enable periodic output every N seconds (1-65535). Implies CPM. | `show 5` |
| `show 0` | Stops the periodic output. The field toggles are kept for the next `show N` | `show 0` |
| `show off` | Same as `show 0` | `show off` |
| `show cpm` | Toggle CPM line inclusion in periodic output | `show cpm` |
| `show cps` | Toggle CPS on periodic output | |
| `show usv` | Toggle µSv on periodic output | |
| `show hv` | Toggle HV on periodic output (builds with HV feedback: ESPGeiger-HW, `*_hv`) | |
| `hv` | Return current live HV value (builds with HV feedback) | |
| `target` | Set target CPM value for Test builds only | `target 1234` |
| `layout` | ESP32: show the flash layout. `layout migrate` starts the [table move](/install/ota#esp32-4-mb-table-move) now | `layout migrate` |

The console also answers while the setup portal is up and while it joins WiFi at boot. Until the device has finished booting only the commands from `reboot` to `reset` above run; the others answer `after boot`. In [safe mode](/configuration/safemode) they answer `off in safe mode`.

### No WiFi at all

`set wifi.mode 1` starts every boot without WiFi: no network, no setup portal, everything else runs (counter, display, HV, SD card rows in `undated.csv` until the time is known). It is not on the web Config page, because the page goes with it. To turn WiFi back on, `set wifi.mode 0` over USB and restart, or factory reset (hold the button 5 s at power-up).

### Its own hotspot

`set wifi.mode 2` makes the device start its own Wi-Fi hotspot each boot instead of joining a network. The hotspot is named after the hostname (e.g. `ESPGeiger-a1b2c3`), and the whole web UI is at `http://192.168.4.1` on it. Uploads and network time need the internet, so they stay off.

* With a web password of 8 characters or more, the hotspot uses it as its Wi-Fi password. A shorter web password, or none, leaves the hotspot open, but the pages still ask for the web password if one is set.
* `set wifi.ap_hide 1` hides the hotspot's name; you then join it by typing the name.
* Until the time is set, the **Network Config** page shows *Clock not set* with a **Set from this browser** button. Press it and SD card rows are dated from then on.

To join a network again, save one on the **Network Config** page: that also turns the hotspot off and restarts to join it. If it can't, it goes back to the network saved before, if any, else the setup portal opens instead of the hotspot. Or `set wifi.mode 0` (over USB, or in the console box on the Status page) and restart.

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

Set the format with the `sout.format` pref. These IDs are separate from the input-side Serial type IDs.

| `set sout.format N` | Wire format | Default baud |
|---|---|---|
| `0` | Labelled (default), driven by `show cpm` / `cps` / `usv` / `hv` | unchanged |
| `1` | MightyOhm | 9600 |
| `2` | User template, see below | unchanged |

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
| `{health}` | `ok`, or what is wrong: `silent`, `storm`, `chatter`, `no_input`, `hv_low`, `hv_high`, `hv_fb`, `crashed`, `noclock`, `savefail`, `lowheap` |
| `{hv}` | Measured HV in volts (HV ADC builds only) |
| `{hv_fault}` | 1 while HV is held off (over the limit; HV ADC builds only) |
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

Set `sout.baud` to override the serial baud rate while lines stream. The change applies immediately on ESP8266 and classic ESP32 builds, which have a real UART. With streaming off (`show 0`) the console stays at 115200, so the recovery commands always work there; the log says `Serial: N baud` just before each switch. On ESP32-S2, ESP32-S3 and ESP32-C3 builds the console is native USB and the baud setting does nothing.

| `set sout.baud N` | Effect |
|---|---|
| `0` (default) | If `sout.format` is set to a known protocol, use that protocol's native baud. Otherwise 115200. |
| `9600`, `19200`, `38400`, `57600` or `115200` | Force this baud regardless of format. Any other number is refused. |

A saved rate that isn't in the list doesn't load: the console says `Prefs: sout.baud refused, default kept`, the port runs at the format's own rate, and Serial out settings aren't saved until `sout.baud` is set again.

After changing baud, reconnect your serial monitor at the new rate so it can decode the wire.

Examples:

```
set sout.format 1       # MightyOhm: drops baud to 9600 automatically
set sout.baud 19200     # override to 19200 regardless of format
set sout.baud 0         # format default; with format 0, back to 115200 at once
```

A line longer than the UART can take at once goes out over the next few loop passes, so a long template line at 9600 baud still arrives whole.

Wire format, Baud override and Template are on the Config page (System tab, **Serial out**). `interval` and `flags` are set by the `show` commands only.
