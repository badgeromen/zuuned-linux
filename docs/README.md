# Documentation guide

Reviewed against the public source layout on October 4, 2026. Application source
and downloads live at [badgeromen/zuuned-linux](https://github.com/badgeromen/zuuned-linux);
shared protocol development lives at [badgeromen/libzune](https://github.com/badgeromen/libzune).
This application snapshot includes libzune as ordinary source files.

## Start here

| Task | Guide |
| --- | --- |
| Download, launch and send a useful bug report | [Tester guide](../packaging/TESTER_README.md) |
| Install device permissions or diagnose launch failure | [Tester installation](../packaging/TESTER_INSTALL.md) |
| Compile the public application source | [Building](../BUILDING.md) |
| Supply authentication data for a source build | [Building with MTPZ](BUILD_WITH_MTPZ.md) |
| Use libzune directly | [Library README](../libzune/README.md), [API reference](../libzune/docs/API_REFERENCE.md), [source index](../libzune/docs/TOC.md) |
| Understand logs and debug reports | [Diagnostics](DIAGNOSTICS.md) |
| Find tests and run regression checks | [CI guide](../ci/README.md) |
| Understand licensing scope | [COPYING](../COPYING.md) |

## Source versus downloadable versions

Migrating an existing release copies its original executable; it does not rebuild
it from the new public tree. The 0.1.1 AppImage contains the artwork discovery,
artist reuse and UI changes listed in its [original release notes](releases/Zuuned-Linux-0.1.1-Release-Notes.md).
Later artist/album matching and whole-library audio-copy detection are present
in this source snapshot, but must not be advertised as included in that older
binary. Check the release's filename, checksum and build identification.

The public source history was created after the historical binaries. GitHub's
automatic source archives describe a tag's tree, not necessarily the code used
for an older migrated executable. Preserve the recorded provenance limitations.

## Current behavior and implementation guides

- Music: [artwork discovery](MUSIC_ARTWORK_DISCOVERY.md),
  [matching engine](MUSIC_MATCHING_ENGINE.md), [matching plan and evidence](MUSIC_MATCHING_PLAN.md),
  [verified local copies](LOCAL_MUSIC_COPIES.md).
- Editing and playback: [Sleeve customization](SLEEVE_CUSTOMIZE.md),
  [artwork print styles](ARTWORK_STYLE_STUDY.md), [video titles](VIDEO_TITLES.md).
- Organization and transfers: [photo albums](PHOTO_ALBUMS.md),
  [queue feedback](QUEUE_AND_FEEDBACK.md), [action rules](ACTION_AUDIT_FIXES.md).
- Desktop integration: [native file dialogs](DESKTOP_FILE_DIALOGS.md).
- Maintenance: [cleanup record](CODEBASE_CLEANUP.md), [feature scope](FEATURE_COMPLETION.md),
  [port plan](PORT_PLAN.md).

These documents contain dated verification results. Test counts establish what
was run on that source at that time, not a guarantee for all later builds or
hardware. Photos and playlists work locally; device transfer and on-device
presentation need separate physical-device checks. Device-browser video playback
is explicitly out of scope.

## Release maintenance

[Release verification](TESTER_RELEASE.md), [architecture](RELEASE_ARCHITECTURE.md)
and [public export preparation](PUBLIC_SOURCE_EXPORT.md) describe the maintainer
process. The export guide starts from the private development checkout.

The public packaging migration is incomplete: the container snapshot exporter
requires a libzune Git submodule, and the Arch PKGBUILD still assumes the old
checkout directory. Native compilation is the supported public-source recipe;
see [BUILDING](../BUILDING.md) for the precise limitation. Hosted release
orchestration remains a proposal, not an already deployed service.

## Historical records

`releases/` and `audits/` preserve dated findings, hashes, commands and original
repository locations. They are evidence, not current installation instructions.
[FRESH_RUN_LOG.md](FRESH_RUN_LOG.md), [CLEAN_GETAWAY.md](CLEAN_GETAWAY.md) and
[ROUND2_FIX_PLAN.md](ROUND2_FIX_PLAN.md) are historical investigations/plans.
Older dependency descriptions, paths and candidate names there must be read in
that context. Use the current guides above before following a historical command.
