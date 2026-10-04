# MTPZ Authentication Protocol

> **Documentation status (2026-10-04):** Protocol research, not an authentication-data distribution or API guide. Public source does not embed values; current setup is in BUILD_WITH_MTPZ.md and PUBLIC_CREDENTIALS.md. A successful device-handle return alone does not prove authentication.

## Overview

The Zune requires MTPZ (MTP-Zune) authentication before accepting any MTP commands. This is a proprietary extension to the MTP protocol that uses RSA encryption, AES-128-CBC, and CMAC (RFC 4493) for a 6-step authentication handshake.

Without successful MTPZ auth, the Zune will accept the MTP session but refuse all file operations with "Access Denied" errors.

## Prerequisites

A key data file at `~/.mtpz-data` containing:
- RSA modulus (128 bytes, hex-encoded)
- RSA private key (128 bytes, hex-encoded)
- RSA public exponent (0x10001)
- AES-128 encryption key (16 bytes)
- X.509 certificate chain (~1024+ bytes, hex-encoded)

These keys are constant across all Zune devices. They were originally extracted from the Windows Zune Software.

## Authentication Flow

### Step 1: Set Session Initiator Info
**Operation:** SetDevicePropValue (0x1016) on property 0xD406
**Value:** `"libmtp/Sajid Anwar - MTPZClassDriver"` (hardcoded string)
**Purpose:** The Zune firmware checks this string to verify the client is "authorized"

### Step 2: Reset MTPZ Handshake
**Operation:** Vendor operation 0x9216 (EndTrustedAppSession)
**Purpose:** Clears any prior authentication state on the device

### Step 3: Send Application Certificate
**Operation:** Vendor operation 0x9212 (SendWMDRMPDAppRequest)
**Payload:** ~785 bytes containing:
```
Bytes 0-4:    Header [0x02, 0x01, 0x01, 0x00, 0x00]
Bytes 5-6:    Certificate chain length (uint16BE)
Bytes 7-N:    Certificate chain data
Bytes N+1-2:  Random block marker [0x00, 0x10]
Bytes N+3-18: 16 random bytes
Bytes N+19:   Signature header [0x01, 0x00, 0x80]
Bytes N+20-147: 128-byte RSA-PSS signature
```

**Signature computation:**
1. SHA-1 hash of message bytes [2..preSignLen)
2. Place hash at offset 8 in 28-byte buffer (first 8 bytes zero)
3. SHA-1 hash of that buffer
4. MGF1-SHA1 expansion (107 bytes)
5. Build 128-byte padded block with PSS-like padding
6. RSA raw private key operation (modular exponentiation)

### Step 4: Get Device Challenge
**Operation:** Vendor operation 0x9213 (GetWMDRMPDAppResponse)
**Response:** ~272 bytes containing RSA-encrypted AES session key

**Decryption (OAEP unmasking):**
1. Extract 128-byte encrypted block at offset 4
2. RSA raw private key operation
3. OAEP unmask: MGF1 seed recovery → data recovery
4. Extract AES-128 session key from decrypted[112..127]

**AES decryption:**
- Algorithm: AES-128-CBC
- IV: all zeros (16 bytes of 0x00)
- Key: derived from SHA-1 hash of session data
- Block length: from response[134] (uint16BE)

**Decrypted payload contains:**
- Certificate echo
- Random echo (must match what we sent in Step 3)
- Device random data
- MAC hash block (used in Steps 5-6)

### Step 5: Send CMAC Confirmation
**Operation:** Vendor operation 0x9212 (SendWMDRMPDAppRequest)
**Payload:** 20 bytes
```
Bytes 0-3:  [0x02, 0x03, 0x00, 0x10]
Bytes 4-19: AES-CMAC tag
```

**CMAC computation (RFC 4493):**
```
seed = [0x00 * 15, 0x01]  (16 bytes)
key = macHash[0..15]      (from Step 4)
cmac = AES-CMAC(key, seed)
```

### Step 6: Enable Trusted File Operations
**Operation:** Vendor operation 0x9214 (EnableTrustedFilesOperations)
**Parameters:** 4 × uint32 derived from CMAC

```
cmacKey = macHash[0..15]
macCount = macHash[16..19]
cmac = AES-CMAC(cmacKey, macCount)
h1 = cmac[0..3] as uint32BE
h2 = cmac[4..7] as uint32BE
h3 = cmac[8..11] as uint32BE
h4 = cmac[12..15] as uint32BE
```

After Step 6 succeeds, the Zune accepts all MTP file operations.

## Implementation in libzune

libzune delegates MTPZ authentication to the vendored libmtp, which includes MTPZ support via libgcrypt. The `zune_connect()` function calls `LIBMTP_Open_Raw_Device()` which triggers the full MTPZ handshake internally.

The gcrypt library must be initialized before any MTPZ operations:
```c
gcry_check_version(NULL);
gcry_control(GCRYCTL_INITIALIZATION_FINISHED, 0);
```

Without this initialization, gcrypt prints "missing initialization" warnings and MTPZ may fail.

## Vendor Operations Used

| Opcode | Name | Purpose |
|--------|------|---------|
| 0x9212 | SendWMDRMPDAppRequest | Send cert + CMAC confirmation |
| 0x9213 | GetWMDRMPDAppResponse | Receive device challenge |
| 0x9214 | EnableTrustedFilesOperations | Final auth with CMAC hash |
| 0x9216 | EndTrustedAppSession | Reset handshake state |

## References

- kbhomes/libmtp-zune — original MTPZ reverse engineering
- zune-explorer `mtpz-auth.js` — JavaScript implementation
- libmtp `src/mtpz.c` — C implementation (used by libzune)
