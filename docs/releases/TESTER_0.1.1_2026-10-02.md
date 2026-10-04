# Zuuned Linux 0.1.1

Artifact: `build-appimage/dist/0.1.1/Zuuned-Linux-0.1.1-x86_64.AppImage`
SHA256: `c5093f118f3e2bd8051c9c5089d3a27cbcfca5c231e70f71a4d47bbb6cfbac3e`

Built October 2 on Debian 13 / Qt 6.8.2 / glibc 2.41. App baseline
426005d74320814f3341d7be8bfced52f854ed90 and libzune baseline
d29bf7a5492b481f3211fbaeb3c596a00db7c742 are both explicitly dirty.
Version is embedded as 0.1.1. Exact local snapshot, image IDs, test logs and
provenance are retained in the artifact's evidence directory.

Packaging regressions and recursive 334-ELF inventory passed. Native focused
checks: 76 identity/provider, 17 local/device artwork, 10 artist reuse.
Actual AppImage runtime and photo gates passed in runtime/runtime-sA6jYUgm:
onboarding, imports, real desktop portal dialogs, diagnostics, settings,
nested photo albums and restart persistence. No USB or network was exposed.
The runtime harness corrects the outdated folder-browse click coordinate;
failed and successful prior 0.1.0 rehearsal evidence is retained separately.

Hardware observations apply to the native build of the shared artist fix,
not to this packaged executable. No existing device duplicates were deleted.
Matching source hosting and remaining license/provenance review are outstanding
and disclosed in the release notes. Application code is not pushed to the
GitHub downloads repository.
