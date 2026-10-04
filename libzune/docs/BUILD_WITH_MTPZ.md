# Building with Zune authentication

Official Zuuned downloads include MTPZ authentication. This guide is for people
compiling libzune from public source and wanting the same built-in behavior.
The public source omits the populated `src/mtpz_keys.h` file. You can
create it locally using an existing MTPZ data file.

## 1. Get the data file

The original libmtp-zune project hosts the five-line
[`.mtpz-data` file](https://github.com/kbhomes/libmtp-zune/blob/master/src/.mtpz-data).
Open that page and use **Download raw file**, or download it from your terminal:

```bash
curl --fail --location --output /tmp/zuuned-mtpz-data \
  https://raw.githubusercontent.com/kbhomes/libmtp-zune/master/src/.mtpz-data
```

The download was reachable and contained five hexadecimal lines when checked on
October 3, 2026. This is an external project; availability can change. You can
also use your existing working `.mtpz-data` file. Download the raw data, not a
GitHub HTML page. `mtpz-auth.js` is JavaScript implementation code, not this file.

## 2. Put the five values into the header

From your libzune source directory, create `src/mtpz_keys.h` with the
template below. If it already exists and contains working values, keep it;
there is no need to replace it.

Replace each `PASTE_LINE_...` placeholder with the entire corresponding line
from the downloaded file. Keep the quotation marks and semicolons. Do not add
`0x`, spaces, commas or the placeholder names to the hexadecimal values.

| Data file line | Header constant | Meaning |
| --- | --- | --- |
| 1 | `MTPZ_DEFAULT_PUBLIC_EXPONENT` | RSA public exponent |
| 2 | `MTPZ_DEFAULT_ENCRYPTION_KEY_HEX` | AES encryption key |
| 3 | `MTPZ_DEFAULT_MODULUS` | RSA modulus |
| 4 | `MTPZ_DEFAULT_PRIVATE_KEY` | RSA private key |
| 5 | `MTPZ_DEFAULT_CERTIFICATES_HEX` | Certificate data |

```c
#ifndef ZUNE_MTPZ_KEYS_H
#define ZUNE_MTPZ_KEYS_H

static const char MTPZ_DEFAULT_PUBLIC_EXPONENT[] =
    "PASTE_LINE_1_HERE";
static const char MTPZ_DEFAULT_ENCRYPTION_KEY_HEX[] =
    "PASTE_LINE_2_HERE";
static const char MTPZ_DEFAULT_MODULUS[] =
    "PASTE_LINE_3_HERE";
static const char MTPZ_DEFAULT_PRIVATE_KEY[] =
    "PASTE_LINE_4_HERE";
static const char MTPZ_DEFAULT_CERTIFICATES_HEX[] =
    "PASTE_LINE_5_HERE";

#endif
```

The long certificate value can remain on one line. C also allows adjacent quoted
strings on separate lines, which are joined automatically. Do not insert literal
newlines inside a quoted string.

The public export ignores this locally populated header. Keep it and your data
file out of public source commits. This guide links upstream data without copying
its values into our documentation.

## 3. Compile

Install the dependencies in [README.md](../README.md#build), then run these
commands from the source root. Cleaning libzune ensures an earlier build without
the header does not leave a stale authentication object behind.

```bash
make clean
make
make zunetool
./zunetool info
```

Follow [Device access](../README.md#linux-usb-access) for USB
permissions. Authentication data does not replace the required udev rules.

## 4. Check authentication

When libzune connects to your Zune, its logs identify the selected data source:

- `using embedded keys`: the compiled header is being used.
- `loaded keys from ~/.mtpz-data`: an external file overrides the compiled values.
- `credentials unavailable`: this build has neither usable external data nor
  compiled defaults. Check the header's exact path and rebuild libzune.

The first two messages establish which data was loaded, not whether the device
handshake succeeded. Confirm `./zunetool list` can read the device library.
Library diagnostics are written to stderr. Capture them with
`./zunetool info 2>zunetool.log` and review the file before sharing it.

## Optional: use a file without embedding it

The same public source can compile without `mtpz_keys.h`. To authenticate at
runtime, place your five-line file at `~/.mtpz-data` instead. This existing loader
is also an override for builds with embedded values. Preserve any working file
already at that location. No rebuild is needed for changes to the runtime file.

Protocol background: [libmtp-zune documentation](https://github.com/kbhomes/libmtp-zune/blob/master/mtpz.md).
