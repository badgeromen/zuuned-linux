# Specimen Alien development AppImage — October 1, 2026

Artifact: `build-appimage/dist/specimen-alien-20261001/Zuuned-Test-0.1.0-x86_64.AppImage`

SHA256: `82ffd7941fd4735f2d47a21f40fd5c81697a3136b1d78b2b8b5c391825517ed5`

App baseline `426005d74320814f3341d7be8bfced52f854ed90` plus uncommitted
Specimen Alien integration; explicitly labeled dirty. libzune
`d29bf7a5492b481f3211fbaeb3c596a00db7c742`, clean and unchanged.
Source snapshot: `/tmp/zuuned-specimen-alien-source-20261001`.
Debian 13, Qt 6.8.2, glibc 2.41. Original generated filename uses the
baseline commit timestamp; this build was performed October 1.

Builder: `sha256:41a9ffba8ef4d91e74d30cb66dac2b96ae11ee1269b88a444fbb55ce2def1c6e`
Runtime: `sha256:b938aa75dcb142a83cc3049b73a1a005c90b97fed7b5605533a248d63e51cf3a`

Packaging helper regressions, recursive 334-ELF dependency inventory and the
actual AppImage runtime gate passed. Evidence resides in the artifact's
`evidence/` directory, with `runtime/runtime-qIM8kTnC/PASS.txt` and
`photo-builder-PASS.txt`: isolated onboarding, import, desktop folder/open/save
pickers, diagnostics export, print settings and nested photo persistence.
No copied-profile migration gate was run for this candidate.

The desktop AppImage launched as PID 347479 after checking the old session's
completed sync and absent in-flight marker, then closing it gracefully.
The new window and OpenGL render context were observed. Physical transfers,
playback and other distributions are not certified by the container checks.
No upload or publication was performed; the separate license review remains open.
