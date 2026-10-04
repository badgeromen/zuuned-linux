#!/usr/bin/env python3
"""Decode a PTP container from a hex string.

PTP container layout (little-endian):
  [0:4]  length (uint32)
  [4:6]  type (uint16)   — 0x0001=command, 0x0002=data, 0x0003=response
  [6:8]  opcode/code (uint16)
  [8:12] transaction ID (uint32)
  [12:]  payload (params for cmd/resp, data bytes for data phase)

Usage:
  ptp-decode.py <hex-string>
  echo <hex-string> | ptp-decode.py
  ptp-decode.py --file captured_bytes.txt  (one hex string per line)
"""
import sys
import struct

TYPE_NAMES = {
    0x0001: "CMD",
    0x0002: "DATA",
    0x0003: "RESP",
    0x0004: "EVENT",
}

# Known opcodes — add more as we decode
OPCODES = {
    0x1001: "GetDeviceInfo",
    0x1002: "OpenSession",
    0x1003: "CloseSession",
    0x1004: "GetStorageIDs",
    0x1005: "GetStorageInfo",
    0x1006: "GetNumObjects",
    0x1007: "GetObjectHandles",
    0x1008: "GetObjectInfo",
    0x1009: "GetObject",
    0x100A: "GetThumb",
    0x100B: "DeleteObject",
    0x100C: "SendObjectInfo",
    0x100D: "SendObject",
    0x1014: "SetObjectProtection",
    0x1015: "GetDevicePropValue",
    0x1016: "SetDevicePropValue",
    0x9801: "GetObjectPropsSupported",
    0x9802: "GetObjectPropDesc",
    0x9803: "GetObjectPropValue",
    0x9804: "SetObjectPropValue",
    0x9805: "GetObjectPropList",
    0x9806: "GetObjectPropList",
    0x9808: "SendObjectPropList",
    0x9810: "GetObjectReferences",
    0x9811: "SetObjectReferences",
    # Zune vendor ops (some known, some unknown)
    0x9108: "ZUNE_CleanDataStore",
    0x9171: "ZUNE_0x9171",
    0x9201: "ZUNE_ReportAddedDeleted",
    0x9202: "ZUNE_ReportAcquired",
    0x9212: "ZUNE_MTPZ_SendCertificate",
    0x9213: "ZUNE_MTPZ_ValidateResponse",
    0x9214: "ZUNE_MTPZ_EnableTrusted",
    0x9215: "ZUNE_0x9215",
    0x9217: "ZUNE_0x9217",
    0x9219: "ZUNE_0x9219",
    0x922A: "ZUNE_0x922A_UNKNOWN",
    0x922B: "ZUNE_0x922B_UNKNOWN",
    0x922C: "ZUNE_0x922C_UNKNOWN",
    0x922D: "ZUNE_0x922D_UNKNOWN",
    0x922F: "ZUNE_0x922F_UNKNOWN",
    0x9230: "ZUNE_0x9230_UNKNOWN",
}

RESPONSE_CODES = {
    0x2001: "OK",
    0x2002: "GeneralError",
    0x2003: "SessionNotOpen",
    0x2004: "InvalidTransactionID",
    0x2005: "OperationNotSupported",
    0x2006: "ParameterNotSupported",
    0x2007: "IncompleteTransfer",
    0x2008: "InvalidStorageID",
    0x2009: "InvalidObjectHandle",
    0x200A: "DevicePropNotSupported",
    0x200B: "InvalidObjectFormatCode",
    0x200C: "StoreFull",
    0x200D: "ObjectWriteProtected",
    0x200E: "StoreReadOnly",
    0x200F: "AccessDenied",
    0x2010: "NoThumbnailPresent",
    0x2011: "SelfTestFailed",
    0x2012: "PartialDeletion",
    0x2013: "StoreNotAvailable",
    0x2014: "SpecByFormatUnsupported",
    0x2015: "NoValidObjectInfo",
    0x2016: "InvalidCodeFormat",
    0xA801: "ObjectPropNotSupported",
    0xA802: "InvalidObjectPropFormat",
    0xA803: "InvalidObjectPropCode",
    0xA804: "InvalidObjectPropValue",
    0xA806: "InvalidObjectReference",
    0xA808: "GroupNotSupported",
    0xA80A: "InvalidDataset",
    0xA80B: "SpecByGroupUnsupported",
    0xA80C: "SpecByDepthUnsupported",
    0xA80D: "ObjectTooLarge",
    0xA80E: "ObjectPropNotSupported2",
}


def decode(hexstr: str) -> None:
    hexstr = hexstr.strip().replace(" ", "").replace(":", "").lower()
    if not hexstr:
        return
    try:
        data = bytes.fromhex(hexstr)
    except ValueError as e:
        print(f"bad hex: {e}")
        return
    if len(data) < 12:
        print(f"too short ({len(data)} bytes) — not a full PTP container header")
        return

    length = struct.unpack("<I", data[0:4])[0]
    ctype = struct.unpack("<H", data[4:6])[0]
    opcode = struct.unpack("<H", data[6:8])[0]
    txid = struct.unpack("<I", data[8:12])[0]
    payload = data[12:]

    type_name = TYPE_NAMES.get(ctype, f"UNKNOWN({ctype:#06x})")
    if ctype == 0x0003:
        op_name = RESPONSE_CODES.get(opcode, f"UNKNOWN_RESP({opcode:#06x})")
    else:
        op_name = OPCODES.get(opcode, f"UNKNOWN({opcode:#06x})")

    print(f"len={length}  type={type_name:4s}  op={opcode:#06x} ({op_name})  txid={txid:#010x}")

    if ctype == 0x0001 or ctype == 0x0003:
        # parameters (up to 5 × uint32 LE after the 12-byte header)
        n_params = min(len(payload) // 4, 5)
        if n_params:
            params = struct.unpack(f"<{n_params}I", payload[: n_params * 4])
            print("  params: " + ", ".join(f"{p:#010x} ({p})" for p in params))
        elif payload:
            print(f"  extra payload ({len(payload)} bytes): {payload[:32].hex()}{'...' if len(payload) > 32 else ''}")
    else:
        # data phase — dump first N bytes hex + attempt ASCII
        print(f"  data payload: {len(payload)} bytes")
        if payload:
            head = payload[:64]
            print(f"  first {len(head)} bytes hex:   {head.hex()}")
            printable = "".join(chr(b) if 32 <= b < 127 else "." for b in head)
            print(f"  first {len(head)} bytes ascii: {printable}")
            # Try UCS-2LE string interpretation (common for MTP strings)
            if len(payload) >= 2 and payload[1] == 0 and payload[0] < 128:
                try:
                    # 1 byte length prefix + UCS-2LE chars + null term
                    slen = payload[0]
                    if slen > 0 and slen * 2 + 1 <= len(payload):
                        s = payload[1 : 1 + slen * 2].decode("utf-16le", errors="replace").rstrip("\x00")
                        print(f"  as MTP string ({slen} chars): {s!r}")
                except Exception:
                    pass


def main():
    args = sys.argv[1:]
    if not args:
        if sys.stdin.isatty():
            print(__doc__)
            sys.exit(1)
        for line in sys.stdin:
            decode(line)
        return
    if args[0] == "--file":
        with open(args[1]) as f:
            for line in f:
                decode(line)
        return
    for arg in args:
        decode(arg)


if __name__ == "__main__":
    main()
