/*
 * ptp.h — PTP/MTP protocol types, opcodes, and session functions
 *
 * Implements the PTP (Picture Transfer Protocol) layer on top of
 * the USB transport in usb.h. Includes MTP extensions and
 * Zune-specific vendor property codes.
 *
 * Part of the native PTP/MTP stack that replaces vendored libmtp.
 */

#ifndef ZUNE_PTP_H
#define ZUNE_PTP_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include "usb.h"

/* ===================================================================
 * PTP Container
 * =================================================================== */

/* Container types */
#define PTP_CT_COMMAND          0x0001
#define PTP_CT_DATA             0x0002
#define PTP_CT_RESPONSE         0x0003
#define PTP_CT_EVENT            0x0004

/* Container header: 12 bytes on the wire
 *   uint32_t  length          (total container length including header)
 *   uint16_t  type            (PTP_CT_*)
 *   uint16_t  code            (opcode or response code)
 *   uint32_t  transaction_id
 */
#define PTP_CONTAINER_HDR_SIZE  12

/* Maximum number of parameters in a command/response container */
#define PTP_MAX_PARAMS          5

/* ===================================================================
 * PTP Operation Codes (standard)
 * =================================================================== */

#define PTP_OC_GetDeviceInfo            0x1001
#define PTP_OC_OpenSession              0x1002
#define PTP_OC_CloseSession             0x1003
#define PTP_OC_GetStorageIDs            0x1004
#define PTP_OC_GetStorageInfo           0x1005
#define PTP_OC_GetNumObjects            0x1006
#define PTP_OC_GetObjectHandles         0x1007
#define PTP_OC_GetObjectInfo            0x1008
#define PTP_OC_GetObject                0x1009
#define PTP_OC_GetThumb                 0x100A
#define PTP_OC_DeleteObject             0x100B
#define PTP_OC_SendObjectInfo           0x100C
#define PTP_OC_SendObject               0x100D
#define PTP_OC_GetDevicePropDesc        0x1014
#define PTP_OC_GetDevicePropValue       0x1015
#define PTP_OC_SetDevicePropValue       0x1016
#define PTP_OC_GetPartialObject         0x101B

/* ===================================================================
 * MTP Extension Operation Codes
 * =================================================================== */

#define MTP_OC_GetObjectPropsSupported  0x9801
#define MTP_OC_GetObjectPropDesc        0x9802
#define MTP_OC_GetObjectPropValue       0x9803
#define MTP_OC_SetObjectPropValue       0x9804
#define MTP_OC_GetObjectPropList        0x9805
#define MTP_OC_SetObjPropList           0x9806
#define MTP_OC_SendObjectPropList       0x9808
#define MTP_OC_GetObjectReferences      0x9810
#define MTP_OC_SetObjectReferences      0x9811

/* ===================================================================
 * WMDRM-PD / MTPZ Operation Codes
 * =================================================================== */

#define MTP_OC_WMDRMPD_SendWMDRMPDAppRequest            0x9212
#define MTP_OC_WMDRMPD_GetWMDRMPDAppResponse            0x9213
#define MTP_OC_WMDRMPD_EnableTrustedFilesOperations     0x9214
#define MTP_OC_WMDRMPD_EndTrustedAppSession             0x9216

/* ===================================================================
 * PTP Response Codes
 * =================================================================== */

#define PTP_RC_OK                               0x2001
#define PTP_RC_GeneralError                     0x2002
#define PTP_RC_SessionNotOpen                   0x2003
#define PTP_RC_InvalidTransactionID             0x2004
#define PTP_RC_OperationNotSupported            0x2005
#define PTP_RC_ParameterNotSupported            0x2006
#define PTP_RC_IncompleteTransfer               0x2007
#define PTP_RC_InvalidStorageID                 0x2008
#define PTP_RC_InvalidObjectHandle              0x2009
#define PTP_RC_NoThumbnailPresent               0x2010
#define PTP_RC_StoreNotAvailable                0x2013
#define PTP_RC_SpecificationByFormatUnsupported 0x2014
#define PTP_RC_InvalidObjectPropCode            0x2016
#define PTP_RC_SessionAlreadyOpened             0x201E
#define PTP_RC_MTP_ObjectPropNotSupported       0xA801

