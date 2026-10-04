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
- [Public authentication setup](PUBLIC_CREDENTIALS.md): embedded-header and external-file behavior.
- [Wire capture findings](WIRE_CAPTURE_FINDINGS.md): observed Windows-client protocol behavior.
- [Linux testing](LINUX_TESTING.md): hardware findings and test workflow.
- [Artist reuse](ARTIST_REUSE.md): `zune_forge_artist` reuses a live matching artist;
  failed inventory reads do not justify creating one, and existing duplicates are not deleted.
- [Metadata probe](METADATA_PROBE.md): native/fallback tags and disc/year behavior.
- [Video titles](VIDEO_TITLES.md): independent display titles and wire filenames.

The macOS DriverKit backend is included in this repository. The corresponding
`ZuneUSBDriver` extension implementation belongs to the consuming macOS project;
there is no `Driver/` source tree here. Linux uses the libusb backend.

## Public API

90 declarations in `include/zune.h`. Consult the linked header for
ownership, return values and threading requirements before calling an operation.

| Function | Declaration | Implementation |
|---|---|---|
| `zune_breach()` | [include/zune.h:56](../include/zune.h#L56) | [src/device.c:54](../src/device.c#L54) |
| `zune_sever()` | [include/zune.h:60](../include/zune.h#L60) | [src/device.c:322](../src/device.c#L322) |
| `zune_get_name()` | [include/zune.h:63](../include/zune.h#L63) | [src/device.c:343](../src/device.c#L343) |
| `zune_get_model()` | [include/zune.h:64](../include/zune.h#L64) | [src/device.c:347](../src/device.c#L347) |
| `zune_get_serial()` | [include/zune.h:65](../include/zune.h#L65) | [src/device.c:351](../src/device.c#L351) |
| `zune_get_battery()` | [include/zune.h:66](../include/zune.h#L66) | [src/device.c:355](../src/device.c#L355) |
| `zune_get_capacity()` | [include/zune.h:67](../include/zune.h#L67) | [src/device.c:359](../src/device.c#L359) |
| `zune_get_headroom()` | [include/zune.h:68](../include/zune.h#L68) | [src/device.c:363](../src/device.c#L363) |
| `zune_refresh_storage()` | [include/zune.h:74](../include/zune.h#L74) | [src/device.c:372](../src/device.c#L372) |
| `zune_identify()` | [include/zune.h:75](../include/zune.h#L75) | [src/device.c:389](../src/device.c#L389) |
| `zune_get_family()` | [include/zune.h:80](../include/zune.h#L80) | [src/device.c:420](../src/device.c#L420) |
| `zune_is_hdd()` | [include/zune.h:84](../include/zune.h#L84) | [src/device.c:431](../src/device.c#L431) |
| `zune_rename()` | [include/zune.h:87](../include/zune.h#L87) | [src/device.c:559](../src/device.c#L559) |
| `zune_is_live()` | [include/zune.h:90](../include/zune.h#L90) | [src/device.c:439](../src/device.c#L439) |
| `zune_abort()` | [include/zune.h:93](../include/zune.h#L93) | [src/device.c:508](../src/device.c#L508) |
| `zune_is_aborted()` | [include/zune.h:96](../include/zune.h#L96) | [src/device.c:512](../src/device.c#L512) |
| `zune_clear_abort()` | [include/zune.h:99](../include/zune.h#L99) | [src/device.c:516](../src/device.c#L516) |
| `zune_autopsy()` | [include/zune.h:106](../include/zune.h#L106) | [src/device.c:522](../src/device.c#L522) |
| `zune_autopsy_name()` | [include/zune.h:109](../include/zune.h#L109) | [src/device.c:526](../src/device.c#L526) |
| `zune_unjam()` | [include/zune.h:114](../include/zune.h#L114) | [src/usb_recovery.c:13](../src/usb_recovery.c#L13) |
| `zune_forge_folder()` | [include/zune.h:118](../include/zune.h#L118) | [src/device.c:448](../src/device.c#L448) |
| `zune_get_folders()` | [include/zune.h:124](../include/zune.h#L124) | [src/device.c:459](../src/device.c#L459) |
| `zune_free_folders()` | [include/zune.h:125](../include/zune.h#L125) | [src/device.c:500](../src/device.c#L500) |
| `zune_get_tracks()` | [include/zune.h:146](../include/zune.h#L146) | [src/track.c:66](../src/track.c#L66) |
| `zune_free_tracks()` | [include/zune.h:147](../include/zune.h#L147) | [src/track.c:183](../src/track.c#L183) |
| `zune_smuggle_track()` | [include/zune.h:151](../include/zune.h#L151) | [src/track.c:366](../src/track.c#L366) |
| `zune_smuggle_track_tagged()` | [include/zune.h:155](../include/zune.h#L155) | [src/track.c:384](../src/track.c#L384) |
| `zune_purge_track()` | [include/zune.h:162](../include/zune.h#L162) | [src/track.c:443](../src/track.c#L443) |
| `zune_extract_track()` | [include/zune.h:165](../include/zune.h#L165) | [src/track.c:459](../src/track.c#L459) |
| `zune_get_track_state()` | [include/zune.h:171](../include/zune.h#L171) | [src/track.c:541](../src/track.c#L541) |
| `zune_set_track_state()` | [include/zune.h:178](../include/zune.h#L178) | [src/track.c:583](../src/track.c#L583) |
| `zune_verify()` | [include/zune.h:186](../include/zune.h#L186) | [src/track.c:476](../src/track.c#L476) |
| `zune_rename_item()` | [include/zune.h:193](../include/zune.h#L193) | [src/finalize.c:92](../src/finalize.c#L92) |
| `zune_sync_notify()` | [include/zune.h:206](../include/zune.h#L206) | [src/finalize.c:126](../src/finalize.c#L126) |
| `zune_probe_object()` | [include/zune.h:214](../include/zune.h#L214) | [src/track.c:620](../src/track.c#L620) |
| `zune_probe_object_named()` | [include/zune.h:218](../include/zune.h#L218) | [src/track.c:627](../src/track.c#L627) |
| `zune_get_item_refs()` | [include/zune.h:224](../include/zune.h#L224) | [src/track.c:648](../src/track.c#L648) |
| `zune_find_track()` | [include/zune.h:232](../include/zune.h#L232) | [src/search.c:14](../src/search.c#L14) |
| `zune_find_video()` | [include/zune.h:238](../include/zune.h#L238) | [src/search.c:39](../src/search.c#L39) |
| `zune_find_photo()` | [include/zune.h:242](../include/zune.h#L242) | [src/search.c:55](../src/search.c#L55) |
| `zune_get_videos()` | [include/zune.h:267](../include/zune.h#L267) | [src/video.c:268](../src/video.c#L268) |
| `zune_free_videos()` | [include/zune.h:268](../include/zune.h#L268) | [src/video.c:386](../src/video.c#L386) |
| `zune_smuggle_video_named()` | [include/zune.h:286](../include/zune.h#L286) | [src/video.c:396](../src/video.c#L396) |
| `zune_smuggle_movie()` | [include/zune.h:298](../include/zune.h#L298) | [src/video.c:440](../src/video.c#L440) |
| `zune_get_series_info()` | [include/zune.h:306](../include/zune.h#L306) | [src/video.c:463](../src/video.c#L463) |
| `zune_smuggle_episode()` | [include/zune.h:313](../include/zune.h#L313) | [src/video.c:527](../src/video.c#L527) |
| `zune_smuggle_clip()` | [include/zune.h:321](../include/zune.h#L321) | [src/video.c:571](../src/video.c#L571) |
| `zune_smuggle_other()` | [include/zune.h:327](../include/zune.h#L327) | [src/video.c:594](../src/video.c#L594) |
| `zune_purge_video()` | [include/zune.h:333](../include/zune.h#L333) | [src/video.c:616](../src/video.c#L616) |
| `zune_extract_video()` | [include/zune.h:336](../include/zune.h#L336) | [src/video.c:626](../src/video.c#L626) |
| `zune_get_photos()` | [include/zune.h:355](../include/zune.h#L355) | [src/photo.c:114](../src/photo.c#L114) |
| `zune_free_photos()` | [include/zune.h:356](../include/zune.h#L356) | [src/photo.c:189](../src/photo.c#L189) |
| `zune_get_dimensions()` | [include/zune.h:360](../include/zune.h#L360) | [src/photo.c:196](../src/photo.c#L196) |
| `zune_get_photo_albums()` | [include/zune.h:364](../include/zune.h#L364) | [src/photo.c:235](../src/photo.c#L235) |
| `zune_free_photo_albums()` | [include/zune.h:367](../include/zune.h#L367) | [src/photo.c:284](../src/photo.c#L284) |
| `zune_arm_photo()` | [include/zune.h:372](../include/zune.h#L372) | [src/photo.c:316](../src/photo.c#L316) |
| `zune_smuggle_photo()` | [include/zune.h:377](../include/zune.h#L377) | [src/photo.c:362](../src/photo.c#L362) |
| `zune_purge_photo()` | [include/zune.h:381](../include/zune.h#L381) | [src/photo.c:478](../src/photo.c#L478) |
| `zune_extract_photo()` | [include/zune.h:384](../include/zune.h#L384) | [src/photo.c:488](../src/photo.c#L488) |
| `zune_get_playlists()` | [include/zune.h:397](../include/zune.h#L397) | [src/playlist.c:20](../src/playlist.c#L20) |
| `zune_free_playlists()` | [include/zune.h:398](../include/zune.h#L398) | [src/playlist.c:112](../src/playlist.c#L112) |
| `zune_forge_playlist()` | [include/zune.h:401](../include/zune.h#L401) | [src/playlist.c:138](../src/playlist.c#L138) |
| `zune_rewire_playlist()` | [include/zune.h:405](../include/zune.h#L405) | [src/playlist.c:210](../src/playlist.c#L210) |
| `zune_purge_playlist()` | [include/zune.h:410](../include/zune.h#L410) | [src/playlist.c:240](../src/playlist.c#L240) |
| `zune_forge_album()` | [include/zune.h:421](../include/zune.h#L421) | [src/album.c:24](../src/album.c#L24) |
| `zune_rewire_album()` | [include/zune.h:428](../include/zune.h#L428) | [src/album.c:138](../src/album.c#L138) |
| `zune_forge_artist()` | [include/zune.h:438](../include/zune.h#L438) | [src/album.c:239](../src/album.c#L239) |
| `zune_link_artist()` | [include/zune.h:442](../include/zune.h#L442) | [src/album.c:336](../src/album.c#L336) |
| `zune_get_albums()` | [include/zune.h:454](../include/zune.h#L454) | [src/album.c:353](../src/album.c#L353) |
| `zune_free_albums()` | [include/zune.h:455](../include/zune.h#L455) | [src/album.c:450](../src/album.c#L450) |
| `zune_brand()` | [include/zune.h:461](../include/zune.h#L461) | [src/thumbnail.c:16](../src/thumbnail.c#L16) |
| `zune_grab_thumb()` | [include/zune.h:467](../include/zune.h#L467) | [src/thumbnail.c:35](../src/thumbnail.c#L35) |
| `zune_grab_photo_thumb()` | [include/zune.h:473](../include/zune.h#L473) | [src/thumbnail.c:68](../src/thumbnail.c#L68) |
| `zune_arm_audio()` | [include/zune.h:480](../include/zune.h#L480) | [src/transcode.c:63](../src/transcode.c#L63) |
| `zune_retag()` | [include/zune.h:485](../include/zune.h#L485) | [src/transcode.c:127](../src/transcode.c#L127) |
| `zune_arm_video()` | [include/zune.h:490](../include/zune.h#L490) | [src/transcode.c:308](../src/transcode.c#L308) |
| `zune_arm_episode()` | [include/zune.h:494](../include/zune.h#L494) | [src/transcode.c:313](../src/transcode.c#L313) |
| `zune_infiltrate_legacy()` | [include/zune.h:510](../include/zune.h#L510) | [src/zmdb.c:843](../src/zmdb.c#L843) |
| `zune_free_library()` | [include/zune.h:511](../include/zune.h#L511) | [src/zmdb.c:1319](../src/zmdb.c#L1319) |
| `zune_finalize()` | [include/zune.h:518](../include/zune.h#L518) | [src/finalize.c:12](../src/finalize.c#L12) |
| `zune_decode_filename()` | [include/zune.h:525](../include/zune.h#L525) | [src/util.c:120](../src/util.c#L120) |
| `zune_snap_thumb()` | [include/zune.h:531](../include/zune.h#L531) | [src/util.c:369](../src/util.c#L369) |
| `zune_probe()` | [include/zune.h:555](../include/zune.h#L555) | [src/util.c:623](../src/util.c#L623)<br>[src/util.c:712](../src/util.c#L712) |
| `zune_free_metadata()` | [include/zune.h:556](../include/zune.h#L556) | [src/util.c:821](../src/util.c#L821) |
| `zune_smuggle_track_ex()` | [include/zune.h:565](../include/zune.h#L565) | [src/track.c:404](../src/track.c#L404) |
| `zune_infiltrate()` | [include/zune.h:612](../include/zune.h#L612) | [src/zmdb.c:1027](../src/zmdb.c#L1027) |
| `zune_free_scan()` | [include/zune.h:613](../include/zune.h#L613) | [src/zmdb.c:1066](../src/zmdb.c#L1066) |
| `zune_dump_raw()` | [include/zune.h:619](../include/zune.h#L619) | [src/zmdb.c:1112](../src/zmdb.c#L1112) |
| `zune_infiltrate_deep()` | [include/zune.h:624](../include/zune.h#L624) | [src/zmdb.c:1187](../src/zmdb.c#L1187) |
| `zune_get_error()` | [include/zune.h:629](../include/zune.h#L629) | [src/device.c:21](../src/device.c#L21) |

## Library definitions

271 function definitions, including conditional alternatives. `public` means
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

### src/device.c

| Function | Scope | Definition |
|---|---|---|
| `zune_set_error()` | internal | [src/device.c:14](../src/device.c#L14) |
| `zune_get_error()` | public | [src/device.c:21](../src/device.c#L21) |
| `zune_breach()` | public | [src/device.c:54](../src/device.c#L54) |
| `zune_sever()` | public | [src/device.c:322](../src/device.c#L322) |
| `zune_get_name()` | public | [src/device.c:343](../src/device.c#L343) |
| `zune_get_model()` | public | [src/device.c:347](../src/device.c#L347) |
| `zune_get_serial()` | public | [src/device.c:351](../src/device.c#L351) |
| `zune_get_battery()` | public | [src/device.c:355](../src/device.c#L355) |
| `zune_get_capacity()` | public | [src/device.c:359](../src/device.c#L359) |
| `zune_get_headroom()` | public | [src/device.c:363](../src/device.c#L363) |
| `zune_refresh_storage()` | public | [src/device.c:372](../src/device.c#L372) |
| `zune_identify()` | public | [src/device.c:389](../src/device.c#L389) |
| `zune_get_family()` | public | [src/device.c:420](../src/device.c#L420) |
| `zune_is_hdd()` | public | [src/device.c:431](../src/device.c#L431) |
| `zune_is_live()` | public | [src/device.c:439](../src/device.c#L439) |
| `zune_forge_folder()` | public | [src/device.c:448](../src/device.c#L448) |
| `zune_get_folders()` | public | [src/device.c:459](../src/device.c#L459) |
| `zune_free_folders()` | public | [src/device.c:500](../src/device.c#L500) |
| `zune_abort()` | public | [src/device.c:508](../src/device.c#L508) |
| `zune_is_aborted()` | public | [src/device.c:512](../src/device.c#L512) |
| `zune_clear_abort()` | public | [src/device.c:516](../src/device.c#L516) |
| `zune_autopsy()` | public | [src/device.c:522](../src/device.c#L522) |
| `zune_autopsy_name()` | public | [src/device.c:526](../src/device.c#L526) |
| `zune_rename()` | public | [src/device.c:559](../src/device.c#L559) |

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
| `le16_pack()` | static | [src/mtp.c:30](../src/mtp.c#L30) |
| `le32_pack()` | static | [src/mtp.c:36](../src/mtp.c#L36) |
| `le64_pack()` | static | [src/mtp.c:44](../src/mtp.c#L44) |
| `le16_unpack()` | static | [src/mtp.c:50](../src/mtp.c#L50) |
| `le32_unpack()` | static | [src/mtp.c:55](../src/mtp.c#L55) |
| `le64_unpack()` | static | [src/mtp.c:63](../src/mtp.c#L63) |
| `utf8_to_ucs2_chars()` | static | [src/mtp.c:96](../src/mtp.c#L96) |
| `ucs2_char_to_utf8()` | static | [src/mtp.c:191](../src/mtp.c#L191) |
| `mtp_string_to_ucs2()` | internal | [src/mtp.c:208](../src/mtp.c#L208) |
| `mtp_ucs2_to_string()` | internal | [src/mtp.c:274](../src/mtp.c#L274) |
| `mtp_read_string()` | static | [src/mtp.c:335](../src/mtp.c#L335) |
| `mtp_pack_string()` | static | [src/mtp.c:364](../src/mtp.c#L364) |
| `mtp_get_storage_ids()` | internal | [src/mtp.c:386](../src/mtp.c#L386) |
| `mtp_get_storage_info()` | internal | [src/mtp.c:439](../src/mtp.c#L439) |
| `mtp_free_storage_info()` | internal | [src/mtp.c:498](../src/mtp.c#L498) |
| `mtp_get_object_handles()` | internal | [src/mtp.c:511](../src/mtp.c#L511) |
| `mtp_get_object_info()` | internal | [src/mtp.c:573](../src/mtp.c#L573) |
| `mtp_free_object_info()` | internal | [src/mtp.c:648](../src/mtp.c#L648) |
| `mtp_get_object()` | internal | [src/mtp.c:655](../src/mtp.c#L655) |
| `mtp_get_object_to_file()` | internal | [src/mtp.c:680](../src/mtp.c#L680) |
| `mtp_pack_object_info()` | static | [src/mtp.c:726](../src/mtp.c#L726) |
| `mtp_send_object_info()` | internal | [src/mtp.c:782](../src/mtp.c#L782) |
| `mtp_send_object()` | internal | [src/mtp.c:845](../src/mtp.c#L845) |
| `mtp_send_object_from_file()` | internal | [src/mtp.c:864](../src/mtp.c#L864) |
| `mtp_delete_object()` | internal | [src/mtp.c:901](../src/mtp.c#L901) |
| `mtp_get_object_prop_value()` | internal | [src/mtp.c:934](../src/mtp.c#L934) |
| `mtp_set_object_prop_value_u16()` | internal | [src/mtp.c:963](../src/mtp.c#L963) |
| `mtp_set_object_prop_value_u32()` | internal | [src/mtp.c:992](../src/mtp.c#L992) |
| `mtp_set_object_prop_value_str()` | internal | [src/mtp.c:1021](../src/mtp.c#L1021) |
| `mtp_set_object_prop_value_ucs2()` | internal | [src/mtp.c:1056](../src/mtp.c#L1056) |
| `mtp_get_object_prop_list()` | internal | [src/mtp.c:1104](../src/mtp.c#L1104) |
| `mtp_get_object_references()` | internal | [src/mtp.c:1141](../src/mtp.c#L1141) |
| `mtp_set_object_references()` | internal | [src/mtp.c:1198](../src/mtp.c#L1198) |
| `mtp_create_folder()` | internal | [src/mtp.c:1244](../src/mtp.c#L1244) |
| `mtp_get_representative_sample()` | internal | [src/mtp.c:1277](../src/mtp.c#L1277) |
| `mtp_send_representative_sample()` | internal | [src/mtp.c:1327](../src/mtp.c#L1327) |
| `mtp_pack_prop_list()` | static | [src/mtp.c:1403](../src/mtp.c#L1403) |
| `mtp_send_object_prop_list()` | internal | [src/mtp.c:1557](../src/mtp.c#L1557) |
| `mtp_set_object_prop_list()` | internal | [src/mtp.c:1625](../src/mtp.c#L1625) |
| `mtp_get_props_supported()` | internal | [src/mtp.c:1662](../src/mtp.c#L1662) |
| `mtp_get_prop_desc()` | internal | [src/mtp.c:1725](../src/mtp.c#L1725) |
| `mtp_vendor_operation()` | internal | [src/mtp.c:1764](../src/mtp.c#L1764) |

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
| `zmdb_usb_read()` | static | [src/zmdb.c:731](../src/zmdb.c#L731) |
| `zune_infiltrate_legacy()` | public | [src/zmdb.c:843](../src/zmdb.c#L843) |
| `zmdb_parse_full()` | static | [src/zmdb.c:871](../src/zmdb.c#L871) |
| `zune_infiltrate()` | public | [src/zmdb.c:1027](../src/zmdb.c#L1027) |
| `zune_free_scan()` | public | [src/zmdb.c:1066](../src/zmdb.c#L1066) |
| `zune_dump_raw()` | public | [src/zmdb.c:1112](../src/zmdb.c#L1112) |
| `zune_infiltrate_deep()` | public | [src/zmdb.c:1187](../src/zmdb.c#L1187) |
| `zune_free_library()` | public | [src/zmdb.c:1319](../src/zmdb.c#L1319) |

### src/zune_internal.h

| Function | Scope | Definition |
|---|---|---|
| `zune_wound_is_mortal()` | static | [src/zune_internal.h:75](../src/zune_internal.h#L75) |

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
| [include/zune.h](../include/zune.h) | 635 |
| [src/album.c](../src/album.c) | 458 |
| [src/device.c](../src/device.c) | 584 |
| [src/driverkit_usb.c](../src/driverkit_usb.c) | 434 |
| [src/finalize.c](../src/finalize.c) | 183 |
| [src/mtp.c](../src/mtp.c) | 1791 |
| [src/mtp.h](../src/mtp.h) | 587 |
| [src/mtpz.c](../src/mtpz.c) | 1972 |
| [src/mtpz.h](../src/mtpz.h) | 39 |
| [src/photo.c](../src/photo.c) | 497 |
| [src/playlist.c](../src/playlist.c) | 252 |
| [src/ptp.c](../src/ptp.c) | 940 |
| [src/ptp.h](../src/ptp.h) | 317 |
| [src/search.c](../src/search.c) | 67 |
| [src/thumbnail.c](../src/thumbnail.c) | 128 |
| [src/track.c](../src/track.c) | 654 |
| [src/transcode.c](../src/transcode.c) | 317 |
| [src/usb.h](../src/usb.h) | 135 |
| [src/usb_libusb.c](../src/usb_libusb.c) | 197 |
| [src/usb_recovery.c](../src/usb_recovery.c) | 40 |
| [src/util.c](../src/util.c) | 826 |
| [src/video.c](../src/video.c) | 635 |
| [src/zmdb.c](../src/zmdb.c) | 1337 |
| [src/zune_internal.h](../src/zune_internal.h) | 115 |

## Tools and regression tests

These are development tools, not additional library API. Software tests do not
replace device authentication, transfer and readback tests on real hardware.

- [tools/ptp-decode.py](../tools/ptp-decode.py)
- [tools/update-toc.mjs](../tools/update-toc.mjs)
- [tools/zunetool.c](../tools/zunetool.c)
  - `die()`: [tools/zunetool.c:31](../tools/zunetool.c#L31)
  - `connect_or_die()`: [tools/zunetool.c:40](../tools/zunetool.c#L40)
  - `cmd_info()`: [tools/zunetool.c:53](../tools/zunetool.c#L53)
  - `cmd_list()`: [tools/zunetool.c:68](../tools/zunetool.c#L68)
  - `cmd_send()`: [tools/zunetool.c:85](../tools/zunetool.c#L85)
  - `cmd_purge()`: [tools/zunetool.c:107](../tools/zunetool.c#L107)
  - `cmd_torture()`: [tools/zunetool.c:120](../tools/zunetool.c#L120)
  - `interrupt_video_test()`: [tools/zunetool.c:156](../tools/zunetool.c#L156)
  - `video_test_string()`: [tools/zunetool.c:161](../tools/zunetool.c#L161)
  - `video_test_number()`: [tools/zunetool.c:176](../tools/zunetool.c#L176)
  - `video_test_readback()`: [tools/zunetool.c:191](../tools/zunetool.c#L191)
  - `cmd_video_title_test()`: [tools/zunetool.c:212](../tools/zunetool.c#L212)
  - `cmd_audit()`: [tools/zunetool.c:316](../tools/zunetool.c#L316)
  - `cmd_refs()`: [tools/zunetool.c:401](../tools/zunetool.c#L401)
  - `cmd_playlists()`: [tools/zunetool.c:431](../tools/zunetool.c#L431)
  - `cmd_mkplaylist()`: [tools/zunetool.c:448](../tools/zunetool.c#L448)
  - `cmd_zmdbdump()`: [tools/zunetool.c:467](../tools/zunetool.c#L467)
  - `cmd_extract()`: [tools/zunetool.c:476](../tools/zunetool.c#L476)
  - `main()`: [tools/zunetool.c:486](../tools/zunetool.c#L486)
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
- [tests/metadata_probe.c](../tests/metadata_probe.c)
  - `check()`: [tests/metadata_probe.c:7](../tests/metadata_probe.c#L7)
  - `same()`: [tests/metadata_probe.c:13](../tests/metadata_probe.c#L13)
  - `empty()`: [tests/metadata_probe.c:17](../tests/metadata_probe.c#L17)
  - `load()`: [tests/metadata_probe.c:23](../tests/metadata_probe.c#L23)
  - `main()`: [tests/metadata_probe.c:30](../tests/metadata_probe.c#L30)
- [tests/run-artist-reuse.sh](../tests/run-artist-reuse.sh)
- [tests/run-metadata-probe.sh](../tests/run-metadata-probe.sh)
- [tests/run-video-named.sh](../tests/run-video-named.sh)
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
| [API_REFERENCE.md](API_REFERENCE.md) | libzune API Reference |
| [ARTIST_REUSE.md](ARTIST_REUSE.md) | Reuse existing artist objects (2026-10-02) |
| [BUILD_WITH_MTPZ.md](BUILD_WITH_MTPZ.md) | Building with Zune authentication |
| [FIRMWARE_FLASH_CAPTURE.md](FIRMWARE_FLASH_CAPTURE.md) | Zune Firmware Flash — USB Capture Guide |
| [LINUX_TESTING.md](LINUX_TESTING.md) | Testing libzune on Linux |
| [MACOS_ZUNE_QUIRKS.md](MACOS_ZUNE_QUIRKS.md) | macOS Zune Quirks — Everything That's Different from Linux |
| [METADATA_PROBE.md](METADATA_PROBE.md) | Metadata probe contract — tester release R2 |
| [MTPZ_PROTOCOL.md](MTPZ_PROTOCOL.md) | MTPZ Authentication Protocol |
| [PROJECT_OVERVIEW.md](PROJECT_OVERVIEW.md) | Zuuned — The Complete Zune Device Manager |
| [PUBLIC_CREDENTIALS.md](PUBLIC_CREDENTIALS.md) | Private and public authentication builds |
| [SYNC_PIPELINE.md](SYNC_PIPELINE.md) | Sync Pipeline — How Content Gets to the Zune |
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
