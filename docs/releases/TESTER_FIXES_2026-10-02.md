# Zuuned Linux 0.1.0 fixes release

Built October 2 from the selected `feature/automatic-music-artwork` checkout.
Application baseline: `426005d74320814f3341d7be8bfced52f854ed90`, dirty.
libzune baseline: `d29bf7a5492b481f3211fbaeb3c596a00db7c742`, dirty, branch
`fix/reuse-device-artists`. Working changes are included in both components.

Artifact SHA256:
`9f09b4b0d197507e50d6ac886adb61ed5c5f24742d6ca48b29ce2258088f62bb`.

Evidence directory: `build-appimage/dist/fixes-20261002/evidence/`.
The complete local source snapshot and hash are retained there; this archive
is not uploaded to the downloads-only GitHub repository.

Builder: `sha256:41a9ffba8ef4d91e74d30cb66dac2b96ae11ee1269b88a444fbb55ce2def1c6e`.
Debian 13, Qt 6.8.2, glibc 2.41. The generated filename's September 28 timestamp
comes from the baseline commit, not the build date.

Packaging helper regressions and the 334-ELF recursive dependency inventory
passed. Focused native regressions passed: 76 music identity/provider checks,
17 local/device artwork checks, and 10 artist reuse checks with sanitizers.
Native hardware observations are recorded in `libzune/docs/ARTIST_REUSE.md`;
these do not constitute a physical transfer test of this AppImage.

The first isolated runtime run failed because the Library browse button had
moved from client y=692 to y=560. Its saved screenshot shows the actual button
position. The harness was corrected without changing the packaged application;
the failed evidence is retained as `runtime/runtime-yINlGe9b`. A rerun is pending.

Matching source hosting and the remaining license/provenance review remain
outstanding and are disclosed in the release notes. No claim of completed
license review is made.
