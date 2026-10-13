# Zuuned Linux 0.1.2 verification

Artifact: `build-appimage/dist/0.1.2/Zuuned-Linux-0.1.2-x86_64.AppImage`
SHA256: `2d5b68056bdc7d8ce182f8da58b2b27cbb0fd41b8c742855b8eb16bbe583d3b3`

Built from the selected feature/artist-album-matching main checkout, not the
hardening worktree. App baseline 426005d and libzune baseline d29bf7a, both dirty.
Only the application version changed for packaging, to 0.1.2.
Exact private input remains at ../zuuned-release-0.1.2-source; the matching
sanitized public export is ../zuuned-release-0.1.2-public-source.
The public source archive excludes the populated authentication header.

35 native groups passed; packaging/export checks and 334-ELF inventory passed.
Exact AppImage runtime and photo gates passed in evidence/runtime/runtime-Cf3i6CPi.
This includes imports, actual portal pickers, diagnostics, artwork settings,
parent/nested-child photo creation and normal restart persistence. Visual
captures were reviewed. No USB or network was exposed. No new physical-device,
real GPU/audio or copied-profile runtime claims are made.

Image IDs, build logs, software test logs and all desktop runtime evidence live
in build-appimage/dist/0.1.2/evidence. BUILD-VERIFICATION.json records asset hashes.
Release title: Zuuned Linux 0.1.2. It remains a testing prerelease.
