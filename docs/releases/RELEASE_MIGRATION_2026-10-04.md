# Testing release migration, October 4, 2026

Destination: https://github.com/badgeromen/zuuned-linux/releases

These are copies of existing binaries, not newly compiled packages. Source at
the migration tags is newer than these historical binaries. Do not describe
GitHub's automatic source archives as the exact corresponding source of them.

| Release | AppImage SHA256 |
| --- | --- |
| Zuuned Linux 0.1.1 | `c5093f118f3e2bd8051c9c5089d3a27cbcfca5c231e70f71a4d47bbb6cfbac3e` |
| Zuuned Linux 0.1.0 | `82ffd7941fd4735f2d47a21f40fd5c81697a3136b1d78b2b8b5c391825517ed5` |

Both target x86_64 Linux, glibc 2.41 or newer, X11/XWayland and OpenGL.
They were built using Debian 13 and Qt 6.8.2. They remain prereleases.

Original binaries, checksum files and third-party notices are preserved. Tester
feedback links and explanatory license/source text now refer to the new account.
Each release includes MIGRATION.json recording asset digests and original build
baselines. Neither release includes all later source changes.

Migration validation checks the original published SHA256 before upload and the
newly downloaded assets after upload. The 0.1.1 executable's `--version` reports
`Zuuned 0.1.1 (app 426005d74320+dirty; libzune d29bf7a5492b+dirty)`.
Historical runtime evidence is described in TESTER_0.1.1_2026-10-02.md. Migration
itself does not repeat physical Zune transfer, playback or desktop GUI tests.

A future fresh build should record the exact public application/libzune revisions,
private authentication input mode, toolchain and validation before publication.
