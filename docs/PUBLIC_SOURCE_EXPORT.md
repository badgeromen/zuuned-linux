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

This is a maintainer migration tool, not a setup step for people cloning the
public repository. Its authentication comparison gate requires the original
private header; do not expect that private-fallback test to pass from a public
clone without the private inputs. The public native build does not require them.

Run from the private checkout:

```sh
node tools/export-public-source.mjs /absolute/path/to/new-public-tree --without-visualizer
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

For the 0.2.0 public release, `--without-visualizer` is required. This explicit
mode removes the private renderer/service/presets, its stream-identity helper,
visualizer tests and design document, and the spoken-test visualizer stub.
The renderer omission includes `PortalState.h` and `PortalShapeMorph.h`; these
contain private animation and shape logic even though the main renderer is gone. It
transforms only the exported copies of five files:

- `CMakeLists.txt`: remove service sources, the feature option and projectM/Pulse
  dependency block.
- `qml/Main.qml`: remove the video-start connection that closes the visualizer.
- `qml/NowPlayingIsland.qml`: remove the visualizer action; keep playback,
  dismissal and playlist actions.
- `src/player/MpvController.cpp`: remove the private stream-monitor include and
  `audio-client-name` override; preserve normal audio/video playback setup.
- `tests/spoken-media/native/CMakeLists.txt`: remove the unused fixture singleton.

Expected integration blocks must match exactly once. Missing/changed blocks or
remaining private integration symbols in app/build/test code stop the export
before the destination is created. `PUBLIC_EXPORT.json` records the selected
mode, excluded paths and transformed files. The private checkout is never
modified. Omitting the flag preserves the legacy snapshot behavior, including
visualizer source; it is not appropriate for this public release.

The exporter and its fixtures are maintainer tools. Their source necessarily
names the excluded components; those references do not register or compile a
renderer. Historical documents may describe private builds and are not claims
that the public package contains their features. Review the export as well as
its manifest before preparing a public commit.

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

`ci/check.sh` distinguishes the private submodule from committed public vendored
source. Public CI reads the library revision from committed `PUBLIC_LIBZUNE.json`,
checks its full revision/tree hashes and the exact committed `libzune` tree, and
rejects modified library/provenance inputs. It does not mistake the enclosing
app commit for the library revision. The library revision is the recorded
provenance assertion; matching that commit to its independent GitHub repository
remains a release-preparation check, not a network operation in CI.

Public CI runs `tests/public-source/check-public.mjs` to verify the missing
private header, explicit visualizer exclusion and absence of private renderer
files/integration. It runs `run-auth.sh "$PWD" --public-only` for actual
missing-authentication behavior, and reports the private exporter/embedded-auth
comparison as not applicable. Private CI retains the full exporter fixture gate.
`node tests/public-source/run-layout.mjs` covers both layouts and mismatch cases.

The authentication gate blocks credential-file access for its own subprocess,
checks public missing-credential failure and private embedded availability,
then deletes its temporary executables. It never opens USB or authenticates
to a physical Zune.

For an existing public repository, apply the reviewed export on a branch based
on its current public history and reconcile contributor changes. Initialize a
fresh public history only for a repository that has none. Inspect the staged
files and ask the user before pushing as required by AGENTS.md.
Never push or mirror the private app/libzune histories to the public destination.
