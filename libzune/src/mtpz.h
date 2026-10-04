#ifndef ZUNE_MTPZ_H
#define ZUNE_MTPZ_H

#ifdef __cplusplus
extern "C" {
#endif

#include "ptp.h"

/* Load MTPZ key data from ~/.mtpz-data.
 * Must be called once before mtpz_handshake().
 * Returns 0 on success, -1 if file not found or parse error. */
int mtpz_load_keys(void);

/* Free loaded key data. Call on cleanup. */
void mtpz_free_keys(void);

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
