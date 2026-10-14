# libzune: source and documentation index

Generated from the current checkout by `node tools/update-toc.mjs`.
Run `node tools/update-toc.mjs --check` to detect stale references.
Node.js is needed only to regenerate this document, not to build or use libzune.

The public API is [include/zune.h](../include/zune.h). This index covers every
ordinary C function definition in `src/*.c` and inline definition in `src/*.h`,
including static helpers and both platform/FFmpeg conditional branches.
Definitions under inactive build conditions are indexed, not a promise that
every symbol is present in every compiled library. Authentication values are
never read or reproduced by the generator. Tool and test functions are listed
separately below. Source links point to definition starts; header links carry
complete public signatures and their contracts.

## Start here

- [README](../README.md): build, integration and the Rebellion API.
- [Buildable examples](../examples/README.md): standalone public-API clients and software-only checks.
- [API reference](API_REFERENCE.md): every public signature, ownership, status codes and implementation limits.
- [Public authentication setup](PUBLIC_CREDENTIALS.md): embedded-header and external-file behavior.
- [Wire capture findings](WIRE_CAPTURE_FINDINGS.md): observed Windows-client protocol behavior.
- [Linux testing](LINUX_TESTING.md): hardware findings and test workflow.
- [Artist reuse](ARTIST_REUSE.md): `zune_forge_artist` reuses a live matching artist;
  failed inventory reads do not justify creating one, and existing duplicates are not deleted.
