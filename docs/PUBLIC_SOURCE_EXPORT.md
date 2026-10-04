# Public GitHub source preparation

User decision, October 4: publish Linux source and releases together at
`badgeromen/zuuned-linux`, and the shared C library at `badgeromen/libzune`.
This supersedes the earlier separate downloads repository plan. Keep decoded
Zune authentication data in private Gitea and embedded official releases, out
of public GitHub source. No push is authorized by this document.

The private header is `libzune/src/mtpz_keys.h`. It includes certificate data,
an encryption key and RSA material, and exists in libzune history. Removing it
in a new commit or adding an ignore rule cannot sanitize that history.

## Export

Run from the private checkout:

```sh
node tools/export-public-source.mjs /absolute/path/to/new-public-tree
```

The destination must not already exist. This copies tracked and nonignored
untracked working-tree source, including current uncommitted changes. It does
not copy Git history. It includes libzune as ordinary source files, excludes
the private header, private submodule configuration, binary/archive/capture
outputs and external SDK symlinks, and materializes safe internal symlinks.
Known credential values are checked before writing the export, including raw
binary and hexadecimal forms. Unexpected matches stop the export without
printing credential values. This is a check against known material, not a
universal secret detector; review new source before each publication.

Bundled TMDB/Fanart API defaults are retained as requested by the user.
The existing settings still allow builders to override these defaults. `PUBLIC_SOURCE.md` documents these differences
and `PUBLIC_EXPORT.json` inventories exclusions. No remote or Git history is
created by the exporter.

Source builders can follow [Building with Zune authentication](BUILD_WITH_MTPZ.md)
to obtain the upstream data file and populate their local header.

## Authentication boundary

User decision, October 3: official downloadable builds retain embedded MTPZ
authentication for out-of-box device use. Keep the populated header in private
Gitea and supply it during trusted release builds. Public source export is a
separate path; do not package its missing-authentication configuration as the
official user download. Preserve the existing external-file override.

libzune conditionally includes the private header when present. Private builds
retain their embedded fallback. When absent, the existing `~/.mtpz-data`
loader remains available, but missing credentials produce a clear error and
no usable authentication state. The source can compile without credentials;
Zune authentication still requires them. No device protocol has been replaced.

The public source snapshot is deliberately different from previously published
AppImages containing embedded credentials. Excluding the header from GitHub
does not remove embedded data from those binaries and is not a conclusion
about redistribution rights or complete corresponding-source obligations.
Retain existing application and third-party licenses and accurate source links.

## Verification and publication

```sh
node tests/public-source/run.mjs
bash tests/public-source/run-auth.sh /absolute/path/to/new-public-tree
cmake -S /absolute/path/to/new-public-tree -B /tmp/zuuned-public-build -G Ninja
cmake --build /tmp/zuuned-public-build --parallel 4
```

The authentication gate blocks credential-file access for its own subprocess,
checks public missing-credential failure and private embedded availability,
then deletes its temporary executables. It never opens USB or authenticates
to a physical Zune.

After review, initialize fresh public Git history from the clean export, inspect
the staged files, and ask the user before pushing as required by AGENTS.md.
Never push or mirror the private app/libzune histories to the public destination.
