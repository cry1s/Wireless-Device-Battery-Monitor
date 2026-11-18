# NuPhy WH80

## Scope and hardware validation

This driver supports the NuPhy WH80 keyboard over its 2.4 GHz dongle
`19F5:8F01` (tested) and its USB cable `19F5:A011` (same protocol, untested).
Other NuPhy keyboards and models, and Bluetooth mode, are deliberately not
probed.

The dongle was tested on Windows and reported:

```
NuPhy WH80  [2.4 GHz receiver]  86%
```

The charging flag was not observed on hardware: the live reading was `0x56`,
with bit 7 clear. Charging decoding is covered by simulated replies only. A
missing or malformed reply is not turned into a guessed percentage; the driver
reports nothing and leaves the device unclaimed. Wired operation, full-charge
transitions and sleep/off behavior have not been tested on hardware.

## Protocol

Source: NuPhyIO's web/desktop bundle,
[`main.b3fb29e0.js`](https://drive.nuphy.io/static/js/main.b3fb29e0.js). In that
bundle, module 64177 `createCommand` builds the frame, module 70688 defines the
constants (`DATA_INDEX = 8`), and `getBatteryState` sends `GetFunc = 5` at the
address of the active mode page (`mode * 64 + 32`). The values below were also
confirmed with a live query on the user's dongle.

The driver writes one 65-byte output report whose first byte is report ID `0`
and whose remaining 64 bytes are the request payload.

Request payload (64 bytes after report ID `0`):

| Byte | Value |
|---|---|
| 0 | `55` |
| 1 | `05`, `GetFunc` |
| 2 | `00` |
| 3 | checksum: sum of bytes 4–63 & `FF` (`21` here) |
| 4 | `01`, data length |
| 5 | `20`, address low |
| 6 | `00`, address high |
| 7 | `00` |
| 8–63 | `00` |

In short: `55 05 00 21 01 20 00 00` followed by zeros.

The reply is one input report. `HidDev::Read` drops the report ID, so the
driver sees the 64-byte payload:

| Byte | Value |
|---|---|
| 0 | `AA` |
| 1 | `05`, command echo |
| 2 | `00` |
| 3 | checksum: sum of bytes 4–63 & `FF` |
| 4 | `01`, length echo |
| 5 | `20`, address low echo |
| 6 | `00` |
| 7 | `00` |
| 8 | battery byte `b` |
| 9–63 | other state, ignored |

In short: `AA 05 00 <sum> 01 20 00 00 <b>` followed by the remaining state bytes.

`b`: bit 7 is the charging flag, bits 0–6 are the percentage (0–100):

- `percent = b & 0x7F`
- `charging = (b & 0x80) != 0`

A reply whose masked value is above 100 (for example `0xE9`, which would read
as 105 %) is rejected as invalid rather than reported.

The driver accepts a reply only if it is at least 9 bytes long and the header,
command echo, length echo, address bytes and checksum all match. It uses the
collection with usage page `0001`, usage `0000`, and 65-byte input and output
reports; the dongle's other collections are not queried.

### Address mirroring

The battery byte lives at address `0x20` in the mode 0 page. On the tested
dongle, addresses `0x20`, `0x60` and `0xA0` all returned the same byte `0x56`
(86 %), so the driver always reads `0x20` and does not spend a request on
`GetBase` first. Other addresses are not read.

### Charging over the cable

The cable firmware returns `0xE4` — 100 % with the charging bit — at all three
mode addresses while the keyboard is charging. The vendor app displays that as
100 %. The driver reports the level as unknown (`percent = -1`) for that
specific reply and keeps `charging = true`; the UI then keeps the last real
percentage for the key and labels it as last known. A charging reply below
100 % is used as-is. The dongle and cable share the key `nuphy:wh80`, so the
UI keeps one entry when the keyboard switches between them.

## Read-only behavior

The driver sends one battery request and reads once with a 600 ms timeout. It
does not retry. It sends no settings, lighting, mode, pairing or firmware
commands and never writes device state.

## Tests

`make test` includes `tests/nuphy_driver_test.cpp`, which links the production
driver against a simulated HID transport. It checks the exact outgoing request,
the 86 % reading, charging decoding, the 0 %/100 % boundaries (including a
charging 100 %), the cable's 100 %/charging reply reported as unknown,
malformed and missing replies, transport failures, already-claimed devices and
exact product and collection filtering. Expected output:

```
NuPhy WH80 driver tests passed
```