- [Metadata probe](METADATA_PROBE.md): native/fallback tags and disc/year behavior.
- [Video titles](VIDEO_TITLES.md): independent display titles and wire filenames.
- [Spoken-media integration](SPOKEN_MEDIA.md): podcast APIs, identity, covers, CLI testing and audiobook limits.
- [Spoken-media research](SPOKEN_MEDIA_RESEARCH.md): capture evidence, Classic hardware gates and audiobook protocol leads.
- [Podcast listening-state investigation](SPOKEN_MEDIA_RESEARCH.md#podcast-listening-state-investigation-2026-10-09): candidate bookmark/played fields, read-only HD baseline and retention validation gates.
- [Vendor property catalog](ZUNE_VENDOR_PROPERTIES.md): metadata hex codes, types and evidence boundaries.

The macOS DriverKit backend is included in this repository. The corresponding
`ZuneUSBDriver` extension implementation belongs to the consuming macOS project;
there is no `Driver/` source tree here. Linux uses the libusb backend.

## Public API

105 declarations in `include/zune.h`. Consult the linked header for
ownership, return values and threading requirements before calling an operation.

| Function | Declaration | Implementation |
|---|---|---|
| `zune_breach()` | [include/zune.h:57](../include/zune.h#L57) | [src/device.c:54](../src/device.c#L54) |
| `zune_sever()` | [include/zune.h:62](../include/zune.h#L62) | [src/device.c:323](../src/device.c#L323) |
| `zune_get_name()` | [include/zune.h:66](../include/zune.h#L66) | [src/device.c:344](../src/device.c#L344) |
| `zune_get_model()` | [include/zune.h:67](../include/zune.h#L67) | [src/device.c:348](../src/device.c#L348) |
| `zune_get_serial()` | [include/zune.h:68](../include/zune.h#L68) | [src/device.c:352](../src/device.c#L352) |
| `zune_get_battery()` | [include/zune.h:69](../include/zune.h#L69) | [src/device.c:356](../src/device.c#L356) |
| `zune_get_capacity()` | [include/zune.h:70](../include/zune.h#L70) | [src/device.c:360](../src/device.c#L360) |
| `zune_get_headroom()` | [include/zune.h:71](../include/zune.h#L71) | [src/device.c:364](../src/device.c#L364) |
| `zune_refresh_storage()` | [include/zune.h:77](../include/zune.h#L77) | [src/device.c:373](../src/device.c#L373) |
| `zune_identify()` | [include/zune.h:78](../include/zune.h#L78) | [src/device.c:390](../src/device.c#L390) |
| `zune_get_family()` | [include/zune.h:83](../include/zune.h#L83) | [src/device.c:421](../src/device.c#L421) |
| `zune_is_hdd()` | [include/zune.h:87](../include/zune.h#L87) | [src/device.c:432](../src/device.c#L432) |
| `zune_rename()` | [include/zune.h:90](../include/zune.h#L90) | [src/device.c:560](../src/device.c#L560) |
| `zune_is_live()` | [include/zune.h:94](../include/zune.h#L94) | [src/device.c:440](../src/device.c#L440) |
| `zune_abort()` | [include/zune.h:98](../include/zune.h#L98) | [src/device.c:509](../src/device.c#L509) |
| `zune_is_aborted()` | [include/zune.h:101](../include/zune.h#L101) | [src/device.c:513](../src/device.c#L513) |
| `zune_clear_abort()` | [include/zune.h:104](../include/zune.h#L104) | [src/device.c:517](../src/device.c#L517) |
| `zune_autopsy()` | [include/zune.h:111](../include/zune.h#L111) | [src/device.c:523](../src/device.c#L523) |
| `zune_autopsy_name()` | [include/zune.h:114](../include/zune.h#L114) | [src/device.c:527](../src/device.c#L527) |
| `zune_unjam()` | [include/zune.h:119](../include/zune.h#L119) | [src/usb_recovery.c:13](../src/usb_recovery.c#L13) |
| `zune_forge_folder()` | [include/zune.h:123](../include/zune.h#L123) | [src/device.c:449](../src/device.c#L449) |
| `zune_get_folders()` | [include/zune.h:129](../include/zune.h#L129) | [src/device.c:460](../src/device.c#L460) |
| `zune_free_folders()` | [include/zune.h:130](../include/zune.h#L130) | [src/device.c:501](../src/device.c#L501) |
| `zune_get_tracks()` | [include/zune.h:151](../include/zune.h#L151) | [src/track.c:66](../src/track.c#L66) |
| `zune_free_tracks()` | [include/zune.h:152](../include/zune.h#L152) | [src/track.c:183](../src/track.c#L183) |
| `zune_smuggle_track()` | [include/zune.h:156](../include/zune.h#L156) | [src/track.c:366](../src/track.c#L366) |
| `zune_smuggle_track_tagged()` | [include/zune.h:160](../include/zune.h#L160) | [src/track.c:384](../src/track.c#L384) |
| `zune_purge_track()` | [include/zune.h:167](../include/zune.h#L167) | [src/track.c:443](../src/track.c#L443) |
| `zune_extract_track()` | [include/zune.h:170](../include/zune.h#L170) | [src/track.c:459](../src/track.c#L459) |
| `zune_get_track_state()` | [include/zune.h:178](../include/zune.h#L178) | [src/track.c:541](../src/track.c#L541) |
| `zune_set_track_state()` | [include/zune.h:185](../include/zune.h#L185) | [src/track.c:583](../src/track.c#L583) |
| `zune_verify()` | [include/zune.h:193](../include/zune.h#L193) | [src/track.c:476](../src/track.c#L476) |
| `zune_rename_item()` | [include/zune.h:200](../include/zune.h#L200) | [src/finalize.c:92](../src/finalize.c#L92) |
| `zune_sync_notify()` | [include/zune.h:213](../include/zune.h#L213) | [src/finalize.c:126](../src/finalize.c#L126) |
| `zune_probe_object()` | [include/zune.h:221](../include/zune.h#L221) | [src/track.c:620](../src/track.c#L620) |
| `zune_probe_object_named()` | [include/zune.h:225](../include/zune.h#L225) | [src/track.c:627](../src/track.c#L627) |
| `zune_get_item_refs()` | [include/zune.h:231](../include/zune.h#L231) | [src/track.c:648](../src/track.c#L648) |
| `zune_find_track()` | [include/zune.h:241](../include/zune.h#L241) | [src/search.c:14](../src/search.c#L14) |
| `zune_find_video()` | [include/zune.h:248](../include/zune.h#L248) | [src/search.c:39](../src/search.c#L39) |
| `zune_find_photo()` | [include/zune.h:252](../include/zune.h#L252) | [src/search.c:55](../src/search.c#L55) |
| `zune_get_videos()` | [include/zune.h:277](../include/zune.h#L277) | [src/video.c:268](../src/video.c#L268) |
| `zune_free_videos()` | [include/zune.h:278](../include/zune.h#L278) | [src/video.c:386](../src/video.c#L386) |
| `zune_smuggle_video_named()` | [include/zune.h:296](../include/zune.h#L296) | [src/video.c:396](../src/video.c#L396) |
| `zune_smuggle_movie()` | [include/zune.h:310](../include/zune.h#L310) | [src/video.c:440](../src/video.c#L440) |
| `zune_get_series_info()` | [include/zune.h:318](../include/zune.h#L318) | [src/video.c:463](../src/video.c#L463) |
| `zune_smuggle_episode()` | [include/zune.h:325](../include/zune.h#L325) | [src/video.c:527](../src/video.c#L527) |
| `zune_smuggle_clip()` | [include/zune.h:333](../include/zune.h#L333) | [src/video.c:571](../src/video.c#L571) |
| `zune_smuggle_other()` | [include/zune.h:339](../include/zune.h#L339) | [src/video.c:594](../src/video.c#L594) |
| `zune_purge_video()` | [include/zune.h:345](../include/zune.h#L345) | [src/video.c:616](../src/video.c#L616) |
| `zune_extract_video()` | [include/zune.h:348](../include/zune.h#L348) | [src/video.c:626](../src/video.c#L626) |
| `zune_get_photos()` | [include/zune.h:367](../include/zune.h#L367) | [src/photo.c:114](../src/photo.c#L114) |
| `zune_free_photos()` | [include/zune.h:368](../include/zune.h#L368) | [src/photo.c:189](../src/photo.c#L189) |
| `zune_get_dimensions()` | [include/zune.h:372](../include/zune.h#L372) | [src/photo.c:196](../src/photo.c#L196) |
| `zune_get_photo_albums()` | [include/zune.h:376](../include/zune.h#L376) | [src/photo.c:235](../src/photo.c#L235) |
| `zune_free_photo_albums()` | [include/zune.h:379](../include/zune.h#L379) | [src/photo.c:284](../src/photo.c#L284) |
| `zune_arm_photo()` | [include/zune.h:384](../include/zune.h#L384) | [src/photo.c:316](../src/photo.c#L316) |
| `zune_smuggle_photo()` | [include/zune.h:389](../include/zune.h#L389) | [src/photo.c:362](../src/photo.c#L362) |
| `zune_purge_photo()` | [include/zune.h:393](../include/zune.h#L393) | [src/photo.c:478](../src/photo.c#L478) |
| `zune_extract_photo()` | [include/zune.h:396](../include/zune.h#L396) | [src/photo.c:488](../src/photo.c#L488) |
| `zune_get_playlists()` | [include/zune.h:409](../include/zune.h#L409) | [src/playlist.c:20](../src/playlist.c#L20) |
| `zune_free_playlists()` | [include/zune.h:410](../include/zune.h#L410) | [src/playlist.c:112](../src/playlist.c#L112) |
| `zune_forge_playlist()` | [include/zune.h:413](../include/zune.h#L413) | [src/playlist.c:138](../src/playlist.c#L138) |
| `zune_rewire_playlist()` | [include/zune.h:417](../include/zune.h#L417) | [src/playlist.c:210](../src/playlist.c#L210) |
| `zune_purge_playlist()` | [include/zune.h:422](../include/zune.h#L422) | [src/playlist.c:240](../src/playlist.c#L240) |
| `zune_forge_album()` | [include/zune.h:433](../include/zune.h#L433) | [src/album.c:24](../src/album.c#L24) |
| `zune_rewire_album()` | [include/zune.h:442](../include/zune.h#L442) | [src/album.c:138](../src/album.c#L138) |
| `zune_forge_artist()` | [include/zune.h:452](../include/zune.h#L452) | [src/album.c:239](../src/album.c#L239) |
| `zune_link_artist()` | [include/zune.h:456](../include/zune.h#L456) | [src/album.c:336](../src/album.c#L336) |
| `zune_get_albums()` | [include/zune.h:468](../include/zune.h#L468) | [src/album.c:353](../src/album.c#L353) |
| `zune_free_albums()` | [include/zune.h:469](../include/zune.h#L469) | [src/album.c:450](../src/album.c#L450) |
| `zune_get_podcast_series()` | [include/zune.h:490](../include/zune.h#L490) | [src/podcast.c:94](../src/podcast.c#L94) |
| `zune_free_podcast_series()` | [include/zune.h:491](../include/zune.h#L491) | [src/podcast.c:85](../src/podcast.c#L85) |
| `zune_get_podcast_episodes()` | [include/zune.h:506](../include/zune.h#L506) | [src/spoken_inventory.c:98](../src/spoken_inventory.c#L98) |
| `zune_free_podcast_episodes()` | [include/zune.h:507](../include/zune.h#L507) | [src/spoken_inventory.c:89](../src/spoken_inventory.c#L89) |
| `zune_get_podcast_episode_sources()` | [include/zune.h:521](../include/zune.h#L521) | [src/podcast.c:530](../src/podcast.c#L530) |
| `zune_free_podcast_episode_sources()` | [include/zune.h:522](../include/zune.h#L522) | [src/podcast.c:523](../src/podcast.c#L523) |
| `zune_get_podcast_states()` | [include/zune.h:542](../include/zune.h#L542) | [src/podcast_state.c:155](../src/podcast_state.c#L155) |
| `zune_free_podcast_states()` | [include/zune.h:543](../include/zune.h#L543) | [src/podcast_state.c:148](../src/podcast_state.c#L148) |
| `zune_forge_podcast_series()` | [include/zune.h:554](../include/zune.h#L554) | [src/podcast.c:278](../src/podcast.c#L278) |
| `zune_smuggle_podcast_episode()` | [include/zune.h:581](../include/zune.h#L581) | [src/podcast.c:349](../src/podcast.c#L349) |
| `zune_find_podcast_episode()` | [include/zune.h:590](../include/zune.h#L590) | [src/podcast.c:438](../src/podcast.c#L438) |
| `zune_get_audiobook_parts()` | [include/zune.h:605](../include/zune.h#L605) | [src/spoken_inventory.c:139](../src/spoken_inventory.c#L139) |
| `zune_free_audiobook_parts()` | [include/zune.h:606](../include/zune.h#L606) | [src/spoken_inventory.c:130](../src/spoken_inventory.c#L130) |
| `zune_find_audiobook_part()` | [include/zune.h:619](../include/zune.h#L619) | [src/audiobook.c:133](../src/audiobook.c#L133) |
| `zune_smuggle_audiobook_part()` | [include/zune.h:626](../include/zune.h#L626) | [src/audiobook.c:215](../src/audiobook.c#L215) |
| `zune_brand()` | [include/zune.h:633](../include/zune.h#L633) | [src/thumbnail.c:16](../src/thumbnail.c#L16) |
| `zune_grab_thumb()` | [include/zune.h:639](../include/zune.h#L639) | [src/thumbnail.c:35](../src/thumbnail.c#L35) |
| `zune_grab_photo_thumb()` | [include/zune.h:645](../include/zune.h#L645) | [src/thumbnail.c:68](../src/thumbnail.c#L68) |
| `zune_arm_audio()` | [include/zune.h:654](../include/zune.h#L654) | [src/transcode.c:63](../src/transcode.c#L63) |
| `zune_retag()` | [include/zune.h:659](../include/zune.h#L659) | [src/transcode.c:127](../src/transcode.c#L127) |
| `zune_arm_video()` | [include/zune.h:664](../include/zune.h#L664) | [src/transcode.c:308](../src/transcode.c#L308) |
| `zune_arm_episode()` | [include/zune.h:668](../include/zune.h#L668) | [src/transcode.c:313](../src/transcode.c#L313) |
| `zune_infiltrate_legacy()` | [include/zune.h:684](../include/zune.h#L684) | [src/zmdb.c:719](../src/zmdb.c#L719) |
| `zune_free_library()` | [include/zune.h:685](../include/zune.h#L685) | [src/zmdb.c:1195](../src/zmdb.c#L1195) |
| `zune_finalize()` | [include/zune.h:692](../include/zune.h#L692) | [src/finalize.c:12](../src/finalize.c#L12) |
| `zune_decode_filename()` | [include/zune.h:699](../include/zune.h#L699) | [src/util.c:120](../src/util.c#L120) |
| `zune_snap_thumb()` | [include/zune.h:705](../include/zune.h#L705) | [src/util.c:369](../src/util.c#L369) |
| `zune_probe()` | [include/zune.h:729](../include/zune.h#L729) | [src/util.c:623](../src/util.c#L623)<br>[src/util.c:712](../src/util.c#L712) |
| `zune_free_metadata()` | [include/zune.h:730](../include/zune.h#L730) | [src/util.c:821](../src/util.c#L821) |
| `zune_smuggle_track_ex()` | [include/zune.h:739](../include/zune.h#L739) | [src/track.c:404](../src/track.c#L404) |
| `zune_infiltrate()` | [include/zune.h:786](../include/zune.h#L786) | [src/zmdb.c:903](../src/zmdb.c#L903) |
| `zune_free_scan()` | [include/zune.h:789](../include/zune.h#L789) | [src/zmdb.c:942](../src/zmdb.c#L942) |
| `zune_dump_raw()` | [include/zune.h:795](../include/zune.h#L795) | [src/zmdb.c:988](../src/zmdb.c#L988) |
| `zune_infiltrate_deep()` | [include/zune.h:800](../include/zune.h#L800) | [src/zmdb.c:1063](../src/zmdb.c#L1063) |
| `zune_get_error()` | [include/zune.h:806](../include/zune.h#L806) | [src/device.c:21](../src/device.c#L21) |

## Library definitions

326 function definitions, including conditional alternatives. `public` means
declared in `include/zune.h`; `internal` means a non-static implementation
symbol outside that API; `static` means file-local or header-inline.

### src/album.c

| Function | Scope | Definition |
|---|---|---|
| `prop_supported()` | static | [src/album.c:16](../src/album.c#L16) |
| `zune_forge_album()` | public | [src/album.c:24](../src/album.c#L24) |
| `zune_rewire_album()` | public | [src/album.c:138](../src/album.c#L138) |
| `artist_name_equal()` | static | [src/album.c:192](../src/album.c#L192) |
| `find_artist()` | static | [src/album.c:211](../src/album.c#L211) |
| `zune_forge_artist()` | public | [src/album.c:239](../src/album.c#L239) |
| `zune_link_artist()` | public | [src/album.c:336](../src/album.c#L336) |
| `zune_get_albums()` | public | [src/album.c:353](../src/album.c#L353) |
| `zune_free_albums()` | public | [src/album.c:450](../src/album.c#L450) |

### src/audiobook.c

| Function | Scope | Definition |
|---|---|---|
| `book_ready()` | static | [src/audiobook.c:7](../src/audiobook.c#L7) |
| `book_text()` | static | [src/audiobook.c:22](../src/audiobook.c#L22) |
| `book_input()` | static | [src/audiobook.c:35](../src/audiobook.c#L35) |
| `book_read_text()` | static | [src/audiobook.c:49](../src/audiobook.c#L49) |
| `book_number()` | static | [src/audiobook.c:65](../src/audiobook.c#L65) |
| `book_inventory()` | static | [src/audiobook.c:80](../src/audiobook.c#L80) |
| `book_folders()` | static | [src/audiobook.c:93](../src/audiobook.c#L93) |
| `zune_find_audiobook_part()` | public | [src/audiobook.c:133](../src/audiobook.c#L133) |
| `zune_smuggle_audiobook_part()` | public | [src/audiobook.c:215](../src/audiobook.c#L215) |

### src/device.c

| Function | Scope | Definition |
|---|---|---|
| `zune_set_error()` | internal | [src/device.c:14](../src/device.c#L14) |
| `zune_get_error()` | public | [src/device.c:21](../src/device.c#L21) |
| `zune_breach()` | public | [src/device.c:54](../src/device.c#L54) |
| `zune_sever()` | public | [src/device.c:323](../src/device.c#L323) |
| `zune_get_name()` | public | [src/device.c:344](../src/device.c#L344) |
| `zune_get_model()` | public | [src/device.c:348](../src/device.c#L348) |
| `zune_get_serial()` | public | [src/device.c:352](../src/device.c#L352) |
| `zune_get_battery()` | public | [src/device.c:356](../src/device.c#L356) |
| `zune_get_capacity()` | public | [src/device.c:360](../src/device.c#L360) |
| `zune_get_headroom()` | public | [src/device.c:364](../src/device.c#L364) |
| `zune_refresh_storage()` | public | [src/device.c:373](../src/device.c#L373) |
| `zune_identify()` | public | [src/device.c:390](../src/device.c#L390) |
| `zune_get_family()` | public | [src/device.c:421](../src/device.c#L421) |
| `zune_is_hdd()` | public | [src/device.c:432](../src/device.c#L432) |
| `zune_is_live()` | public | [src/device.c:440](../src/device.c#L440) |
| `zune_forge_folder()` | public | [src/device.c:449](../src/device.c#L449) |
| `zune_get_folders()` | public | [src/device.c:460](../src/device.c#L460) |
| `zune_free_folders()` | public | [src/device.c:501](../src/device.c#L501) |
| `zune_abort()` | public | [src/device.c:509](../src/device.c#L509) |
| `zune_is_aborted()` | public | [src/device.c:513](../src/device.c#L513) |
| `zune_clear_abort()` | public | [src/device.c:517](../src/device.c#L517) |
| `zune_autopsy()` | public | [src/device.c:523](../src/device.c#L523) |
| `zune_autopsy_name()` | public | [src/device.c:527](../src/device.c#L527) |
| `zune_rename()` | public | [src/device.c:560](../src/device.c#L560) |

### src/driverkit_usb.c

| Function | Scope | Definition |
|---|---|---|
| `get_data()` | static | [src/driverkit_usb.c:63](../src/driverkit_usb.c#L63) |
| `driverkit_open()` | static | [src/driverkit_usb.c:70](../src/driverkit_usb.c#L70) |
| `driverkit_close()` | static | [src/driverkit_usb.c:193](../src/driverkit_usb.c#L193) |
| `driverkit_bulk_read()` | static | [src/driverkit_usb.c:220](../src/driverkit_usb.c#L220) |
| `driverkit_bulk_write()` | static | [src/driverkit_usb.c:268](../src/driverkit_usb.c#L268) |
| `driverkit_clear_halt()` | static | [src/driverkit_usb.c:323](../src/driverkit_usb.c#L323) |
| `driverkit_reset()` | static | [src/driverkit_usb.c:353](../src/driverkit_usb.c#L353) |
| `zune_usb_get_backend()` | internal | [src/driverkit_usb.c:391](../src/driverkit_usb.c#L391) |
| `zune_usb_find_device()` | internal | [src/driverkit_usb.c:398](../src/driverkit_usb.c#L398) |

### src/finalize.c

| Function | Scope | Definition |
|---|---|---|
| `zune_finalize()` | public | [src/finalize.c:12](../src/finalize.c#L12) |
| `zune_rename_item()` | public | [src/finalize.c:92](../src/finalize.c#L92) |
| `zune_sync_notify()` | public | [src/finalize.c:126](../src/finalize.c#L126) |

### src/mtp.c

| Function | Scope | Definition |
|---|---|---|
| `le16_pack()` | static | [src/mtp.c:31](../src/mtp.c#L31) |
| `le32_pack()` | static | [src/mtp.c:37](../src/mtp.c#L37) |
| `le64_pack()` | static | [src/mtp.c:45](../src/mtp.c#L45) |
| `le16_unpack()` | static | [src/mtp.c:51](../src/mtp.c#L51) |
| `le32_unpack()` | static | [src/mtp.c:56](../src/mtp.c#L56) |
| `le64_unpack()` | static | [src/mtp.c:64](../src/mtp.c#L64) |
| `utf8_to_ucs2_chars()` | static | [src/mtp.c:97](../src/mtp.c#L97) |
| `ucs2_char_to_utf8()` | static | [src/mtp.c:192](../src/mtp.c#L192) |
| `mtp_string_to_ucs2()` | internal | [src/mtp.c:209](../src/mtp.c#L209) |
| `mtp_ucs2_to_string()` | internal | [src/mtp.c:275](../src/mtp.c#L275) |
| `mtp_read_string()` | static | [src/mtp.c:336](../src/mtp.c#L336) |
| `mtp_pack_string()` | static | [src/mtp.c:365](../src/mtp.c#L365) |
| `mtp_get_storage_ids()` | internal | [src/mtp.c:387](../src/mtp.c#L387) |
| `mtp_get_storage_info()` | internal | [src/mtp.c:440](../src/mtp.c#L440) |
| `mtp_free_storage_info()` | internal | [src/mtp.c:499](../src/mtp.c#L499) |
| `mtp_get_object_handles()` | internal | [src/mtp.c:512](../src/mtp.c#L512) |
| `mtp_get_object_info()` | internal | [src/mtp.c:578](../src/mtp.c#L578) |
| `mtp_free_object_info()` | internal | [src/mtp.c:653](../src/mtp.c#L653) |
| `mtp_get_object()` | internal | [src/mtp.c:660](../src/mtp.c#L660) |
| `mtp_get_object_to_file()` | internal | [src/mtp.c:685](../src/mtp.c#L685) |
| `mtp_pack_object_info()` | static | [src/mtp.c:731](../src/mtp.c#L731) |
| `mtp_send_object_info()` | internal | [src/mtp.c:787](../src/mtp.c#L787) |
| `mtp_send_object()` | internal | [src/mtp.c:850](../src/mtp.c#L850) |
| `mtp_send_object_from_file()` | internal | [src/mtp.c:869](../src/mtp.c#L869) |
| `mtp_delete_object()` | internal | [src/mtp.c:906](../src/mtp.c#L906) |
| `mtp_get_object_prop_value()` | internal | [src/mtp.c:939](../src/mtp.c#L939) |
| `mtp_set_object_prop_value_u16()` | internal | [src/mtp.c:968](../src/mtp.c#L968) |
| `mtp_set_object_prop_value_u32()` | internal | [src/mtp.c:997](../src/mtp.c#L997) |
| `mtp_set_object_prop_value_str()` | internal | [src/mtp.c:1026](../src/mtp.c#L1026) |
| `mtp_set_object_prop_value_ucs2()` | internal | [src/mtp.c:1061](../src/mtp.c#L1061) |
| `mtp_get_object_prop_list()` | internal | [src/mtp.c:1109](../src/mtp.c#L1109) |
| `mtp_get_object_references()` | internal | [src/mtp.c:1146](../src/mtp.c#L1146) |
| `mtp_set_object_references()` | internal | [src/mtp.c:1203](../src/mtp.c#L1203) |
| `mtp_create_folder()` | internal | [src/mtp.c:1249](../src/mtp.c#L1249) |
| `mtp_get_representative_sample()` | internal | [src/mtp.c:1282](../src/mtp.c#L1282) |
| `mtp_send_representative_sample()` | internal | [src/mtp.c:1332](../src/mtp.c#L1332) |
| `mtp_pack_prop_list()` | static | [src/mtp.c:1408](../src/mtp.c#L1408) |
| `mtp_send_object_prop_list()` | internal | [src/mtp.c:1570](../src/mtp.c#L1570) |
| `mtp_set_object_prop_list()` | internal | [src/mtp.c:1638](../src/mtp.c#L1638) |
| `mtp_get_props_supported()` | internal | [src/mtp.c:1675](../src/mtp.c#L1675) |
| `mtp_get_prop_desc()` | internal | [src/mtp.c:1747](../src/mtp.c#L1747) |
| `mtp_vendor_operation()` | internal | [src/mtp.c:1786](../src/mtp.c#L1786) |

### src/mtp_text.h

| Function | Scope | Definition |
|---|---|---|
| `mtp_text_to_array()` | static | [src/mtp_text.h:11](../src/mtp_text.h#L11) |
| `mtp_array_to_text()` | static | [src/mtp_text.h:52](../src/mtp_text.h#L52) |

### src/mtpz.c

| Function | Scope | Definition |
|---|---|---|
| `fgets_strip()` | static | [src/mtpz.c:54](../src/mtpz.c#L54) |
| `hex_to_bytes()` | static | [src/mtpz.c:74](../src/mtpz.c#L74) |
| `mtpz_load_embedded_keys()` | static | [src/mtpz.c:97](../src/mtpz.c#L97) |
| `mtpz_load_keys_from_file()` | static | [src/mtpz.c:124](../src/mtpz.c#L124) |
| `mtpz_load_keys()` | internal | [src/mtpz.c:202](../src/mtpz.c#L202) |
| `mtpz_free_keys()` | internal | [src/mtpz.c:220](../src/mtpz.c#L220) |
| `mtpz_keys_available()` | internal | [src/mtpz.c:229](../src/mtpz.c#L229) |
| `mtpz_bswap32()` | static | [src/mtpz.c:242](../src/mtpz.c#L242) |
| `mtpz_rsa_init()` | static | [src/mtpz.c:265](../src/mtpz.c#L265) |
| `mtpz_rsa_free()` | static | [src/mtpz.c:293](../src/mtpz.c#L293) |
| `mtpz_rsa_decrypt()` | static | [src/mtpz.c:300](../src/mtpz.c#L300) |
| `mtpz_rsa_sign()` | static | [src/mtpz.c:339](../src/mtpz.c#L339) |
| `mtpz_hash_init_state()` | static | [src/mtpz.c:359](../src/mtpz.c#L359) |
| `mtpz_hash_reset_state()` | static | [src/mtpz.c:367](../src/mtpz.c#L367) |
| `mtpz_hash_transform_hash()` | static | [src/mtpz.c:383](../src/mtpz.c#L383) |
| `mtpz_hash_finalize_hash()` | static | [src/mtpz.c:423](../src/mtpz.c#L423) |
| `mtpz_hash_custom6A5DC()` | static | [src/mtpz.c:461](../src/mtpz.c#L461) |
| `mtpz_hash_compute_hash()` | static | [src/mtpz.c:485](../src/mtpz.c#L485) |
| `mtpz_hash_f()` | static | [src/mtpz.c:530](../src/mtpz.c#L530) |
| `mtpz_hash_rotate_left()` | static | [src/mtpz.c:541](../src/mtpz.c#L541) |
| `mtpz_encryption_cipher()` | static | [src/mtpz.c:1072](../src/mtpz.c#L1072) |
| `mtpz_encryption_cipher_advanced()` | static | [src/mtpz.c:1097](../src/mtpz.c#L1097) |
| `mtpz_encryption_expand_key()` | static | [src/mtpz.c:1157](../src/mtpz.c#L1157) |
| `mtpz_encryption_expand_key_inner()` | static | [src/mtpz.c:1190](../src/mtpz.c#L1190) |
| `mtpz_encryption_inv_mix_columns()` | static | [src/mtpz.c:1241](../src/mtpz.c#L1241) |
| `mtpz_encryption_decrypt_custom()` | static | [src/mtpz.c:1257](../src/mtpz.c#L1257) |
| `mtpz_encryption_encrypt_custom()` | static | [src/mtpz.c:1330](../src/mtpz.c#L1330) |
| `mtpz_encryption_encrypt_mac()` | static | [src/mtpz.c:1405](../src/mtpz.c#L1405) |
| `mtpz_encode_mtp_string()` | static | [src/mtpz.c:1814](../src/mtpz.c#L1814) |
| `mtpz_handshake()` | internal | [src/mtpz.c:1842](../src/mtpz.c#L1842) |

### src/photo.c

| Function | Scope | Definition |
|---|---|---|
| `folder_cache_clear()` | static | [src/photo.c:27](../src/photo.c#L27) |
| `folder_cache_insert()` | static | [src/photo.c:38](../src/photo.c#L38) |
| `folder_cache_lookup()` | static | [src/photo.c:47](../src/photo.c#L47) |
| `folder_cache_count()` | static | [src/photo.c:55](../src/photo.c#L55) |
| `zune_cache_folder_names()` | internal | [src/photo.c:68](../src/photo.c#L68) |
| `is_picture_format()` | static | [src/photo.c:103](../src/photo.c#L103) |
| `zune_get_photos()` | public | [src/photo.c:114](../src/photo.c#L114) |
| `zune_free_photos()` | public | [src/photo.c:189](../src/photo.c#L189) |
| `zune_get_dimensions()` | public | [src/photo.c:196](../src/photo.c#L196) |
| `zune_get_photo_albums()` | public | [src/photo.c:235](../src/photo.c#L235) |
| `zune_free_photo_albums()` | public | [src/photo.c:284](../src/photo.c#L284) |
| `shell_escape()` | static | [src/photo.c:292](../src/photo.c#L292) |
| `zune_arm_photo()` | public | [src/photo.c:316](../src/photo.c#L316) |
| `zune_smuggle_photo()` | public | [src/photo.c:362](../src/photo.c#L362) |
| `zune_purge_photo()` | public | [src/photo.c:478](../src/photo.c#L478) |
| `zune_extract_photo()` | public | [src/photo.c:488](../src/photo.c#L488) |

### src/playlist.c

| Function | Scope | Definition |
|---|---|---|
| `pl_prop_supported()` | static | [src/playlist.c:11](../src/playlist.c#L11) |
| `zune_get_playlists()` | public | [src/playlist.c:20](../src/playlist.c#L20) |
| `zune_free_playlists()` | public | [src/playlist.c:112](../src/playlist.c#L112) |
| `zune_forge_playlist()` | public | [src/playlist.c:138](../src/playlist.c#L138) |
| `zune_rewire_playlist()` | public | [src/playlist.c:210](../src/playlist.c#L210) |
| `zune_purge_playlist()` | public | [src/playlist.c:240](../src/playlist.c#L240) |

### src/podcast.c

| Function | Scope | Definition |
|---|---|---|
| `podcast_ready()` | static | [src/podcast.c:16](../src/podcast.c#L16) |
| `podcast_text_valid()` | static | [src/podcast.c:24](../src/podcast.c#L24) |
| `podcast_filename_valid()` | static | [src/podcast.c:45](../src/podcast.c#L45) |
| `podcast_read_text()` | static | [src/podcast.c:55](../src/podcast.c#L55) |
| `zune_free_podcast_series()` | public | [src/podcast.c:85](../src/podcast.c#L85) |
| `zune_get_podcast_series()` | public | [src/podcast.c:94](../src/podcast.c#L94) |
| `podcast_preflight()` | static | [src/podcast.c:123](../src/podcast.c#L123) |
| `podcast_date_readback()` | static | [src/podcast.c:149](../src/podcast.c#L149) |
| `podcast_title_filename()` | static | [src/podcast.c:167](../src/podcast.c#L167) |
| `podcast_log_filename()` | static | [src/podcast.c:178](../src/podcast.c#L178) |
| `podcast_verify_properties()` | static | [src/podcast.c:193](../src/podcast.c#L193) |
| `podcast_cleanup()` | static | [src/podcast.c:265](../src/podcast.c#L265) |
| `zune_forge_podcast_series()` | public | [src/podcast.c:278](../src/podcast.c#L278) |
| `podcast_date_valid()` | static | [src/podcast.c:314](../src/podcast.c#L314) |
| `podcast_send_file()` | static | [src/podcast.c:333](../src/podcast.c#L333) |
| `zune_smuggle_podcast_episode()` | public | [src/podcast.c:349](../src/podcast.c#L349) |
| `zune_find_podcast_episode()` | public | [src/podcast.c:438](../src/podcast.c#L438) |
| `podcast_series_by_id()` | static | [src/podcast.c:495](../src/podcast.c#L495) |
| `podcast_series_by_feed()` | static | [src/podcast.c:501](../src/podcast.c#L501) |
| `podcast_source_by_identity()` | static | [src/podcast.c:506](../src/podcast.c#L506) |
| `podcast_source_by_id()` | static | [src/podcast.c:512](../src/podcast.c#L512) |
| `podcast_u32_order()` | static | [src/podcast.c:518](../src/podcast.c#L518) |
| `zune_free_podcast_episode_sources()` | public | [src/podcast.c:523](../src/podcast.c#L523) |
| `zune_get_podcast_episode_sources()` | public | [src/podcast.c:530](../src/podcast.c#L530) |

### src/podcast_state.c

| Function | Scope | Definition |
|---|---|---|
| `state_u16()` | static | [src/podcast_state.c:12](../src/podcast_state.c#L12) |
| `state_u32()` | static | [src/podcast_state.c:14](../src/podcast_state.c#L14) |
| `state_by_id()` | static | [src/podcast_state.c:18](../src/podcast_state.c#L18) |
| `state_by_offset()` | static | [src/podcast_state.c:23](../src/podcast_state.c#L23) |
| `state_utf8()` | static | [src/podcast_state.c:31](../src/podcast_state.c#L31) |
| `zune_parse_podcast_states()` | internal | [src/podcast_state.c:51](../src/podcast_state.c#L51) |
| `zune_free_podcast_states()` | public | [src/podcast_state.c:148](../src/podcast_state.c#L148) |
| `zune_get_podcast_states()` | public | [src/podcast_state.c:155](../src/podcast_state.c#L155) |

### src/ptp.c

| Function | Scope | Definition |
|---|---|---|
| `le16_pack()` | static | [src/ptp.c:52](../src/ptp.c#L52) |
| `le32_pack()` | static | [src/ptp.c:58](../src/ptp.c#L58) |
| `le16_unpack()` | static | [src/ptp.c:66](../src/ptp.c#L66) |
| `le32_unpack()` | static | [src/ptp.c:71](../src/ptp.c#L71) |
| `ptp_usb_write_timeout()` | static | [src/ptp.c:90](../src/ptp.c#L90) |
| `ptp_usb_write()` | static | [src/ptp.c:109](../src/ptp.c#L109) |
| `ptp_usb_read_timeout()` | static | [src/ptp.c:122](../src/ptp.c#L122) |
| `ptp_usb_read()` | static | [src/ptp.c:139](../src/ptp.c#L139) |
| `ptp_send_command()` | static | [src/ptp.c:161](../src/ptp.c#L161) |
| `ptp_send_data()` | static | [src/ptp.c:215](../src/ptp.c#L215) |
| `ptp_recv_data()` | static | [src/ptp.c:411](../src/ptp.c#L411) |
| `ptp_recv_response_timeout()` | static | [src/ptp.c:562](../src/ptp.c#L562) |
| `ptp_recv_response()` | static | [src/ptp.c:647](../src/ptp.c#L647) |
| `ptp_session_init()` | internal | [src/ptp.c:665](../src/ptp.c#L665) |
| `ptp_open_session()` | internal | [src/ptp.c:684](../src/ptp.c#L684) |
| `ptp_close_session()` | internal | [src/ptp.c:747](../src/ptp.c#L747) |
| `ptp_transaction()` | internal | [src/ptp.c:795](../src/ptp.c#L795) |
| `ptp_get_device_prop_value()` | internal | [src/ptp.c:892](../src/ptp.c#L892) |
| `ptp_set_device_prop_value()` | internal | [src/ptp.c:921](../src/ptp.c#L921) |

### src/search.c

| Function | Scope | Definition |
|---|---|---|
| `zune_find_track()` | public | [src/search.c:14](../src/search.c#L14) |
| `zune_find_video()` | public | [src/search.c:39](../src/search.c#L39) |
| `zune_find_photo()` | public | [src/search.c:55](../src/search.c#L55) |

### src/spoken_inventory.c

| Function | Scope | Definition |
|---|---|---|
| `spoken_ready()` | static | [src/spoken_inventory.c:8](../src/spoken_inventory.c#L8) |
| `spoken_compare_id()` | static | [src/spoken_inventory.c:22](../src/spoken_inventory.c#L22) |
| `spoken_handles()` | static | [src/spoken_inventory.c:28](../src/spoken_inventory.c#L28) |
| `spoken_text()` | static | [src/spoken_inventory.c:44](../src/spoken_inventory.c#L44) |
| `spoken_number()` | static | [src/spoken_inventory.c:64](../src/spoken_inventory.c#L64) |
| `spoken_info()` | static | [src/spoken_inventory.c:75](../src/spoken_inventory.c#L75) |
| `zune_free_podcast_episodes()` | public | [src/spoken_inventory.c:89](../src/spoken_inventory.c#L89) |
| `zune_get_podcast_episodes()` | public | [src/spoken_inventory.c:98](../src/spoken_inventory.c#L98) |
| `zune_free_audiobook_parts()` | public | [src/spoken_inventory.c:130](../src/spoken_inventory.c#L130) |
| `zune_get_audiobook_parts()` | public | [src/spoken_inventory.c:139](../src/spoken_inventory.c#L139) |

### src/thumbnail.c

| Function | Scope | Definition |
|---|---|---|
| `zune_brand()` | public | [src/thumbnail.c:16](../src/thumbnail.c#L16) |
| `zune_grab_thumb()` | public | [src/thumbnail.c:35](../src/thumbnail.c#L35) |
| `zune_grab_photo_thumb()` | public | [src/thumbnail.c:68](../src/thumbnail.c#L68) |

### src/track.c

| Function | Scope | Definition |
|---|---|---|
| `read_string_prop()` | static | [src/track.c:13](../src/track.c#L13) |
| `read_u32_prop()` | static | [src/track.c:30](../src/track.c#L30) |
| `read_u16_prop()` | static | [src/track.c:49](../src/track.c#L49) |
| `zune_get_tracks()` | public | [src/track.c:66](../src/track.c#L66) |
| `zune_free_tracks()` | public | [src/track.c:183](../src/track.c#L183) |
| `get_file_size()` | static | [src/track.c:199](../src/track.c#L199) |
| `filename_no_ext()` | static | [src/track.c:210](../src/track.c#L210) |
| `format_from_ext()` | static | [src/track.c:230](../src/track.c#L230) |
| `basename_of()` | static | [src/track.c:241](../src/track.c#L241) |
| `send_track_internal()` | static | [src/track.c:250](../src/track.c#L250) |
| `zune_smuggle_track()` | public | [src/track.c:366](../src/track.c#L366) |
| `zune_smuggle_track_tagged()` | public | [src/track.c:384](../src/track.c#L384) |
| `zune_smuggle_track_ex()` | public | [src/track.c:404](../src/track.c#L404) |
| `zune_purge_track()` | public | [src/track.c:443](../src/track.c#L443) |
| `zune_extract_track()` | public | [src/track.c:459](../src/track.c#L459) |
| `zune_verify()` | public | [src/track.c:476](../src/track.c#L476) |
| `zune_get_track_state()` | public | [src/track.c:541](../src/track.c#L541) |
| `zune_set_track_state()` | public | [src/track.c:583](../src/track.c#L583) |
| `zune_probe_object()` | public | [src/track.c:620](../src/track.c#L620) |
| `zune_probe_object_named()` | public | [src/track.c:627](../src/track.c#L627) |
| `zune_get_item_refs()` | public | [src/track.c:648](../src/track.c#L648) |

### src/transcode.c

| Function | Scope | Definition |
|---|---|---|
| `shell_escape()` | static | [src/transcode.c:19](../src/transcode.c#L19) |
| `filename_no_ext()` | static | [src/transcode.c:44](../src/transcode.c#L44) |
| `zune_arm_audio()` | public | [src/transcode.c:63](../src/transcode.c#L63) |
| `zune_retag()` | public | [src/transcode.c:127](../src/transcode.c#L127) |
| `model_to_profile()` | static | [src/transcode.c:222](../src/transcode.c#L222) |
| `transcode_video_impl()` | static | [src/transcode.c:235](../src/transcode.c#L235) |
| `zune_arm_video()` | public | [src/transcode.c:308](../src/transcode.c#L308) |
| `zune_arm_episode()` | public | [src/transcode.c:313](../src/transcode.c#L313) |

### src/usb_libusb.c

| Function | Scope | Definition |
|---|---|---|
| `get_data()` | static | [src/usb_libusb.c:20](../src/usb_libusb.c#L20) |
| `libusb_backend_open()` | static | [src/usb_libusb.c:24](../src/usb_libusb.c#L24) |
| `libusb_backend_close()` | static | [src/usb_libusb.c:99](../src/usb_libusb.c#L99) |
| `libusb_backend_bulk_read()` | static | [src/usb_libusb.c:114](../src/usb_libusb.c#L114) |
| `libusb_backend_bulk_write()` | static | [src/usb_libusb.c:141](../src/usb_libusb.c#L141) |
| `libusb_backend_clear_halt()` | static | [src/usb_libusb.c:159](../src/usb_libusb.c#L159) |
| `libusb_backend_reset()` | static | [src/usb_libusb.c:165](../src/usb_libusb.c#L165) |
| `zune_usb_get_backend()` | internal | [src/usb_libusb.c:180](../src/usb_libusb.c#L180) |
| `zune_usb_find_device()` | internal | [src/usb_libusb.c:184](../src/usb_libusb.c#L184) |

### src/usb_recovery.c

| Function | Scope | Definition |
|---|---|---|
| `zune_unjam()` | public | [src/usb_recovery.c:13](../src/usb_recovery.c#L13) |

### src/util.c

| Function | Scope | Definition |
|---|---|---|
| `path_get_dirname()` | static | [src/util.c:27](../src/util.c#L27) |
| `path_get_basename()` | static | [src/util.c:46](../src/util.c#L46) |
| `strip_trim()` | static | [src/util.c:63](../src/util.c#L63) |
| `clean_series_name()` | static | [src/util.c:84](../src/util.c#L84) |
| `zune_decode_filename()` | public | [src/util.c:120](../src/util.c#L120) |
| `zune_snap_thumb()` | public | [src/util.c:369](../src/util.c#L369) |
| `probe_space()` | static | [src/util.c:411](../src/util.c#L411) |
| `probe_trim()` | static | [src/util.c:427](../src/util.c#L427) |
| `probe_tags_free()` | static | [src/util.c:450](../src/util.c#L450) |
| `probe_number()` | static | [src/util.c:456](../src/util.c#L456) |
| `probe_two_digits()` | static | [src/util.c:476](../src/util.c#L476) |
| `probe_year()` | static | [src/util.c:486](../src/util.c#L486) |
| `probe_text()` | static | [src/util.c:536](../src/util.c#L536) |
| `probe_numeric()` | static | [src/util.c:546](../src/util.c#L546) |
| `probe_fill()` | static | [src/util.c:557](../src/util.c#L557) |
| `probe_duration()` | static | [src/util.c:575](../src/util.c#L575) |
| `probe_dictionary()` | static | [src/util.c:586](../src/util.c#L586) |
| `probe_selected_stream()` | static | [src/util.c:598](../src/util.c#L598) |
| `zune_probe()` | public | [src/util.c:623](../src/util.c#L623) |
| `probe_flat_value()` | static | [src/util.c:663](../src/util.c#L663) |
| `probe_flat_tag()` | static | [src/util.c:695](../src/util.c#L695) |
| `probe_flat_duration()` | static | [src/util.c:704](../src/util.c#L704) |
| `zune_probe()` | public | [src/util.c:712](../src/util.c#L712) |
| `zune_free_metadata()` | public | [src/util.c:821](../src/util.c#L821) |

### src/video.c

| Function | Scope | Definition |
|---|---|---|
| `simple_utf8_to_ucs2()` | static | [src/video.c:22](../src/video.c#L22) |
| `is_video_format()` | static | [src/video.c:52](../src/video.c#L52) |
| `format_from_extension()` | static | [src/video.c:62](../src/video.c#L62) |
| `send_representative_sample()` | static | [src/video.c:77](../src/video.c#L77) |
| `set_video_metadata()` | static | [src/video.c:107](../src/video.c#L107) |
| `set_video_series()` | static | [src/video.c:146](../src/video.c#L146) |
| `read_video_string()` | static | [src/video.c:164](../src/video.c#L164) |
| `send_video_file()` | static | [src/video.c:180](../src/video.c#L180) |
| `zune_get_videos()` | public | [src/video.c:268](../src/video.c#L268) |
| `zune_free_videos()` | public | [src/video.c:386](../src/video.c#L386) |
| `zune_smuggle_video_named()` | public | [src/video.c:396](../src/video.c#L396) |
| `zune_smuggle_movie()` | public | [src/video.c:440](../src/video.c#L440) |
| `zune_get_series_info()` | public | [src/video.c:463](../src/video.c#L463) |
| `zune_smuggle_episode()` | public | [src/video.c:527](../src/video.c#L527) |
| `zune_smuggle_clip()` | public | [src/video.c:571](../src/video.c#L571) |
| `zune_smuggle_other()` | public | [src/video.c:594](../src/video.c#L594) |
| `zune_purge_video()` | public | [src/video.c:616](../src/video.c#L616) |
| `zune_extract_video()` | public | [src/video.c:626](../src/video.c#L626) |

### src/zmdb.c

| Function | Scope | Definition |
|---|---|---|
| `rd16()` | static | [src/zmdb.c:64](../src/zmdb.c#L64) |
| `rd32()` | static | [src/zmdb.c:68](../src/zmdb.c#L68) |
| `rdi32()` | static | [src/zmdb.c:72](../src/zmdb.c#L72) |
| `rd64()` | static | [src/zmdb.c:76](../src/zmdb.c#L76) |
| `read_utf8()` | static | [src/zmdb.c:81](../src/zmdb.c#L81) |
| `zmdb_read_record()` | static | [src/zmdb.c:127](../src/zmdb.c#L127) |
| `zmdb_lookup()` | static | [src/zmdb.c:142](../src/zmdb.c#L142) |
| `zmdb_resolve_string()` | static | [src/zmdb.c:152](../src/zmdb.c#L152) |
| `zmdb_parse_varint_fields()` | static | [src/zmdb.c:210](../src/zmdb.c#L210) |
| `zmdb_parse_track()` | static | [src/zmdb.c:280](../src/zmdb.c#L280) |
| `zmdb_parse_video()` | static | [src/zmdb.c:355](../src/zmdb.c#L355) |
| `zmdb_parse_picture()` | static | [src/zmdb.c:424](../src/zmdb.c#L424) |
| `zmdb_parse_album()` | static | [src/zmdb.c:448](../src/zmdb.c#L448) |
| `zmdb_parse_artist()` | static | [src/zmdb.c:481](../src/zmdb.c#L481) |
| `zmdb_parse_photo_album()` | static | [src/zmdb.c:502](../src/zmdb.c#L502) |
| `zmdb_discover()` | static | [src/zmdb.c:544](../src/zmdb.c#L544) |
| `zmdb_parse()` | static | [src/zmdb.c:574](../src/zmdb.c#L574) |
| `zune_infiltrate_legacy()` | public | [src/zmdb.c:719](../src/zmdb.c#L719) |
| `zmdb_parse_full()` | static | [src/zmdb.c:747](../src/zmdb.c#L747) |
| `zune_infiltrate()` | public | [src/zmdb.c:903](../src/zmdb.c#L903) |
| `zune_free_scan()` | public | [src/zmdb.c:942](../src/zmdb.c#L942) |
| `zune_dump_raw()` | public | [src/zmdb.c:988](../src/zmdb.c#L988) |
| `zune_infiltrate_deep()` | public | [src/zmdb.c:1063](../src/zmdb.c#L1063) |
| `zune_free_library()` | public | [src/zmdb.c:1195](../src/zmdb.c#L1195) |

### src/zmdb_io.c

| Function | Scope | Definition |
|---|---|---|
| `rd16()` | static | [src/zmdb_io.c:7](../src/zmdb_io.c#L7) |
| `rd32()` | static | [src/zmdb_io.c:8](../src/zmdb_io.c#L8) |
| `zune_zmdb_read_payload()` | internal | [src/zmdb_io.c:10](../src/zmdb_io.c#L10) |

### src/zune_internal.h

| Function | Scope | Definition |
|---|---|---|
| `zune_wound_is_mortal()` | static | [src/zune_internal.h:76](../src/zune_internal.h#L76) |

## Headers, types and constants

| Header | Contents |
|---|---|
| [include/zune.h](../include/zune.h) | Public functions, opaque device handle, models/families, metadata, media records, database scan types and progress callback. |
| [src/zune_internal.h](../src/zune_internal.h) | Device state, shared internal helpers and caches. |
| [src/usb.h](../src/usb.h) | USB backend vtable, device identifiers, endpoints and transport abstraction. |
| [src/ptp.h](../src/ptp.h) | PTP sessions, containers, response codes, MTP/vendor operation and property constants. |
| [src/mtp.h](../src/mtp.h) | MTP storage/object/property data structures and operation declarations. |
| [src/mtpz.h](../src/mtpz.h) | MTPZ key-loading and authentication declarations. |

Optional `src/mtpz_keys.h` supplies embedded authentication material. It is not
part of this generated index or required for a credential-free library build.
See [PUBLIC_CREDENTIALS.md](PUBLIC_CREDENTIALS.md) for the runtime override.

## Source inventory

| File | Lines |
|---|---|
| [include/zune.h](../include/zune.h) | 812 |
| [src/album.c](../src/album.c) | 458 |
| [src/audiobook.c](../src/audiobook.c) | 346 |
| [src/device.c](../src/device.c) | 585 |
| [src/driverkit_usb.c](../src/driverkit_usb.c) | 434 |
| [src/finalize.c](../src/finalize.c) | 183 |
| [src/mtp.c](../src/mtp.c) | 1813 |
| [src/mtp.h](../src/mtp.h) | 588 |
| [src/mtp_text.h](../src/mtp_text.h) | 84 |
| [src/mtpz.c](../src/mtpz.c) | 1972 |
| [src/mtpz.h](../src/mtpz.h) | 39 |
| [src/photo.c](../src/photo.c) | 497 |
| [src/playlist.c](../src/playlist.c) | 252 |
| [src/podcast.c](../src/podcast.c) | 639 |
| [src/podcast_state.c](../src/podcast_state.c) | 180 |
| [src/ptp.c](../src/ptp.c) | 940 |
| [src/ptp.h](../src/ptp.h) | 317 |
| [src/search.c](../src/search.c) | 67 |
| [src/spoken_inventory.c](../src/spoken_inventory.c) | 168 |
| [src/thumbnail.c](../src/thumbnail.c) | 128 |
| [src/track.c](../src/track.c) | 654 |
| [src/transcode.c](../src/transcode.c) | 317 |
| [src/usb.h](../src/usb.h) | 135 |
| [src/usb_libusb.c](../src/usb_libusb.c) | 197 |
| [src/usb_recovery.c](../src/usb_recovery.c) | 40 |
| [src/util.c](../src/util.c) | 826 |
| [src/video.c](../src/video.c) | 635 |
| [src/zmdb.c](../src/zmdb.c) | 1213 |
| [src/zmdb_io.c](../src/zmdb_io.c) | 127 |
| [src/zune_internal.h](../src/zune_internal.h) | 122 |

## Integration examples

- [examples/README.md](../examples/README.md)
- [examples/audiobook_send.c](../examples/audiobook_send.c)
- [examples/common.h](../examples/common.h)
- [examples/device_info.c](../examples/device_info.c)
- [examples/podcast_send.c](../examples/podcast_send.c)
- [examples/spoken_inventory.c](../examples/spoken_inventory.c)

## Tools and regression tests

These are development tools, not additional library API. Software tests do not
replace device authentication, transfer and readback tests on real hardware.

- [tools/audiobook_test.h](../tools/audiobook_test.h)
  - `book_test_load()`: [tools/audiobook_test.h:12](../tools/audiobook_test.h#L12)
  - `book_test_inventory()`: [tools/audiobook_test.h:37](../tools/audiobook_test.h#L37)
  - `book_test_verify()`: [tools/audiobook_test.h:46](../tools/audiobook_test.h#L46)
  - `cmd_audiobook_test()`: [tools/audiobook_test.h:79](../tools/audiobook_test.h#L79)
- [tools/inspect-podcast-state.mjs](../tools/inspect-podcast-state.mjs)
- [tools/media_capabilities.h](../tools/media_capabilities.h)
  - `media_probe_format()`: [tools/media_capabilities.h:10](../tools/media_capabilities.h#L10)
  - `media_probe_report()`: [tools/media_capabilities.h:23](../tools/media_capabilities.h#L23)
- [tools/podcast_send.h](../tools/podcast_send.h)
  - `cmd_podcast_send()`: [tools/podcast_send.h:7](../tools/podcast_send.h#L7)
- [tools/podcast_test.h](../tools/podcast_test.h)
  - `podcast_test_interrupt()`: [tools/podcast_test.h:7](../tools/podcast_test.h#L7)
  - `podcast_test_new_id()`: [tools/podcast_test.h:9](../tools/podcast_test.h#L9)
  - `podcast_test_same_file()`: [tools/podcast_test.h:18](../tools/podcast_test.h#L18)
  - `cmd_podcast_test()`: [tools/podcast_test.h:33](../tools/podcast_test.h#L33)
- [tools/ptp-decode.py](../tools/ptp-decode.py)
- [tools/update-api-reference.mjs](../tools/update-api-reference.mjs)
- [tools/update-toc.mjs](../tools/update-toc.mjs)
- [tools/zunetool.c](../tools/zunetool.c)
  - `die()`: [tools/zunetool.c:32](../tools/zunetool.c#L32)
  - `connect_or_die()`: [tools/zunetool.c:41](../tools/zunetool.c#L41)
  - `cmd_info()`: [tools/zunetool.c:54](../tools/zunetool.c#L54)
  - `cmd_list()`: [tools/zunetool.c:69](../tools/zunetool.c#L69)
  - `cmd_send()`: [tools/zunetool.c:86](../tools/zunetool.c#L86)
  - `cmd_purge()`: [tools/zunetool.c:108](../tools/zunetool.c#L108)
  - `cmd_torture()`: [tools/zunetool.c:121](../tools/zunetool.c#L121)
  - `interrupt_video_test()`: [tools/zunetool.c:157](../tools/zunetool.c#L157)
  - `video_test_string()`: [tools/zunetool.c:162](../tools/zunetool.c#L162)
  - `video_test_number()`: [tools/zunetool.c:177](../tools/zunetool.c#L177)
  - `video_test_readback()`: [tools/zunetool.c:192](../tools/zunetool.c#L192)
  - `cmd_video_title_test()`: [tools/zunetool.c:213](../tools/zunetool.c#L213)
  - `cmd_audit()`: [tools/zunetool.c:317](../tools/zunetool.c#L317)
  - `cmd_refs()`: [tools/zunetool.c:402](../tools/zunetool.c#L402)
  - `cmd_playlists()`: [tools/zunetool.c:432](../tools/zunetool.c#L432)
  - `cmd_mkplaylist()`: [tools/zunetool.c:449](../tools/zunetool.c#L449)
  - `cmd_zmdbdump()`: [tools/zunetool.c:468](../tools/zunetool.c#L468)
  - `cmd_extract()`: [tools/zunetool.c:477](../tools/zunetool.c#L477)
  - `cmd_media_capabilities()`: [tools/zunetool.c:487](../tools/zunetool.c#L487)
  - `cmd_podcasts()`: [tools/zunetool.c:503](../tools/zunetool.c#L503)
  - `main()`: [tools/zunetool.c:517](../tools/zunetool.c#L517)
- [tests/artist_reuse.c](../tests/artist_reuse.c)
  - `zune_set_error()`: [tests/artist_reuse.c:7](../tests/artist_reuse.c#L7)
  - `zune_autopsy_name()`: [tests/artist_reuse.c:8](../tests/artist_reuse.c#L8)
  - `mtp_get_props_supported()`: [tests/artist_reuse.c:9](../tests/artist_reuse.c#L9)
  - `mtp_get_object_handles()`: [tests/artist_reuse.c:12](../tests/artist_reuse.c#L12)
  - `mtp_get_object_prop_value()`: [tests/artist_reuse.c:19](../tests/artist_reuse.c#L19)
  - `mtp_ucs2_to_string()`: [tests/artist_reuse.c:28](../tests/artist_reuse.c#L28)
  - `mtp_send_object_prop_list()`: [tests/artist_reuse.c:29](../tests/artist_reuse.c#L29)
  - `mtp_send_object()`: [tests/artist_reuse.c:38](../tests/artist_reuse.c#L38)
  - `mtp_delete_object()`: [tests/artist_reuse.c:41](../tests/artist_reuse.c#L41)
  - `check()`: [tests/artist_reuse.c:43](../tests/artist_reuse.c#L43)
  - `main()`: [tests/artist_reuse.c:47](../tests/artist_reuse.c#L47)
- [tests/audiobook-protocol.c](../tests/audiobook-protocol.c)
  - `read32()`: [tests/audiobook-protocol.c:19](../tests/audiobook-protocol.c#L19)
  - `read16()`: [tests/audiobook-protocol.c:22](../tests/audiobook-protocol.c#L22)
  - `put32()`: [tests/audiobook-protocol.c:23](../tests/audiobook-protocol.c#L23)
  - `zune_set_error()`: [tests/audiobook-protocol.c:24](../tests/audiobook-protocol.c#L24)
  - `zune_get_error()`: [tests/audiobook-protocol.c:27](../tests/audiobook-protocol.c#L27)
  - `check()`: [tests/audiobook-protocol.c:28](../tests/audiobook-protocol.c#L28)
  - `object()`: [tests/audiobook-protocol.c:32](../tests/audiobook-protocol.c#L32)
  - `duplicate()`: [tests/audiobook-protocol.c:36](../tests/audiobook-protocol.c#L36)
  - `property()`: [tests/audiobook-protocol.c:45](../tests/audiobook-protocol.c#L45)
  - `reply()`: [tests/audiobook-protocol.c:49](../tests/audiobook-protocol.c#L49)
  - `reset()`: [tests/audiobook-protocol.c:54](../tests/audiobook-protocol.c#L54)
  - `zune_forge_folder()`: [tests/audiobook-protocol.c:65](../tests/audiobook-protocol.c#L65)
  - `type_for()`: [tests/audiobook-protocol.c:73](../tests/audiobook-protocol.c#L73)
  - `ptp_transaction()`: [tests/audiobook-protocol.c:78](../tests/audiobook-protocol.c#L78)
  - `main()`: [tests/audiobook-protocol.c:153](../tests/audiobook-protocol.c#L153)
- [tests/audiobook_gate.c](../tests/audiobook_gate.c)
  - `__wrap_zune_breach()`: [tests/audiobook_gate.c:13](../tests/audiobook_gate.c#L13)
  - `__wrap_mtp_get_object_prop_value()`: [tests/audiobook_gate.c:17](../tests/audiobook_gate.c#L17)
  - `__wrap_mtp_get_object_handles()`: [tests/audiobook_gate.c:28](../tests/audiobook_gate.c#L28)
  - `check()`: [tests/audiobook_gate.c:40](../tests/audiobook_gate.c#L40)
  - `main()`: [tests/audiobook_gate.c:47](../tests/audiobook_gate.c#L47)
- [tests/examples.c](../tests/examples.c)
  - `check()`: [tests/examples.c:28](../tests/examples.c#L28)
  - `reset()`: [tests/examples.c:34](../tests/examples.c#L34)
  - `run()`: [tests/examples.c:35](../tests/examples.c#L35)
  - `writes()`: [tests/examples.c:52](../tests/examples.c#L52)
  - `owned()`: [tests/examples.c:53](../tests/examples.c#L53)
  - `read_failed()`: [tests/examples.c:57](../tests/examples.c#L57)
  - `write_file()`: [tests/examples.c:58](../tests/examples.c#L58)
  - `zune_breach()`: [tests/examples.c:63](../tests/examples.c#L63)
  - `zune_sever()`: [tests/examples.c:64](../tests/examples.c#L64)
  - `zune_get_name()`: [tests/examples.c:65](../tests/examples.c#L65)
  - `zune_get_model()`: [tests/examples.c:66](../tests/examples.c#L66)
  - `zune_get_serial()`: [tests/examples.c:67](../tests/examples.c#L67)
  - `zune_get_battery()`: [tests/examples.c:68](../tests/examples.c#L68)
  - `zune_get_error()`: [tests/examples.c:69](../tests/examples.c#L69)
  - `zune_autopsy()`: [tests/examples.c:70](../tests/examples.c#L70)
  - `zune_get_podcast_series()`: [tests/examples.c:71](../tests/examples.c#L71)
  - `zune_free_podcast_series()`: [tests/examples.c:80](../tests/examples.c#L80)
  - `zune_get_podcast_episodes()`: [tests/examples.c:86](../tests/examples.c#L86)
  - `zune_free_podcast_episodes()`: [tests/examples.c:95](../tests/examples.c#L95)
  - `zune_get_audiobook_parts()`: [tests/examples.c:101](../tests/examples.c#L101)
  - `zune_free_audiobook_parts()`: [tests/examples.c:112](../tests/examples.c#L112)
  - `zune_get_podcast_states()`: [tests/examples.c:118](../tests/examples.c#L118)
  - `zune_free_podcast_states()`: [tests/examples.c:129](../tests/examples.c#L129)
  - `zune_get_podcast_episode_sources()`: [tests/examples.c:135](../tests/examples.c#L135)
  - `zune_free_podcast_episode_sources()`: [tests/examples.c:144](../tests/examples.c#L144)
  - `zune_find_podcast_episode()`: [tests/examples.c:150](../tests/examples.c#L150)
  - `zune_forge_folder()`: [tests/examples.c:159](../tests/examples.c#L159)
  - `zune_forge_podcast_series()`: [tests/examples.c:163](../tests/examples.c#L163)
  - `zune_smuggle_podcast_episode()`: [tests/examples.c:169](../tests/examples.c#L169)
  - `zune_find_audiobook_part()`: [tests/examples.c:176](../tests/examples.c#L176)
  - `zune_smuggle_audiobook_part()`: [tests/examples.c:183](../tests/examples.c#L183)
  - `zune_brand()`: [tests/examples.c:191](../tests/examples.c#L191)
  - `zune_finalize()`: [tests/examples.c:196](../tests/examples.c#L196)
  - `main()`: [tests/examples.c:198](../tests/examples.c#L198)
- [tests/media_capabilities.c](../tests/media_capabilities.c)
  - `ptp_transaction()`: [tests/media_capabilities.c:12](../tests/media_capabilities.c#L12)
  - `check()`: [tests/media_capabilities.c:55](../tests/media_capabilities.c#L55)
  - `report()`: [tests/media_capabilities.c:61](../tests/media_capabilities.c#L61)
  - `main()`: [tests/media_capabilities.c:77](../tests/media_capabilities.c#L77)
- [tests/metadata_probe.c](../tests/metadata_probe.c)
  - `check()`: [tests/metadata_probe.c:7](../tests/metadata_probe.c#L7)
  - `same()`: [tests/metadata_probe.c:13](../tests/metadata_probe.c#L13)
  - `empty()`: [tests/metadata_probe.c:17](../tests/metadata_probe.c#L17)
  - `load()`: [tests/metadata_probe.c:23](../tests/metadata_probe.c#L23)
  - `main()`: [tests/metadata_probe.c:30](../tests/metadata_probe.c#L30)
- [tests/podcast-state.c](../tests/podcast-state.c)
  - `check()`: [tests/podcast-state.c:6](../tests/podcast-state.c#L6)
  - `zune_set_error()`: [tests/podcast-state.c:9](../tests/podcast-state.c#L9)
  - `zune_get_error()`: [tests/podcast-state.c:11](../tests/podcast-state.c#L11)
  - `put32()`: [tests/podcast-state.c:12](../tests/podcast-state.c#L12)
  - `put16()`: [tests/podcast-state.c:14](../tests/podcast-state.c#L14)
  - `fixture()`: [tests/podcast-state.c:18](../tests/podcast-state.c#L18)
  - `parse()`: [tests/podcast-state.c:37](../tests/podcast-state.c#L37)
  - `fake_read()`: [tests/podcast-state.c:48](../tests/podcast-state.c#L48)
  - `fake_write()`: [tests/podcast-state.c:59](../tests/podcast-state.c#L59)
  - `transport_fixture()`: [tests/podcast-state.c:67](../tests/podcast-state.c#L67)
  - `get()`: [tests/podcast-state.c:74](../tests/podcast-state.c#L74)
  - `main()`: [tests/podcast-state.c:83](../tests/podcast-state.c#L83)
- [tests/podcasts.c](../tests/podcasts.c)
  - `read32()`: [tests/podcasts.c:14](../tests/podcasts.c#L14)
  - `read16()`: [tests/podcasts.c:15](../tests/podcasts.c#L15)
  - `put32()`: [tests/podcasts.c:16](../tests/podcasts.c#L16)
  - `check()`: [tests/podcasts.c:17](../tests/podcasts.c#L17)
  - `zune_set_error()`: [tests/podcasts.c:18](../tests/podcasts.c#L18)
  - `zune_get_error()`: [tests/podcasts.c:19](../tests/podcasts.c#L19)
  - `object()`: [tests/podcasts.c:20](../tests/podcasts.c#L20)
  - `prop()`: [tests/podcasts.c:21](../tests/podcasts.c#L21)
  - `reply()`: [tests/podcasts.c:22](../tests/podcasts.c#L22)
  - `reset()`: [tests/podcasts.c:23](../tests/podcasts.c#L23)
  - `duplicate()`: [tests/podcasts.c:28](../tests/podcasts.c#L28)
  - `type_for()`: [tests/podcasts.c:32](../tests/podcasts.c#L32)
  - `ptp_transaction()`: [tests/podcasts.c:39](../tests/podcasts.c#L39)
  - `forge()`: [tests/podcasts.c:120](../tests/podcasts.c#L120)
  - `main()`: [tests/podcasts.c:121](../tests/podcasts.c#L121)
- [tests/run-artist-reuse.sh](../tests/run-artist-reuse.sh)
- [tests/run-audiobook-gate.sh](../tests/run-audiobook-gate.sh)
- [tests/run-audiobook-protocol.sh](../tests/run-audiobook-protocol.sh)
- [tests/run-examples.sh](../tests/run-examples.sh)
- [tests/run-media-capabilities.sh](../tests/run-media-capabilities.sh)
- [tests/run-metadata-probe.sh](../tests/run-metadata-probe.sh)
- [tests/run-podcast-state.sh](../tests/run-podcast-state.sh)
- [tests/run-podcasts.sh](../tests/run-podcasts.sh)
- [tests/run-spoken-inventory.sh](../tests/run-spoken-inventory.sh)
- [tests/run-video-named.sh](../tests/run-video-named.sh)
- [tests/spoken-inventory.c](../tests/spoken-inventory.c)
  - `zune_set_error()`: [tests/spoken-inventory.c:9](../tests/spoken-inventory.c#L9)
  - `check()`: [tests/spoken-inventory.c:12](../tests/spoken-inventory.c#L12)
  - `next()`: [tests/spoken-inventory.c:16](../tests/spoken-inventory.c#L16)
  - `mtp_get_object_handles()`: [tests/spoken-inventory.c:21](../tests/spoken-inventory.c#L21)
  - `mtp_get_object_info()`: [tests/spoken-inventory.c:33](../tests/spoken-inventory.c#L33)
  - `mtp_free_object_info()`: [tests/spoken-inventory.c:40](../tests/spoken-inventory.c#L40)
  - `number()`: [tests/spoken-inventory.c:41](../tests/spoken-inventory.c#L41)
  - `string()`: [tests/spoken-inventory.c:45](../tests/spoken-inventory.c#L45)
  - `mtp_get_object_prop_value()`: [tests/spoken-inventory.c:52](../tests/spoken-inventory.c#L52)
  - `reset()`: [tests/spoken-inventory.c:68](../tests/spoken-inventory.c#L68)
  - `main()`: [tests/spoken-inventory.c:73](../tests/spoken-inventory.c#L73)
- [tests/video_named.c](../tests/video_named.c)
  - `check()`: [tests/video_named.c:20](../tests/video_named.c#L20)
  - `property()`: [tests/video_named.c:26](../tests/video_named.c#L26)
  - `put_property()`: [tests/video_named.c:32](../tests/video_named.c#L32)
  - `put_string()`: [tests/video_named.c:46](../tests/video_named.c#L46)
  - `string_equals()`: [tests/video_named.c:54](../tests/video_named.c#L54)
  - `number_equals()`: [tests/video_named.c:63](../tests/video_named.c#L63)
  - `reset_transport()`: [tests/video_named.c:69](../tests/video_named.c#L69)
  - `copy_reply()`: [tests/video_named.c:80](../tests/video_named.c#L80)
  - `ptp_transaction()`: [tests/video_named.c:89](../tests/video_named.c#L89)
  - `zune_is_aborted()`: [tests/video_named.c:160](../tests/video_named.c#L160)
  - `zune_unjam()`: [tests/video_named.c:161](../tests/video_named.c#L161)
  - `zune_autopsy_name()`: [tests/video_named.c:162](../tests/video_named.c#L162)
  - `zune_set_error()`: [tests/video_named.c:163](../tests/video_named.c#L163)
  - `zune_get_error()`: [tests/video_named.c:169](../tests/video_named.c#L169)
  - `send_named()`: [tests/video_named.c:171](../tests/video_named.c#L171)
  - `main()`: [tests/video_named.c:179](../tests/video_named.c#L179)

## Documentation map

The linked source is authoritative for current signatures. Historical protocol
notes describe the captures and experiments stated in each document; older
architecture/API narratives may describe an earlier implementation.

| Document | Title |
|---|---|
| [ALBUM_HIERARCHY.md](ALBUM_HIERARCHY.md) | Zune MTP Album/Artist Object Hierarchy |
| [API_REFERENCE.md](API_REFERENCE.md) | libzune API reference |
| [ARTIST_REUSE.md](ARTIST_REUSE.md) | Reuse existing artist objects (2026-10-02) |
| [BUILD_WITH_MTPZ.md](BUILD_WITH_MTPZ.md) | Building with Zune authentication |
| [FIRMWARE_FLASH_CAPTURE.md](FIRMWARE_FLASH_CAPTURE.md) | Zune Firmware Flash — USB Capture Guide |
| [LINUX_TESTING.md](LINUX_TESTING.md) | Testing libzune on Linux |
| [MACOS_ZUNE_QUIRKS.md](MACOS_ZUNE_QUIRKS.md) | macOS Zune Quirks — Everything That's Different from Linux |
| [METADATA_PROBE.md](METADATA_PROBE.md) | Metadata probe contract — tester release R2 |
| [MTPZ_PROTOCOL.md](MTPZ_PROTOCOL.md) | MTPZ Authentication Protocol |
| [PROJECT_OVERVIEW.md](PROJECT_OVERVIEW.md) | libzune project overview |
| [PUBLIC_CREDENTIALS.md](PUBLIC_CREDENTIALS.md) | Private and public authentication builds |
| [RELEASE_0.2.0.md](RELEASE_0.2.0.md) | libzune 0.2.0 |
| [SPOKEN_MEDIA.md](SPOKEN_MEDIA.md) | Podcasts and audiobooks: integration guide |
| [SPOKEN_MEDIA_RESEARCH.md](SPOKEN_MEDIA_RESEARCH.md) | Spoken media and HD artist-art research |
| [SYNC_PIPELINE.md](SYNC_PIPELINE.md) | Building a sync pipeline with libzune |
| [VIDEO_TITLES.md](VIDEO_TITLES.md) | Video display titles and transport filenames |
| [WHY_C.md](WHY_C.md) | Why C? |
| [WIRE_CAPTURE_FINDINGS.md](WIRE_CAPTURE_FINDINGS.md) | Wire Capture Findings — Windows 8 Zune Client Ground Truth |
| [ZMDB_DUMP_ANALYSIS.md](ZMDB_DUMP_ANALYSIS.md) | ZMDB Dump Analysis — Zuuned Zune (Classic, ZMed v2) |
| [ZMDB_FORMAT.md](ZMDB_FORMAT.md) | ZMDB Binary Format — Zune Media Database |
| [ZUNEDB_ARCHITECTURE.md](ZUNEDB_ARCHITECTURE.md) | ZuneDB — Unified Device Library Engine |
| [ZUNE_MTP_PROTOCOL_FINDINGS.md](ZUNE_MTP_PROTOCOL_FINDINGS.md) | Zune MTP Protocol — Reverse Engineering Findings |
| [ZUNE_VENDOR_OPS.md](ZUNE_VENDOR_OPS.md) | Zune vendor opcodes — decoded from Windows client captures |
| [ZUNE_VENDOR_PROPERTIES.md](ZUNE_VENDOR_PROPERTIES.md) | Zune Vendor MTP Properties & Operations |
| [ZUUNEDMAC_ARCHITECTURE.md](ZUUNEDMAC_ARCHITECTURE.md) | ZuunedMac — SwiftUI Architecture |
