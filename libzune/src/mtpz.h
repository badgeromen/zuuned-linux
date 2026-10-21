#ifndef ZUNE_MTPZ_H
#define ZUNE_MTPZ_H

#ifdef __cplusplus
extern "C" {
#endif

#include "ptp.h"

/* Load MTPZ key data: ~/.mtpz-data, else the embedded defaults of a private
 * build. Loads at most once per process (thread-safe); later calls return the
 * first result until mtpz_free_keys. Returns 0 when keys are loaded, else -1. */
int mtpz_load_keys(void);

/* Wipe and free loaded key data, then re-arm mtpz_load_keys. No handshake
 * may be running. Safe when nothing is loaded. */
void mtpz_free_keys(void);

/* Initialize libgcrypt once per process, unless the host already finished
 * its own initialization. Returns 0 when ready, -1 if libgcrypt is older
 * than the version libzune was built against. */
int mtpz_init_gcrypt(void);

/* Check if MTPZ keys are loaded and available. */
int mtpz_keys_available(void);

/* Perform the MTPZ authentication handshake.
 * Must be called after ptp_open_session() and before any MTP operations.
 *
 * Steps:
 *   1. Set SessionInitiatorInfo device property
 *   2. Reset handshake (EndTrustedAppSession)
 *   3. Send application certificate
 *   4. Validate handshake response (RSA + AES)
 *   5. Send confirmation
 *   6. Open secure sync session (EnableTrustedFilesOperations)
 *
 * Returns 0 on success, -1 on failure. */
int mtpz_handshake(ptp_session_t *session);

#ifdef __cplusplus
}
#endif

#endif /* ZUNE_MTPZ_H */
