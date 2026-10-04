/*
 * mtp.h — MTP (Media Transfer Protocol) operations layer
 *
 * Wraps specific MTP operations on top of the PTP transport layer
 * (ptp.h/ptp.c). Provides typed accessors for storage, objects,
 * properties, references, and representative samples.
 *
 * All functions return 0 on success, -1 on error.
 * Caller-freed outputs are noted in parameter descriptions.
 *
 * Part of the native PTP/MTP stack that replaces vendored libmtp.
 *
 * Copyright (c) 2026 BadgerOmens
 */

#ifndef ZUNE_MTP_H
#define ZUNE_MTP_H

#ifdef __cplusplus
extern "C" {
#endif

#include "ptp.h"  /* gets usb.h transitively */

/* ===================================================================
 * MTP Representative Sample Property Codes
 * =================================================================== */

#define MTP_OPC_RepresentativeSampleFormat  0xDC81
#define MTP_OPC_RepresentativeSampleSize    0xDC82
#define MTP_OPC_RepresentativeSampleHeight  0xDC83
#define MTP_OPC_RepresentativeSampleWidth   0xDC84
#define MTP_OPC_RepresentativeSampleData    0xDC86

/* ===================================================================
 * Storage Info
 * =================================================================== */

typedef struct {
    uint32_t storage_id;
    uint16_t storage_type;      /* 0x0003 = Fixed RAM */
    uint16_t filesystem_type;   /* 0x0002 = Generic Hierarchical */
    uint16_t access_capability; /* 0x0000 = Read-Write */
    uint64_t max_capacity;
    uint64_t free_space;
    char *storage_description;  /* caller frees via mtp_free_storage_info */
    char *volume_label;         /* caller frees via mtp_free_storage_info */
} mtp_storage_info_t;

/* ===================================================================
 * Object Info
 * =================================================================== */

typedef struct {
    uint32_t storage_id;
    uint16_t object_format;             /* MTP_OFC_* */
    uint32_t object_compressed_size;
    uint32_t parent_object;
    uint16_t association_type;          /* 0x0001 for folders */
    char *filename;                     /* caller frees via mtp_free_object_info */
} mtp_object_info_t;

/* ===================================================================
 * Storage Operations
 * =================================================================== */

/*
 * mtp_get_storage_ids — Enumerate storage IDs on the device.
 *
 * Sends GetStorageIDs (0x1004). Response data is a uint32 count
 * followed by an array of uint32 storage IDs.
 *
 *   ids:    [out] allocated array of storage IDs, caller frees
 *   count:  [out] number of storage IDs
 *
 * Returns 0 on success, -1 on error.
 */
int mtp_get_storage_ids(ptp_session_t *s, uint32_t **ids, int *count);

/*
 * mtp_get_storage_info — Read storage info for a specific storage ID.
 *
 * Sends GetStorageInfo (0x1005) with the storage ID as param1.
 * Parses the response data into the mtp_storage_info_t struct.
 *
 *   storage_id:  storage to query
 *   info:        [out] filled storage info struct
 *
 * Returns 0 on success, -1 on error.
 */
int mtp_get_storage_info(ptp_session_t *s, uint32_t storage_id,
                         mtp_storage_info_t *info);

/*
 * mtp_free_storage_info — Free strings inside an mtp_storage_info_t.
 *
 * Does not free the struct itself.
 */
void mtp_free_storage_info(mtp_storage_info_t *info);

/* ===================================================================
 * Object Operations
 * =================================================================== */

/*
 * mtp_get_object_handles — Enumerate object handles.
 *
 * Sends GetObjectHandles (0x1007) with storage, format, and parent
 * filters. Use 0xFFFFFFFF for all storages, 0x00000000 for all
 * formats or root parent.
 *
 *   storage:  storage ID (0xFFFFFFFF = all)
 *   format:   object format filter (0x00000000 = all)
 *   parent:   parent handle filter (0x00000000 = root)
 *   handles:  [out] allocated array of handles, caller frees
 *   count:    [out] number of handles
 *
 * Returns 0 on success, -1 on error.
 */
int mtp_get_object_handles(ptp_session_t *s, uint32_t storage,
                           uint32_t format, uint32_t parent,
                           uint32_t **handles, int *count);

/*
 * mtp_get_object_info — Read object info for a specific handle.
 *
 * Sends GetObjectInfo (0x1008) with the handle as param1.
 * Parses the ObjectInfo dataset into the mtp_object_info_t struct.
 *
 *   handle:  object handle
 *   info:    [out] filled object info struct
 *
 * Returns 0 on success, -1 on error.
 */
int mtp_get_object_info(ptp_session_t *s, uint32_t handle,
                        mtp_object_info_t *info);

/*
 * mtp_free_object_info — Free strings inside an mtp_object_info_t.
 *
 * Does not free the struct itself.
 */
void mtp_free_object_info(mtp_object_info_t *info);

/*
 * mtp_get_object — Read an object's data into memory.
 *
 * Sends GetObject (0x1009) with the handle as param1.
 *
 *   handle:  object handle
 *   data:    [out] allocated data buffer, caller frees
 *   len:     [out] data length in bytes
 *
 * Returns 0 on success, -1 on error.
 */
int mtp_get_object(ptp_session_t *s, uint32_t handle,
                   uint8_t **data, uint32_t *len);

/*
 * mtp_get_object_to_file — Read an object's data directly to a file.
 *
 * Calls mtp_get_object then writes the data to the specified path.
 *
 *   handle:  object handle
 *   path:    filesystem path to write to
 *
 * Returns 0 on success, -1 on error.
 */
int mtp_get_object_to_file(ptp_session_t *s, uint32_t handle,
                           const char *path);

/*
 * mtp_send_object_info — Send object metadata (first step of send).
 *
 * Sends SendObjectInfo (0x100C) with the packed ObjectInfo dataset.
 * The device responds with the new handle.
 *
 * After a successful call, mtp_send_object() or
 * mtp_send_object_from_file() must be called to send the actual data.
 *
 *   storage:    target storage ID
 *   parent:     parent folder handle
 *   info:       object metadata (storage_id, object_format,
 *               object_compressed_size, parent_object, association_type,
 *               filename must be set)
 *   new_handle: [out] handle assigned by the device
 *
 * Returns 0 on success, -1 on error.
 */
int mtp_send_object_info(ptp_session_t *s, uint32_t storage,
                         uint32_t parent, mtp_object_info_t *info,
                         uint32_t *new_handle);

/*
 * mtp_send_object — Send object data from memory (second step of send).
 *
 * Sends SendObject (0x100D) with the data payload. Must be called
 * immediately after a successful mtp_send_object_info().
 *
 *   data:  object data
 *   len:   data length in bytes
 *
 * Returns 0 on success, -1 on error.
 */
int mtp_send_object(ptp_session_t *s, const uint8_t *data, uint32_t len);

/*
 * mtp_send_object_from_file — Send object data from a file.
 *
 * Reads the file into memory and calls mtp_send_object.
 * Must be called immediately after a successful mtp_send_object_info().
 *
 *   path:  filesystem path to read from
 *   len:   expected file size in bytes (used for validation)
 *
 * Returns 0 on success, -1 on error.
 */
int mtp_send_object_from_file(ptp_session_t *s, const char *path,
                              uint32_t len);

/*
 * mtp_delete_object — Delete an object from the device.
 *
 * Sends DeleteObject (0x100B) with the handle as param1.
 *
 *   handle:  object handle to delete
 *
 * Returns 0 on success, -1 on error.
 */
int mtp_delete_object(ptp_session_t *s, uint32_t handle);

/* ===================================================================
 * Property Operations
 * =================================================================== */

/*
 * mtp_get_object_prop_value — Read a raw object property value.
 *
 * Sends GetObjectPropValue (0x9803) with handle and propcode.
 * Returns the raw property bytes. Caller must interpret based
 * on the property's data type.
 *
 *   handle:  object handle
 *   prop:    MTP_OPC_* property code
 *   data:    [out] raw property value, caller frees
 *   len:     [out] data length in bytes
 *
 * Returns 0 on success, -1 on error.
 */
int mtp_get_object_prop_value(ptp_session_t *s, uint32_t handle,
                              uint16_t prop, uint8_t **data,
                              uint32_t *len);

/*
 * mtp_set_object_prop_value_u16 — Set a uint16 object property.
 *
 *   handle:  object handle
 *   prop:    MTP_OPC_* property code
 *   val:     uint16 value
 *
 * Returns 0 on success, -1 on error.
 */
int mtp_set_object_prop_value_u16(ptp_session_t *s, uint32_t handle,
                                  uint16_t prop, uint16_t val);

/*
 * mtp_set_object_prop_value_u32 — Set a uint32 object property.
 *
 *   handle:  object handle
 *   prop:    MTP_OPC_* property code
 *   val:     uint32 value
 *
 * Returns 0 on success, -1 on error.
 */
int mtp_set_object_prop_value_u32(ptp_session_t *s, uint32_t handle,
                                  uint16_t prop, uint32_t val);

/*
 * mtp_set_object_prop_value_str — Set a string object property (UTF-8).
 *
 * Converts the UTF-8 string to MTP string wire format (uint8 count +
 * UCS-2LE characters + null terminator) and sends it.
 *
 *   handle:  object handle
 *   prop:    MTP_OPC_* property code
 *   str:     UTF-8 string
 *
 * Returns 0 on success, -1 on error.
 */
int mtp_set_object_prop_value_str(ptp_session_t *s, uint32_t handle,
                                  uint16_t prop, const char *str);

/*
 * mtp_set_object_prop_value_ucs2 — Set a UCS-2 array object property.
 *
 * Sends a raw UCS-2 array in AUINT16 format (uint32 count + uint16
 * values). Used for properties like Description (0xDC48) that require
 * the AUINT16 wire format rather than MTP string format.
 *
 *   handle:  object handle
 *   prop:    MTP_OPC_* property code
 *   ucs2:    array of UCS-2LE code units
 *   count:   number of UCS-2 code units in the array
 *
 * Returns 0 on success, -1 on error.
 */
int mtp_set_object_prop_value_ucs2(ptp_session_t *s, uint32_t handle,
                                   uint16_t prop, const uint16_t *ucs2,
                                   uint32_t count);

/*
 * mtp_get_object_prop_list — Get multiple object properties at once.
 *
 * Sends GetObjectPropList (0x9805). Returns raw MTP ObjectPropList
 * dataset that the caller must parse.
 *
 *   handle:  object handle (0x00000000 for all)
 *   format:  object format filter (0x00000000 for all)
 *   prop:    property code (0xFFFF for all properties)
 *   group:   property group code (0x00000000 for no group)
 *   depth:   enumeration depth (0xFFFFFFFF for all)
 *   data:    [out] raw ObjectPropList data, caller frees
 *   len:     [out] data length in bytes
 *
 * Returns 0 on success, -1 on error.
 */
int mtp_get_object_prop_list(ptp_session_t *s, uint32_t handle,
                             uint32_t format, uint16_t prop,
                             uint32_t group, uint32_t depth,
                             uint8_t **data, uint32_t *len);

/* ===================================================================
 * References (album/playlist track lists)
 * =================================================================== */

/*
 * mtp_get_object_references — Get the reference list for an object.
 *
 * Used for albums and playlists to enumerate their track handles.
 * Sends GetObjectReferences (0x9810).
 *
 *   handle:  album or playlist handle
 *   refs:    [out] allocated array of referenced handles, caller frees
 *   count:   [out] number of references
 *
 * Returns 0 on success, -1 on error.
 */
int mtp_get_object_references(ptp_session_t *s, uint32_t handle,
                              uint32_t **refs, int *count);

/*
 * mtp_set_object_references — Set the reference list for an object.
 *
 * Used to assign tracks to albums or playlists.
 * Sends SetObjectReferences (0x9811).
 *
 *   handle:  album or playlist handle
 *   refs:    array of handles to reference
 *   count:   number of references
 *
 * Returns 0 on success, -1 on error.
 */
int mtp_set_object_references(ptp_session_t *s, uint32_t handle,
                              uint32_t *refs, int count);

/* ===================================================================
 * Folders
 * =================================================================== */

/*
 * mtp_create_folder — Create a folder on the device.
 *
 * Sends SendObjectInfo with ObjectFormat=Association (0x3001) and
 * AssociationType=GenericFolder (0x0001).
 *
 *   parent:     parent folder handle (0x00000000 for root)
 *   storage:    target storage ID
 *   name:       folder name (UTF-8)
 *   new_handle: [out] handle of the created folder
 *
 * Returns 0 on success, -1 on error.
 */
int mtp_create_folder(ptp_session_t *s, uint32_t parent,
                      uint32_t storage, const char *name,
                      uint32_t *new_handle);

/* ===================================================================
 * Representative Samples (album art, video thumbnails)
 * =================================================================== */

/*
 * mtp_get_representative_sample — Get a thumbnail/album art image.
 *
 * Sends GetThumb (0x100A) for the object. Returns the raw image data
 * (typically JPEG).
 *
 *   handle:  object handle
 *   data:    [out] image data, caller frees
 *   len:     [out] image data length in bytes
 *
 * Returns 0 on success, -1 on error.
 */
int mtp_get_representative_sample(ptp_session_t *s, uint32_t handle,
                                  uint8_t **data, uint32_t *len);

/*
 * mtp_send_representative_sample — Set thumbnail/album art on an object.
 *
 * Sets the representative sample properties (format, width, height,
 * size) then sends the image data via RepresentativeSampleData
 * (0xDC86).
 *
 *   handle:  object handle to attach the sample to
 *   format:  image format (e.g. MTP_OFC_JPEG = 0x3801)
 *   width:   image width in pixels
 *   height:  image height in pixels
 *   data:    raw image data
 *   len:     image data length in bytes
 *
 * Returns 0 on success, -1 on error.
 */
int mtp_send_representative_sample(ptp_session_t *s, uint32_t handle,
                                   uint16_t format, uint32_t width,
                                   uint32_t height, const uint8_t *data,
                                   uint32_t len);

/* ===================================================================
 * Property List Builder (for SendObjectPropList / SetObjPropList)
 * =================================================================== */

typedef struct {
    uint32_t handle;      /* 0 for new objects (SendObjectPropList) */
    uint16_t prop_code;   /* MTP_OPC_* or ZUNE_OPC_* */
    uint16_t data_type;   /* PTP_DTC_* */
    union {
        uint8_t  u8;
        uint16_t u16;
        uint32_t u32;
        uint64_t u64;
        char    *str;     /* UTF-8, will be converted to MTP string */
    } value;
} mtp_prop_entry_t;

/*
 * mtp_send_object_prop_list — Create a new object with a property list.
 *
 * Sends SendObjectPropList (0x9808) with the given properties. After
 * a successful call, mtp_send_object() or mtp_send_object_from_file()
 * must be called to send the actual data (unless object_size is 0 for
 * associations/folders).
 *
 * Command params: [storage_id, parent_handle, object_format,
 *                  object_size_hi32, object_size_lo32]
 * Data: packed property list (uint32 count + per-entry fields)
 * Response params: [storage, parent, new_handle]
 *
 *   storage_id:    target storage ID
 *   parent_handle: parent folder handle
 *   object_format: MTP_OFC_* format code
 *   object_size:   total object data size in bytes
 *   props:         array of property entries
 *   num_props:     number of property entries
 *   new_handle:    [out] handle assigned by the device
 *
 * Returns 0 on success, -1 on error.
 */
int mtp_send_object_prop_list(ptp_session_t *s,
                              uint32_t storage_id,
                              uint32_t parent_handle,
                              uint16_t object_format,
                              uint64_t object_size,
                              mtp_prop_entry_t *props,
                              int num_props,
                              uint32_t *new_handle);

/*
 * mtp_set_object_prop_list — Set multiple properties on existing objects.
 *
 * Sends SetObjPropList (0x9806) with the given properties. Each entry's
 * handle field identifies the target object.
 *
 * Command: no params
 * Data: packed property list (same format as SendObjectPropList)
 *
 *   props:      array of property entries (handle field must be set)
 *   num_props:  number of property entries
 *
 * Returns 0 on success, -1 on error.
 */
int mtp_set_object_prop_list(ptp_session_t *s,
                             mtp_prop_entry_t *props,
                             int num_props);

/*
 * mtp_get_props_supported — Get supported properties for an object format.
 *
 * Sends GetObjectPropsSupported (0x9801) with the format code. Returns
 * an array of MTP_OPC_* property codes that the device supports for
 * objects of that format.
 *
 *   format_code: MTP_OFC_* format code
 *   props:       [out] allocated array of uint16 property codes, caller frees
 *   count:       [out] number of property codes
 *
 * Returns 0 on success, -1 on error.
 */
int mtp_get_props_supported(ptp_session_t *s, uint16_t format_code,
                            uint16_t **props, int *count);

/*
 * mtp_get_prop_desc — Get the property description for an object format.
 *
 * Sends GetObjectPropDesc (0x9802) with the property code and format.
 * Returns raw ObjectPropDesc dataset that the caller must parse.
 *
 *   prop_code:   MTP_OPC_* property code
 *   format_code: MTP_OFC_* format code
 *   data:        [out] raw ObjectPropDesc data, caller frees
 *   len:         [out] data length in bytes
 *
 * Returns 0 on success, -1 on error.
 */
int mtp_get_prop_desc(ptp_session_t *s, uint16_t prop_code,
                      uint16_t format_code, uint8_t **data,
                      uint32_t *len);

/* ===================================================================
 * Vendor Operations
 * =================================================================== */

/*
 * mtp_vendor_operation — Execute a vendor-specific MTP operation.
 *
 * Sends a transaction with the given opcode and parameters. If
 * recv_data is non-NULL, expects a data phase response.
 *
 *   opcode:    vendor operation code
 *   params:    array of up to PTP_MAX_PARAMS parameters
 *   nparams:   number of parameters
 *   recv_data: [out] response data (NULL if no data expected), caller frees
 *   recv_len:  [out] response data length
 *
 * Returns 0 on success, -1 on error.
 */
int mtp_vendor_operation(ptp_session_t *s, uint16_t opcode,
                         uint32_t *params, int nparams,
                         uint8_t **recv_data, uint32_t *recv_len);

/* ===================================================================
 * String Helpers
 * =================================================================== */

/*
 * mtp_string_to_ucs2 — Convert a UTF-8 string to MTP string wire format.
 *
 * MTP string format on the wire:
 *   Byte 0:       uint8_t  num_chars (including null terminator)
 *   Bytes 1...:   uint16_t chars[num_chars] (UCS-2LE, null terminated)
 *
 * For an empty string or NULL input, num_chars=0 (returns 1 byte).
 * Only BMP characters are supported (no surrogate pairs needed for Zune).
 *
 *   utf8:     input UTF-8 string (may be NULL)
 *   out_len:  [out] total byte length of the wire-format output
 *
 * Returns allocated buffer (caller frees), or NULL on allocation failure.
 */
uint8_t *mtp_string_to_ucs2(const char *utf8, uint32_t *out_len);

/*
 * mtp_ucs2_to_string — Convert an MTP string from wire format to UTF-8.
 *
 * Reads the MTP string format (count byte + UCS-2LE characters) and
 * produces a null-terminated UTF-8 string.
 *
 *   data:  pointer to the MTP string in wire format
 *   len:   available bytes at data pointer
 *
 * Returns allocated UTF-8 string (caller frees), or NULL on error.
 */
char *mtp_ucs2_to_string(const uint8_t *data, uint32_t len);

#ifdef __cplusplus
}
#endif

#endif /* ZUNE_MTP_H */
