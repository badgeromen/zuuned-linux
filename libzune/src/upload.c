/*
 * libzune — upload.c
 * One upload engine for every media object (T3.1/T3.2): create the object
 * (SendObjectInfo, or SendObjectPropList for objects created with their
 * metadata), stream the file from disk, and on failure triage the device's
 * response, remove the partial object and retry.
 */

#include "zune_internal.h"
#include <errno.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

/* The file the caller asked to send: a prepared copy is an implementation
 * detail, so errors name the original. */
static const char *source_name(const zune_upload_t *up) {
    return up->source ? up->source : up->path;
}

static const char *display_name(const zune_upload_t *up) {
    if (up->info && up->info->filename) return up->info->filename;
    const char *slash = strrchr(up->path, '/');
    return slash ? slash + 1 : up->path;
}

int zune_smuggle_core(ZuneDevice *dev, const zune_upload_t *up, uint32_t *out_handle)
{
    *out_handle = 0;
    /* Open once and keep the descriptor: every attempt sends these bytes, and
     * a source that changed size since it was measured is refused up front. */
    int fd = open(up->path, O_RDONLY | O_CLOEXEC);
    struct stat st;
    if (fd < 0 || fstat(fd, &st) != 0 || !S_ISREG(st.st_mode) || (uint64_t)st.st_size != up->size) {
        if (fd >= 0) close(fd);
        zune_set_error("Cannot send %s: %s is missing, unreadable or changed size", up->kind, source_name(up));
        return ZUNE_UPLOAD_REFUSED;
    }

    if (up->info && up->info->object_compressed_size != up->size) {
        close(fd);
        zune_set_error("Cannot send %s: declared size differs from the file", up->kind);
        return ZUNE_UPLOAD_REFUSED;
    }

    int status = ZUNE_UPLOAD_REFUSED, ever_uncertain = 0, changed = 0;
    uint16_t failure = 0;
    uint32_t leftover = 0;
    for (int attempt = 1; attempt <= 3; attempt++) {
        if (zune_is_aborted(dev) || ZUNE_FLAG_GET(dev->disconnecting)) {
            /* Cancellation does not prove an unanswered creation left
             * nothing behind. Use the common uncertain failure below so
             * callers retain the warning needed for transfer recovery. */
            if (ever_uncertain) break;
            zune_set_error("Aborted: %s", source_name(up));
            if (failure) dev->ptp.last_response = failure;  /* name the failed send, not its cleanup */
            close(fd);
            return status;
        }
        /* A source that changed size during an earlier attempt cannot match
         * the size the object declares: stop instead of creating another. */
        if (attempt > 1 && (fstat(fd, &st) != 0 || (uint64_t)st.st_size != up->size)) {
            changed = 1;
            break;
        }

        /* Step 1: create the object. */
        uint32_t handle = 0;
        int created = up->info
            ? mtp_send_object_info(&dev->ptp, dev->storage_id, up->parent, up->info, &handle)
            : mtp_send_object_prop_list(&dev->ptp, dev->storage_id, up->parent, up->format,
                                        up->size, up->props, up->nprops, &handle);
        if (created != 0 || handle == 0 || handle == UINT32_MAX) {
            failure = dev->ptp.last_response;
            /* Success without a handle (the create helpers fail an OK that
             * carries none), or no answer: an object may exist. */
            int uncertain = created == 0 || failure == PTP_RC_OK || zune_response_uncertain(failure);
            status = uncertain ? ZUNE_UPLOAD_UNCERTAIN : ZUNE_UPLOAD_REFUSED;
            ever_uncertain |= uncertain;
            ZLOG(ZUNE_CHATTER_SHOUT, "libzune", "%s create attempt %d/3 failed for %s (%s 0x%04X)",
                 up->kind, attempt, display_name(up), zune_autopsy_name(failure), failure);
            if (zune_wound_is_mortal(failure) || (uncertain && up->strict))
                break;
        } else {
            /* Step 2: the payload, streamed from the open file. */
            if (mtp_send_object_from_fd(&dev->ptp, fd, up->size) == 0) {
                if (ever_uncertain)
                    ZLOG(ZUNE_CHATTER_SHOUT, "libzune", "%s sent on attempt %d; an earlier attempt got "
                         "no answer and may have left an object", up->kind, attempt);
                *out_handle = handle;
                close(fd);
                return ZUNE_UPLOAD_SENT;
            }
            failure = dev->ptp.last_response;
            ZLOG(ZUNE_CHATTER_SHOUT, "libzune", "%s send attempt %d/3 failed for %s (%s 0x%04X)",
                 up->kind, attempt, display_name(up), zune_autopsy_name(failure), failure);
            /* The partial object must go before anything is retried. A device
             * that already dropped it answers InvalidObjectHandle. */
            if (mtp_delete_object(&dev->ptp, handle) != 0 &&
                dev->ptp.last_response != PTP_RC_InvalidObjectHandle) {
                leftover = handle;
                status = ZUNE_UPLOAD_ORPHANED;
                break;
            }
            status = ZUNE_UPLOAD_REMOVED;
            if (zune_wound_is_mortal(failure))
                break;
        }
        if (attempt < 3) {
            zune_unjam(dev);
            usleep(500000);
        }
    }
    close(fd);
    if (status != ZUNE_UPLOAD_ORPHANED && ever_uncertain)
        status = ZUNE_UPLOAD_UNCERTAIN;     /* an unanswered creation may have left an object */

    /* The cleanup's response replaced last_response; zune_autopsy must name
     * the cause. */
    dev->ptp.last_response = failure;
    if (status == ZUNE_UPLOAD_ORPHANED) {
        *out_handle = leftover;
        zune_set_error("Failed to send %s (%s 0x%04X): %s; new object %u could not be removed, "
                       "inspect it before retrying", up->kind, zune_autopsy_name(failure), failure,
                       source_name(up), leftover);
    } else if (status == ZUNE_UPLOAD_UNCERTAIN) {
        zune_set_error("Failed to send %s (%s 0x%04X): %s; the device gave no usable answer and may "
                       "hold an incomplete object", up->kind, zune_autopsy_name(failure), failure, source_name(up));
    } else if (changed) {
        zune_set_error("Failed to send %s: %s changed size during the upload (%s 0x%04X)", up->kind,
                       source_name(up), zune_autopsy_name(failure), failure);
    } else {
        zune_set_error("Failed to send %s (%s 0x%04X): %s", up->kind, zune_autopsy_name(failure),
                       failure, source_name(up));
    }
    return status;
}
