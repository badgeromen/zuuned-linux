/* Load the built shared object and check its runtime face: zune_dossier
 * matches the header, and internal helpers cannot be resolved. */
#include <dlfcn.h>
#include <stdio.h>
#include <string.h>
#include "zune.h"

#define TEXT_(x) #x
#define TEXT(x) TEXT_(x)

int main(int argc, char **argv) {
    if (argc != 2) return 2;
    void *library = dlopen(argv[1], RTLD_NOW | RTLD_LOCAL);
    if (!library) { fprintf(stderr, "FAIL: dlopen: %s\n", dlerror()); return 1; }
    int ok = 1;
    const char *(*dossier)(void);
    *(void **)(&dossier) = dlsym(library, "zune_dossier");
    const char *want = TEXT(LIBZUNE_VERSION_MAJOR) "." TEXT(LIBZUNE_VERSION_MINOR) "."
                       TEXT(LIBZUNE_VERSION_PATCH);
    if (!dossier || strcmp(dossier(), want) != 0) {
        fprintf(stderr, "FAIL: zune_dossier is not %s\n", want);
        ok = 0;
    } else {
        printf("PASS: zune_dossier() returns %s\n", dossier());
    }
    static const char *const internal[] = {
        "zune_set_error", "zune_run_tool", "zune_tool_path", "zune_mktemp",
        "zune_zmdb_read_payload", "zune_usb_find_device", "zune_cache_folder_names",
        "mtp_get_object_info", "mtp_get_object_references", "ptp_transaction", NULL
    };
    for (int i = 0; internal[i]; i++) {
        if (dlsym(library, internal[i])) {
            fprintf(stderr, "FAIL: internal %s is exported\n", internal[i]);
            ok = 0;
        }
    }
    if (ok) printf("PASS: internal helpers are not resolvable\n");
    dlclose(library);
    return ok ? 0 : 1;
}
