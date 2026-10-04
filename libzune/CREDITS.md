# Credits & Attribution

libzune builds on protocol knowledge and reverse engineering work from
several open-source projects. While libzune is an independent C
implementation (no code was copied), the following projects were
essential references for understanding the Zune's proprietary protocols.

## XuneSyncLibrary

- **Author:** magicisinthehole
- **License:** LGPL-2.1
- **Repository:** https://github.com/xune-software/XuneSyncLibrary

Original reverse engineering of the ZMDB binary database format.
Reference implementation for:
- Device family identification via MTP property 0xD21A
- ZMDB backwards varint field parsing (skip count, disc number, last played)
- Track rating/playcount encoding (0=neutral, 8=liked, 3=disliked)
- PPP/TCP/IP/HTTP network tunnel protocol over USB vendor opcodes
- Sync partnership establishment sequence
- Podcast series/episode upload pipeline

## zune-explorer

- **Author:** NiceBeard
- **License:** MIT
- **Repository:** https://github.com/nickybeard/zune-explorer

JavaScript/Electron implementation of Zune MTP sync. Reference for:
- MTPZ authentication handshake (RSA/AES key exchange)
- ZMDB binary format parsing (Classic ZMed v2 + HD ZMed v5)
- MTP property codes and vendor operation codes
- Album/artist MTP object hierarchy
- Zune vendor properties (series name, season, episode)

## Protocol Research

Wireshark packet captures of the official Microsoft Zune desktop software
were analyzed to decode vendor opcodes 0x9217-0x9230. Findings documented
in `docs/ZUNE_VENDOR_OPS.md`.