/* ===================================================================
 * Internal Error Codes (not on the wire — used by ptp_transaction)
 * =================================================================== */

#define PTP_ERROR_IO                    0x02FF
#define PTP_ERROR_TIMEOUT               0x02FA
#define PTP_ERROR_CANCEL                0x02FB
#define PTP_ERROR_BADPARAM              0x02FC

/* ===================================================================
 * Data Phase Flags
 * =================================================================== */

#define PTP_DP_NODATA                   0x0000
#define PTP_DP_SENDDATA                 0x0001
#define PTP_DP_GETDATA                  0x0002

/* ===================================================================
 * PTP Data Type Codes
 * =================================================================== */

#define PTP_DTC_UNDEF                   0x0000
#define PTP_DTC_INT8                    0x0001
#define PTP_DTC_UINT8                   0x0002
#define PTP_DTC_INT16                   0x0003
#define PTP_DTC_UINT16                  0x0004
#define PTP_DTC_INT32                   0x0005
#define PTP_DTC_UINT32                  0x0006
#define PTP_DTC_INT64                   0x0007
#define PTP_DTC_UINT64                  0x0008
#define PTP_DTC_AUINT16                 0x4004  /* array of uint16 */
#define PTP_DTC_AUINT32                 0x4006  /* array of uint32 */
#define PTP_DTC_STR                     0xFFFF

/* ===================================================================
 * Byte Order
 * =================================================================== */

#define PTP_DL_LE                       0x0F    /* little-endian (always for USB) */

/* ===================================================================
 * MTP Device Property Codes
 * =================================================================== */

#define MTP_DPC_SessionInitiatorInfo    0xD406

/* ===================================================================
 * MTP Object Format Codes
 * =================================================================== */

#define MTP_OFC_Undefined                       0x3000
#define MTP_OFC_Association                     0x3001  /* folder */
#define MTP_OFC_MP3                             0x3009
#define MTP_OFC_JPEG                            0x3801
#define MTP_OFC_WMA                             0xB901
#define MTP_OFC_WMV                             0xB981
#define MTP_OFC_MP4                             0xB982
#define MTP_OFC_AbstractArtist                  0xB218  /* non-standard, Zune uses this */
#define MTP_OFC_AbstractAudioAlbum              0xBA03
#define MTP_OFC_AbstractAudioVideoPlaylist      0xBA05

/* ===================================================================
 * MTP Object Property Codes (standard)
 * =================================================================== */

#define MTP_OPC_StorageID               0xDC01
#define MTP_OPC_ObjectFormat            0xDC02
#define MTP_OPC_ObjectSize              0xDC04
#define MTP_OPC_ObjectFileName          0xDC07
#define MTP_OPC_DateCreated             0xDC08
#define MTP_OPC_DateModified            0xDC09
#define MTP_OPC_ParentObject            0xDC0B
#define MTP_OPC_Name                    0xDC44
#define MTP_OPC_Artist                  0xDC46
#define MTP_OPC_Description             0xDC48
#define MTP_OPC_Duration                0xDC89
#define MTP_OPC_Track                   0xDC8B
#define MTP_OPC_Genre                   0xDC8C
#define MTP_OPC_AlbumName               0xDC9A
#define MTP_OPC_AlbumArtist             0xDC9B
/* MetaGenre is 0xDC95 on Zune (per ZUNE_VENDOR_PROPERTIES.md), NOT 0xDD03 which
 * was wrong in the original native PTP/MTP migration. The Zune rejects 0xDD03
 * with 0xA801 (ObjectPropNotSupported) for video objects. */
#define MTP_OPC_Rating                  0xDC8A
#define MTP_OPC_MetaGenre               0xDC95
#define MTP_OPC_UseCount                0xDC91

/* ===================================================================
 * Zune Vendor Object Property Codes
 *
 * These are outside the standard MTP property range. Standard libmtp
 * maps them to 0 through its enum lookup. Our native stack sends
 * them directly over PTP.
 * =================================================================== */

#define ZUNE_OPC_SeriesName             0xDA9A
#define ZUNE_OPC_Season                 0xDAB5
#define ZUNE_OPC_Episode                0xDAB6
#define ZUNE_OPC_ArtistId               0xDAB9

/* ===================================================================
 * PTP Session
 * =================================================================== */

