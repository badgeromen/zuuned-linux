# Zuuned Linux 0.1.1

This release fixes artwork discovery and artist reuse during Zune sync.

- Missing album covers now search all known local track locations before trying online artwork. Existing file, embedded and customized covers are preserved.
- Artist artwork lookup uses library album information to help distinguish artists with the same name, and retries temporary lookup failures once. Ambiguous identities can still require Customize.
- Settings → Library → **find missing artwork** retries discovery for your library.
- Zune sync reuses existing artist objects instead of creating a new artist for each transfer batch. Existing duplicates are not automatically removed.
- The grunge border stays visible while replacement masks load during window resizing.

Download `Zuuned-Linux-0.1.1-x86_64.AppImage`, its matching `.sha256`, and
`TESTER-README.md`. Only the Zuuned application is packaged; developer testing tools are not included.

**Requirements:** Linux x86_64, glibc 2.41 or newer, X11/XWayland and working
OpenGL. Built on Debian 13 with Qt 6.8.2. Older distributions and native Wayland
are not certified.

**Feedback:** Settings → Health → **export debug report**. Review the saved file
before attaching it to an issue. Include the release filename, steps to
reproduce, Linux desktop and Zune model. Reports are never uploaded automatically.

**Device evidence:** the native build of this fix reused existing Selena Gomez,
Zweihänder and Post Malone objects. Eight album operations completed with zero
failures and successful finalization. Jelly Roll was recreated after deletions
and reconnection; the log does not establish whether its old object survived.
This is not a claim that all existing duplicates have been repaired.

**Build identity:** app baseline `426005d74320814f3341d7be8bfced52f854ed90`
and libzune baseline `d29bf7a5492b481f3211fbaeb3c596a00db7c742`, both with
working changes included and marked dirty. This is a development snapshot.

**License/source status:** original application code is GPL-3.0-or-later;
third-party components retain their own licenses. Application source is intended
for a separate host and is not contained in this downloads repository's generated
source archives. The matching source download URL and remaining third-party
license/provenance work are still outstanding. The notices archive records the
packaged notices; it is not a corresponding-source archive or completed legal review.

**Verified for this AppImage:** 334-ELF dependency inventory; isolated startup, onboarding, music import, native folder/open/save dialogs, diagnostics export, settings persistence and nested photo-album creation/restart. Physical audio/GPU/USB checks and other distributions remain separate.

**SHA256:** `c5093f118f3e2bd8051c9c5089d3a27cbcfca5c231e70f71a4d47bbb6cfbac3e`