typedef struct ptp_session {
    zune_usb_handle_t *usb;
    uint32_t session_id;
    uint32_t transaction_id;    /* auto-incremented per transaction */
    uint8_t  byteorder;         /* PTP_DL_LE for USB */
    int      split_header_data; /* if 1, send data header separately from payload (Zune/WMP) */

    /* Cause of death for the last transaction: the device's PTP response
     * code (0x2001 = OK), or PTP_ERROR_IO for transport failures, or 0 if
     * no response was ever read. Surfaced publicly via zune_autopsy(). */
    uint16_t last_response;

    /* Borrowed pointer to the owning device's cancel flag (may be NULL).
     * Checked between data-phase chunks so zune_abort() can actually stop
     * a multi-hundred-MB transfer mid-flight. */
    volatile int *abort_flag;
} ptp_session_t;

/* ===================================================================
 * Function Declarations
 * =================================================================== */

/*
 * ptp_session_init — Initialize a PTP session struct.
 *
 * Sets session_id=0, transaction_id=0, byteorder=PTP_DL_LE.
 * The USB handle must already be opened via zune_usb_find_device().
 */
void ptp_session_init(ptp_session_t *s, zune_usb_handle_t *usb);

/*
 * ptp_open_session — Send OpenSession to the device.
 *
 * Assigns session_id=1 and resets transaction_id.
 * Returns 0 on success, -1 on error.
 */
int ptp_open_session(ptp_session_t *s);

/*
 * ptp_close_session — Send CloseSession to the device.
 *
 * Returns 0 on success, -1 on error.
 */
int ptp_close_session(ptp_session_t *s);

/*
 * ptp_transaction — Execute a full PTP transaction.
 *
 * Sends a command container with the given opcode and parameters,
 * optionally sends or receives a data phase, then reads the
 * response container.
 *
 *   s:              initialized session (must be open for most opcodes)
 *   opcode:         PTP_OC_* or MTP_OC_* operation code
 *   params:         array of up to PTP_MAX_PARAMS uint32 parameters (may be NULL)
 *   nparams:        number of parameters (0..5)
 *   flags:          PTP_DP_NODATA, PTP_DP_SENDDATA, or PTP_DP_GETDATA
 *   send_data:      data to send (PTP_DP_SENDDATA), NULL otherwise
 *   send_len:       length of send_data in bytes
 *   recv_data:      [out] received data (PTP_DP_GETDATA), caller must free()
 *   recv_len:       [out] length of received data
 *   response_code:  [out] PTP_RC_* response code from device (may be NULL)
 *   resp_params:    [out] array of up to PTP_MAX_PARAMS response params (may be NULL)
 *   resp_nparams:   [out] number of response params received (may be NULL)
 *
 * Returns 0 on success (PTP_RC_OK received), -1 on transport or protocol error.
 */
int ptp_transaction(ptp_session_t *s,
                    uint16_t opcode,
                    uint32_t *params, int nparams,
                    uint16_t flags,
                    uint8_t *send_data, uint64_t send_len,
                    uint8_t **recv_data, uint32_t *recv_len,
                    uint16_t *response_code,
                    uint32_t *resp_params, int *resp_nparams);

/*
 * ptp_get_device_prop_value — Read a device property.
 *
 * Convenience wrapper around ptp_transaction with PTP_OC_GetDevicePropValue.
 *   propcode:  MTP_DPC_* property code
 *   data:      [out] raw property value, caller must free()
 *   len:       [out] length in bytes
 *
 * Returns 0 on success, -1 on error.
 */
int ptp_get_device_prop_value(ptp_session_t *s, uint16_t propcode,
                              uint8_t **data, uint32_t *len);

/*
 * ptp_set_device_prop_value — Write a device property.
 *
 * Convenience wrapper around ptp_transaction with PTP_OC_SetDevicePropValue.
 *   propcode:  MTP_DPC_* property code
 *   data:      raw property value to send
 *   len:       length in bytes
 *
 * Returns 0 on success, -1 on error.
 */
int ptp_set_device_prop_value(ptp_session_t *s, uint16_t propcode,
                              uint8_t *data, uint32_t len);

#ifdef __cplusplus
}
#endif

#endif /* ZUNE_PTP_H */
