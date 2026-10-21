# libzune: source and documentation index

Generated from the current checkout by `node tools/update-toc.mjs`.
Run `node tools/update-toc.mjs --check` to detect stale references.
Node.js is needed only to regenerate this document, not to build or use libzune.

The public API is [include/zune.h](../include/zune.h). This index covers every
ordinary C function definition in `src/*.c` and private definitions in `src/*.h` and `src/hd/*.h`,
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
- [HD artist-content C API](HD_ARTWORK_API.md): reusable native USB content API, callbacks, examples and hardening handoff.
- [HD artwork research](HD_ARTWORK_RESEARCH.md): artist-background identity, USB tunnel evidence and experimental hardware gates.
- [How HD artwork travels over USB](HD_ARTWORK_TRANSPORT.md): native local HTTP responder, protocol layers, image roles and current limits.
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

114 declarations in `include/zune.h`. Consult the linked header for
ownership, return values and threading requirements before calling an operation.

| Function | Declaration | Implementation |
|---|---|---|
| `zune_hd_art_validate()` | [include/zune.h:121](../include/zune.h#L121) | [src/hd_artwork.c:79](../src/hd_artwork.c#L79) |
| `zune_hd_art_serve()` | [include/zune.h:132](../include/zune.h#L132) | [src/hd_artwork.c:162](../src/hd_artwork.c#L162) |
| `zune_hd_art_ensure_artist()` | [include/zune.h:143](../include/zune.h#L143) | [src/hd_artwork.c:311](../src/hd_artwork.c#L311) |
| `zune_dossier()` | [include/zune.h:154](../include/zune.h#L154) | [src/device.c:36](../src/device.c#L36) |
| `zune_wiretap()` | [include/zune.h:177](../include/zune.h#L177) | [src/wiretap.c:42](../src/wiretap.c#L42) |
| `zune_set_chatter()` | [include/zune.h:182](../include/zune.h#L182) | [src/wiretap.c:35](../src/wiretap.c#L35) |
| `zune_breach()` | [include/zune.h:211](../include/zune.h#L211) | [src/device.c:250](../src/device.c#L250) |
| `zune_sever()` | [include/zune.h:216](../include/zune.h#L216) | [src/device.c:290](../src/device.c#L290) |
| `zune_go_dark()` | [include/zune.h:222](../include/zune.h#L222) | [src/device.c:311](../src/device.c#L311) |
| `zune_get_name()` | [include/zune.h:226](../include/zune.h#L226) | [src/device.c:317](../src/device.c#L317) |
| `zune_get_model()` | [include/zune.h:227](../include/zune.h#L227) | [src/device.c:321](../src/device.c#L321) |
| `zune_get_serial()` | [include/zune.h:228](../include/zune.h#L228) | [src/device.c:325](../src/device.c#L325) |
| `zune_get_battery()` | [include/zune.h:229](../include/zune.h#L229) | [src/device.c:329](../src/device.c#L329) |
| `zune_get_capacity()` | [include/zune.h:230](../include/zune.h#L230) | [src/device.c:333](../src/device.c#L333) |
| `zune_get_headroom()` | [include/zune.h:231](../include/zune.h#L231) | [src/device.c:337](../src/device.c#L337) |
| `zune_refresh_storage()` | [include/zune.h:237](../include/zune.h#L237) | [src/device.c:346](../src/device.c#L346) |
| `zune_identify()` | [include/zune.h:238](../include/zune.h#L238) | [src/device.c:364](../src/device.c#L364) |
| `zune_get_family()` | [include/zune.h:243](../include/zune.h#L243) | [src/device.c:395](../src/device.c#L395) |
| `zune_is_hdd()` | [include/zune.h:247](../include/zune.h#L247) | [src/device.c:406](../src/device.c#L406) |
| `zune_rename()` | [include/zune.h:250](../include/zune.h#L250) | [src/device.c:557](../src/device.c#L557) |
| `zune_is_live()` | [include/zune.h:254](../include/zune.h#L254) | [src/device.c:414](../src/device.c#L414) |
| `zune_abort()` | [include/zune.h:258](../include/zune.h#L258) | [src/device.c:504](../src/device.c#L504) |
| `zune_is_aborted()` | [include/zune.h:261](../include/zune.h#L261) | [src/device.c:508](../src/device.c#L508) |
| `zune_clear_abort()` | [include/zune.h:264](../include/zune.h#L264) | [src/device.c:512](../src/device.c#L512) |
| `zune_autopsy()` | [include/zune.h:271](../include/zune.h#L271) | [src/device.c:518](../src/device.c#L518) |
| `zune_autopsy_name()` | [include/zune.h:274](../include/zune.h#L274) | [src/device.c:522](../src/device.c#L522) |
| `zune_unjam()` | [include/zune.h:279](../include/zune.h#L279) | [src/usb_recovery.c:13](../src/usb_recovery.c#L13) |
| `zune_forge_folder()` | [include/zune.h:283](../include/zune.h#L283) | [src/device.c:423](../src/device.c#L423) |
| `zune_get_folders()` | [include/zune.h:291](../include/zune.h#L291) | [src/device.c:435](../src/device.c#L435) |
| `zune_free_folders()` | [include/zune.h:292](../include/zune.h#L292) | [src/device.c:496](../src/device.c#L496) |
| `zune_get_tracks()` | [include/zune.h:313](../include/zune.h#L313) | [src/track.c:47](../src/track.c#L47) |
| `zune_free_tracks()` | [include/zune.h:314](../include/zune.h#L314) | [src/track.c:170](../src/track.c#L170) |
| `zune_smuggle_track()` | [include/zune.h:320](../include/zune.h#L320) | [src/track.c:282](../src/track.c#L282) |
| `zune_smuggle_track_tagged()` | [include/zune.h:324](../include/zune.h#L324) | [src/track.c:301](../src/track.c#L301) |
| `zune_purge_track()` | [include/zune.h:331](../include/zune.h#L331) | [src/track.c:358](../src/track.c#L358) |
| `zune_extract_track()` | [include/zune.h:334](../include/zune.h#L334) | [src/track.c:375](../src/track.c#L375) |
| `zune_get_track_state()` | [include/zune.h:342](../include/zune.h#L342) | [src/track.c:459](../src/track.c#L459) |
| `zune_set_track_state()` | [include/zune.h:349](../include/zune.h#L349) | [src/track.c:502](../src/track.c#L502) |
| `zune_verify()` | [include/zune.h:357](../include/zune.h#L357) | [src/track.c:393](../src/track.c#L393) |
| `zune_rename_item()` | [include/zune.h:364](../include/zune.h#L364) | [src/finalize.c:50](../src/finalize.c#L50) |
| `zune_sync_notify()` | [include/zune.h:377](../include/zune.h#L377) | [src/finalize.c:85](../src/finalize.c#L85) |
| `zune_probe_object()` | [include/zune.h:385](../include/zune.h#L385) | [src/track.c:540](../src/track.c#L540) |
| `zune_probe_object_named()` | [include/zune.h:389](../include/zune.h#L389) | [src/track.c:548](../src/track.c#L548) |
| `zune_get_item_refs()` | [include/zune.h:395](../include/zune.h#L395) | [src/track.c:570](../src/track.c#L570) |
| `zune_find_track()` | [include/zune.h:405](../include/zune.h#L405) | [src/search.c:14](../src/search.c#L14) |
| `zune_find_video()` | [include/zune.h:412](../include/zune.h#L412) | [src/search.c:40](../src/search.c#L40) |
| `zune_find_photo()` | [include/zune.h:416](../include/zune.h#L416) | [src/search.c:57](../src/search.c#L57) |
| `zune_get_videos()` | [include/zune.h:441](../include/zune.h#L441) | [src/video.c:220](../src/video.c#L220) |
| `zune_free_videos()` | [include/zune.h:442](../include/zune.h#L442) | [src/video.c:339](../src/video.c#L339) |
| `zune_smuggle_video_named()` | [include/zune.h:460](../include/zune.h#L460) | [src/video.c:349](../src/video.c#L349) |
| `zune_smuggle_movie()` | [include/zune.h:475](../include/zune.h#L475) | [src/video.c:394](../src/video.c#L394) |
| `zune_get_series_info()` | [include/zune.h:483](../include/zune.h#L483) | [src/video.c:418](../src/video.c#L418) |
| `zune_smuggle_episode()` | [include/zune.h:490](../include/zune.h#L490) | [src/video.c:472](../src/video.c#L472) |
| `zune_smuggle_clip()` | [include/zune.h:498](../include/zune.h#L498) | [src/video.c:517](../src/video.c#L517) |
| `zune_smuggle_other()` | [include/zune.h:504](../include/zune.h#L504) | [src/video.c:541](../src/video.c#L541) |
| `zune_purge_video()` | [include/zune.h:510](../include/zune.h#L510) | [src/video.c:564](../src/video.c#L564) |
| `zune_extract_video()` | [include/zune.h:513](../include/zune.h#L513) | [src/video.c:580](../src/video.c#L580) |
| `zune_get_photos()` | [include/zune.h:532](../include/zune.h#L532) | [src/photo.c:138](../src/photo.c#L138) |
| `zune_free_photos()` | [include/zune.h:533](../include/zune.h#L533) | [src/photo.c:214](../src/photo.c#L214) |
| `zune_get_dimensions()` | [include/zune.h:537](../include/zune.h#L537) | [src/photo.c:221](../src/photo.c#L221) |
| `zune_get_photo_albums()` | [include/zune.h:541](../include/zune.h#L541) | [src/photo.c:259](../src/photo.c#L259) |
| `zune_free_photo_albums()` | [include/zune.h:544](../include/zune.h#L544) | [src/photo.c:313](../src/photo.c#L313) |
| `zune_arm_photo()` | [include/zune.h:549](../include/zune.h#L549) | [src/photo.c:330](../src/photo.c#L330) |
| `zune_smuggle_photo()` | [include/zune.h:554](../include/zune.h#L554) | [src/photo.c:373](../src/photo.c#L373) |
| `zune_purge_photo()` | [include/zune.h:558](../include/zune.h#L558) | [src/photo.c:496](../src/photo.c#L496) |
| `zune_extract_photo()` | [include/zune.h:561](../include/zune.h#L561) | [src/photo.c:512](../src/photo.c#L512) |
| `zune_get_playlists()` | [include/zune.h:574](../include/zune.h#L574) | [src/playlist.c:12](../src/playlist.c#L12) |
| `zune_free_playlists()` | [include/zune.h:575](../include/zune.h#L575) | [src/playlist.c:97](../src/playlist.c#L97) |
| `zune_forge_playlist()` | [include/zune.h:578](../include/zune.h#L578) | [src/playlist.c:124](../src/playlist.c#L124) |
| `zune_rewire_playlist()` | [include/zune.h:582](../include/zune.h#L582) | [src/playlist.c:198](../src/playlist.c#L198) |
| `zune_purge_playlist()` | [include/zune.h:587](../include/zune.h#L587) | [src/playlist.c:229](../src/playlist.c#L229) |
| `zune_forge_album()` | [include/zune.h:598](../include/zune.h#L598) | [src/album.c:32](../src/album.c#L32) |
| `zune_rewire_album()` | [include/zune.h:607](../include/zune.h#L607) | [src/album.c:150](../src/album.c#L150) |
| `zune_forge_artist()` | [include/zune.h:617](../include/zune.h#L617) | [src/album.c:246](../src/album.c#L246) |
| `zune_link_artist()` | [include/zune.h:621](../include/zune.h#L621) | [src/album.c:342](../src/album.c#L342) |
| `zune_get_albums()` | [include/zune.h:633](../include/zune.h#L633) | [src/album.c:360](../src/album.c#L360) |
| `zune_get_albums_checked()` | [include/zune.h:638](../include/zune.h#L638) | [src/hd_artwork.c:386](../src/hd_artwork.c#L386) |
| `zune_free_albums()` | [include/zune.h:641](../include/zune.h#L641) | [src/album.c:442](../src/album.c#L442) |
| `zune_get_podcast_series()` | [include/zune.h:662](../include/zune.h#L662) | [src/podcast.c:93](../src/podcast.c#L93) |
| `zune_free_podcast_series()` | [include/zune.h:663](../include/zune.h#L663) | [src/podcast.c:84](../src/podcast.c#L84) |
| `zune_get_podcast_episodes()` | [include/zune.h:678](../include/zune.h#L678) | [src/spoken_inventory.c:98](../src/spoken_inventory.c#L98) |
| `zune_free_podcast_episodes()` | [include/zune.h:679](../include/zune.h#L679) | [src/spoken_inventory.c:89](../src/spoken_inventory.c#L89) |
| `zune_get_podcast_episode_sources()` | [include/zune.h:693](../include/zune.h#L693) | [src/podcast.c:520](../src/podcast.c#L520) |
| `zune_free_podcast_episode_sources()` | [include/zune.h:694](../include/zune.h#L694) | [src/podcast.c:513](../src/podcast.c#L513) |
| `zune_get_podcast_states()` | [include/zune.h:714](../include/zune.h#L714) | [src/podcast_state.c:151](../src/podcast_state.c#L151) |
| `zune_free_podcast_states()` | [include/zune.h:715](../include/zune.h#L715) | [src/podcast_state.c:144](../src/podcast_state.c#L144) |
| `zune_forge_podcast_series()` | [include/zune.h:726](../include/zune.h#L726) | [src/podcast.c:275](../src/podcast.c#L275) |
| `zune_smuggle_podcast_episode()` | [include/zune.h:756](../include/zune.h#L756) | [src/podcast.c:329](../src/podcast.c#L329) |
| `zune_find_podcast_episode()` | [include/zune.h:765](../include/zune.h#L765) | [src/podcast.c:428](../src/podcast.c#L428) |
| `zune_get_audiobook_parts()` | [include/zune.h:780](../include/zune.h#L780) | [src/spoken_inventory.c:140](../src/spoken_inventory.c#L140) |
| `zune_free_audiobook_parts()` | [include/zune.h:781](../include/zune.h#L781) | [src/spoken_inventory.c:131](../src/spoken_inventory.c#L131) |
| `zune_find_audiobook_part()` | [include/zune.h:794](../include/zune.h#L794) | [src/audiobook.c:132](../src/audiobook.c#L132) |
| `zune_smuggle_audiobook_part()` | [include/zune.h:803](../include/zune.h#L803) | [src/audiobook.c:215](../src/audiobook.c#L215) |
| `zune_brand()` | [include/zune.h:810](../include/zune.h#L810) | [src/thumbnail.c:16](../src/thumbnail.c#L16) |
| `zune_get_brand_limits()` | [include/zune.h:819](../include/zune.h#L819) | [src/thumbnail.c:91](../src/thumbnail.c#L91) |
| `zune_grab_thumb()` | [include/zune.h:826](../include/zune.h#L826) | [src/thumbnail.c:140](../src/thumbnail.c#L140) |
| `zune_grab_photo_thumb()` | [include/zune.h:832](../include/zune.h#L832) | [src/thumbnail.c:174](../src/thumbnail.c#L174) |
| `zune_arm_audio()` | [include/zune.h:841](../include/zune.h#L841) | [src/transcode.c:51](../src/transcode.c#L51) |
| `zune_retag()` | [include/zune.h:846](../include/zune.h#L846) | [src/transcode.c:83](../src/transcode.c#L83) |
| `zune_arm_video()` | [include/zune.h:851](../include/zune.h#L851) | [src/transcode.c:235](../src/transcode.c#L235) |
| `zune_arm_episode()` | [include/zune.h:855](../include/zune.h#L855) | [src/transcode.c:241](../src/transcode.c#L241) |
| `zune_infiltrate_legacy()` | [include/zune.h:871](../include/zune.h#L871) | [src/zmdb.c:842](../src/zmdb.c#L842) |
| `zune_free_library()` | [include/zune.h:873](../include/zune.h#L873) | [src/zmdb.c:1256](../src/zmdb.c#L1256) |
| `zune_finalize()` | [include/zune.h:880](../include/zune.h#L880) | [src/finalize.c:12](../src/finalize.c#L12) |
| `zune_decode_filename()` | [include/zune.h:887](../include/zune.h#L887) | [src/util.c:438](../src/util.c#L438) |
| `zune_snap_thumb()` | [include/zune.h:895](../include/zune.h#L895) | [src/util.c:509](../src/util.c#L509) |
| `zune_probe()` | [include/zune.h:919](../include/zune.h#L919) | [src/util.c:784](../src/util.c#L784)<br>[src/util.c:874](../src/util.c#L874) |
| `zune_free_metadata()` | [include/zune.h:920](../include/zune.h#L920) | [src/util.c:978](../src/util.c#L978) |
| `zune_smuggle_track_ex()` | [include/zune.h:929](../include/zune.h#L929) | [src/track.c:322](../src/track.c#L322) |
| `zune_infiltrate()` | [include/zune.h:976](../include/zune.h#L976) | [src/zmdb.c:1000](../src/zmdb.c#L1000) |
| `zune_free_scan()` | [include/zune.h:979](../include/zune.h#L979) | [src/zmdb.c:1044](../src/zmdb.c#L1044) |
| `zune_dump_raw()` | [include/zune.h:985](../include/zune.h#L985) | [src/zmdb.c:1090](../src/zmdb.c#L1090) |
| `zune_infiltrate_deep()` | [include/zune.h:990](../include/zune.h#L990) | [src/zmdb.c:1122](../src/zmdb.c#L1122) |
| `zune_get_error()` | [include/zune.h:1000](../include/zune.h#L1000) | [src/device.c:27](../src/device.c#L27) |

## Library definitions

487 function definitions, including conditional alternatives. `public` means
declared in `include/zune.h`; `internal` means a non-static implementation
symbol outside that API; `static` means file-local or header-inline.

### src/album.c

| Function | Scope | Definition |
|---|---|---|
| `zune_forge_failed()` | internal | [src/album.c:17](../src/album.c#L17) |
| `zune_forge_album()` | public | [src/album.c:32](../src/album.c#L32) |
| `zune_rewire_album()` | public | [src/album.c:150](../src/album.c#L150) |
| `artist_name_equal()` | static | [src/album.c:205](../src/album.c#L205) |
| `find_artist()` | static | [src/album.c:224](../src/album.c#L224) |
| `zune_forge_artist()` | public | [src/album.c:246](../src/album.c#L246) |
| `zune_link_artist()` | public | [src/album.c:342](../src/album.c#L342) |
| `zune_get_albums()` | public | [src/album.c:360](../src/album.c#L360) |
| `zune_free_albums()` | public | [src/album.c:442](../src/album.c#L442) |

### src/audiobook.c

| Function | Scope | Definition |
|---|---|---|
| `book_ready()` | static | [src/audiobook.c:6](../src/audiobook.c#L6) |
| `book_text()` | static | [src/audiobook.c:21](../src/audiobook.c#L21) |
| `book_input()` | static | [src/audiobook.c:34](../src/audiobook.c#L34) |
| `book_read_text()` | static | [src/audiobook.c:48](../src/audiobook.c#L48) |
| `book_number()` | static | [src/audiobook.c:64](../src/audiobook.c#L64) |
| `book_inventory()` | static | [src/audiobook.c:79](../src/audiobook.c#L79) |
| `book_folders()` | static | [src/audiobook.c:92](../src/audiobook.c#L92) |
| `zune_find_audiobook_part()` | public | [src/audiobook.c:132](../src/audiobook.c#L132) |
| `zune_smuggle_audiobook_part()` | public | [src/audiobook.c:215](../src/audiobook.c#L215) |

### src/bytes.h

| Function | Scope | Definition |
|---|---|---|
| `zune_le16()` | static | [src/bytes.h:11](../src/bytes.h#L11) |
| `zune_le32()` | static | [src/bytes.h:16](../src/bytes.h#L16) |
| `zune_le64()` | static | [src/bytes.h:21](../src/bytes.h#L21) |
| `zune_put_le16()` | static | [src/bytes.h:26](../src/bytes.h#L26) |
| `zune_put_le32()` | static | [src/bytes.h:32](../src/bytes.h#L32) |
| `zune_put_le64()` | static | [src/bytes.h:38](../src/bytes.h#L38) |

### src/device.c

| Function | Scope | Definition |
|---|---|---|
| `zune_set_error()` | internal | [src/device.c:14](../src/device.c#L14) |
| `zune_clear_error()` | internal | [src/device.c:23](../src/device.c#L23) |
| `zune_get_error()` | public | [src/device.c:27](../src/device.c#L27) |
| `zune_dossier()` | public | [src/device.c:36](../src/device.c#L36) |
| `breach_open()` | static | [src/device.c:46](../src/device.c#L46) |
| `breach_session()` | static | [src/device.c:82](../src/device.c#L82) |
| `breach_auth()` | static | [src/device.c:119](../src/device.c#L119) |
| `breach_identify()` | static | [src/device.c:155](../src/device.c#L155) |
| `breach_storage()` | static | [src/device.c:227](../src/device.c#L227) |
| `zune_breach()` | public | [src/device.c:250](../src/device.c#L250) |
| `zune_sever()` | public | [src/device.c:290](../src/device.c#L290) |
| `zune_go_dark()` | public | [src/device.c:311](../src/device.c#L311) |
| `zune_get_name()` | public | [src/device.c:317](../src/device.c#L317) |
| `zune_get_model()` | public | [src/device.c:321](../src/device.c#L321) |
| `zune_get_serial()` | public | [src/device.c:325](../src/device.c#L325) |
| `zune_get_battery()` | public | [src/device.c:329](../src/device.c#L329) |
| `zune_get_capacity()` | public | [src/device.c:333](../src/device.c#L333) |
| `zune_get_headroom()` | public | [src/device.c:337](../src/device.c#L337) |
| `zune_refresh_storage()` | public | [src/device.c:346](../src/device.c#L346) |
| `zune_identify()` | public | [src/device.c:364](../src/device.c#L364) |
| `zune_get_family()` | public | [src/device.c:395](../src/device.c#L395) |
| `zune_is_hdd()` | public | [src/device.c:406](../src/device.c#L406) |
| `zune_is_live()` | public | [src/device.c:414](../src/device.c#L414) |
| `zune_forge_folder()` | public | [src/device.c:423](../src/device.c#L423) |
| `zune_get_folders()` | public | [src/device.c:435](../src/device.c#L435) |
| `zune_free_folders()` | public | [src/device.c:496](../src/device.c#L496) |
| `zune_abort()` | public | [src/device.c:504](../src/device.c#L504) |
| `zune_is_aborted()` | public | [src/device.c:508](../src/device.c#L508) |
| `zune_clear_abort()` | public | [src/device.c:512](../src/device.c#L512) |
| `zune_autopsy()` | public | [src/device.c:518](../src/device.c#L518) |
| `zune_autopsy_name()` | public | [src/device.c:522](../src/device.c#L522) |
| `zune_rename()` | public | [src/device.c:557](../src/device.c#L557) |

### src/devinfo.c

| Function | Scope | Definition |
|---|---|---|
| `devinfo_skip()` | static | [src/devinfo.c:14](../src/devinfo.c#L14) |
| `devinfo_skip_string()` | static | [src/devinfo.c:21](../src/devinfo.c#L21) |
| `devinfo_skip_u16_array()` | static | [src/devinfo.c:26](../src/devinfo.c#L26) |
| `zune_parse_device_info()` | internal | [src/devinfo.c:45](../src/devinfo.c#L45) |

### src/driverkit_usb.c

| Function | Scope | Definition |
|---|---|---|
| `get_data()` | static | [src/driverkit_usb.c:64](../src/driverkit_usb.c#L64) |
| `driverkit_open()` | static | [src/driverkit_usb.c:71](../src/driverkit_usb.c#L71) |
| `driverkit_close()` | static | [src/driverkit_usb.c:194](../src/driverkit_usb.c#L194) |
| `driverkit_bulk_read()` | static | [src/driverkit_usb.c:221](../src/driverkit_usb.c#L221) |
| `driverkit_bulk_write()` | static | [src/driverkit_usb.c:269](../src/driverkit_usb.c#L269) |
| `driverkit_clear_halt()` | static | [src/driverkit_usb.c:324](../src/driverkit_usb.c#L324) |
| `driverkit_reset()` | static | [src/driverkit_usb.c:354](../src/driverkit_usb.c#L354) |
| `zune_usb_get_backend()` | internal | [src/driverkit_usb.c:392](../src/driverkit_usb.c#L392) |
| `zune_usb_find_device()` | internal | [src/driverkit_usb.c:399](../src/driverkit_usb.c#L399) |

### src/finalize.c

| Function | Scope | Definition |
|---|---|---|
| `zune_finalize()` | public | [src/finalize.c:12](../src/finalize.c#L12) |
| `zune_rename_item()` | public | [src/finalize.c:50](../src/finalize.c#L50) |
| `zune_sync_notify()` | public | [src/finalize.c:85](../src/finalize.c#L85) |

### src/hd_artwork.c

| Function | Scope | Definition |
|---|---|---|
| `hd_guid()` | static | [src/hd_artwork.c:15](../src/hd_artwork.c#L15) |
| `hd_name()` | static | [src/hd_artwork.c:37](../src/hd_artwork.c#L37) |
| `hd_sink_emit()` | static | [src/hd_artwork.c:45](../src/hd_artwork.c#L45) |
| `hd_prepare()` | static | [src/hd_artwork.c:49](../src/hd_artwork.c#L49) |
| `zune_hd_art_validate()` | public | [src/hd_artwork.c:79](../src/hd_artwork.c#L79) |
| `hd_progress()` | static | [src/hd_artwork.c:103](../src/hd_artwork.c#L103) |
| `hd_public_emit()` | static | [src/hd_artwork.c:117](../src/hd_artwork.c#L117) |
| `hd_public_ready()` | static | [src/hd_artwork.c:122](../src/hd_artwork.c#L122) |
| `hd_complete()` | static | [src/hd_artwork.c:136](../src/hd_artwork.c#L136) |
| `hd_public_packet()` | static | [src/hd_artwork.c:148](../src/hd_artwork.c#L148) |
| `hd_public_tick()` | static | [src/hd_artwork.c:154](../src/hd_artwork.c#L154) |
| `zune_hd_art_serve()` | public | [src/hd_artwork.c:162](../src/hd_artwork.c#L162) |
| `hd_equal_name()` | static | [src/hd_artwork.c:221](../src/hd_artwork.c#L221) |
| `hd_read_name()` | static | [src/hd_artwork.c:239](../src/hd_artwork.c#L239) |
| `hd_name_wire()` | static | [src/hd_artwork.c:255](../src/hd_artwork.c#L255) |
| `hd_prop()` | static | [src/hd_artwork.c:274](../src/hd_artwork.c#L274) |
| `hd_artist_parent()` | static | [src/hd_artwork.c:283](../src/hd_artwork.c#L283) |
| `zune_hd_art_ensure_artist()` | public | [src/hd_artwork.c:311](../src/hd_artwork.c#L311) |
| `zune_get_albums_checked()` | public | [src/hd_artwork.c:386](../src/hd_artwork.c#L386) |

### src/mtp.c

| Function | Scope | Definition |
|---|---|---|
| `utf8_to_ucs2_chars()` | static | [src/mtp.c:55](../src/mtp.c#L55) |
| `ucs2_char_to_utf8()` | static | [src/mtp.c:150](../src/mtp.c#L150) |
| `mtp_string_to_ucs2()` | internal | [src/mtp.c:167](../src/mtp.c#L167) |
| `mtp_ucs2_to_string()` | internal | [src/mtp.c:233](../src/mtp.c#L233) |
| `mtp_read_string()` | static | [src/mtp.c:294](../src/mtp.c#L294) |
| `mtp_get_storage_ids()` | internal | [src/mtp.c:321](../src/mtp.c#L321) |
| `mtp_get_storage_info()` | internal | [src/mtp.c:379](../src/mtp.c#L379) |
| `mtp_free_storage_info()` | internal | [src/mtp.c:439](../src/mtp.c#L439) |
| `mtp_get_object_handles()` | internal | [src/mtp.c:452](../src/mtp.c#L452) |
| `mtp_get_object_info()` | internal | [src/mtp.c:518](../src/mtp.c#L518) |
| `mtp_free_object_info()` | internal | [src/mtp.c:594](../src/mtp.c#L594) |
| `drop_reply()` | static | [src/mtp.c:603](../src/mtp.c#L603) |
| `mtp_get_object()` | internal | [src/mtp.c:612](../src/mtp.c#L612) |
| `mtp_get_object_to_file()` | internal | [src/mtp.c:638](../src/mtp.c#L638) |
| `mtp_pack_object_info()` | static | [src/mtp.c:684](../src/mtp.c#L684) |
| `mtp_send_object_info()` | internal | [src/mtp.c:740](../src/mtp.c#L740) |
| `mtp_send_object()` | internal | [src/mtp.c:803](../src/mtp.c#L803) |
| `mtp_send_object_from_fd()` | internal | [src/mtp.c:822](../src/mtp.c#L822) |
| `mtp_send_object_from_file()` | internal | [src/mtp.c:838](../src/mtp.c#L838) |
| `mtp_delete_object()` | internal | [src/mtp.c:851](../src/mtp.c#L851) |
| `mtp_get_object_prop_value()` | internal | [src/mtp.c:884](../src/mtp.c#L884) |
| `mtp_set_object_prop_value_u16()` | internal | [src/mtp.c:914](../src/mtp.c#L914) |
| `mtp_set_object_prop_value_u32()` | internal | [src/mtp.c:943](../src/mtp.c#L943) |
| `mtp_set_object_prop_value_str()` | internal | [src/mtp.c:972](../src/mtp.c#L972) |
| `mtp_set_object_prop_value_ucs2()` | internal | [src/mtp.c:1007](../src/mtp.c#L1007) |
| `mtp_get_object_prop_list()` | internal | [src/mtp.c:1059](../src/mtp.c#L1059) |
| `mtp_get_object_references()` | internal | [src/mtp.c:1097](../src/mtp.c#L1097) |
| `mtp_set_object_references()` | internal | [src/mtp.c:1159](../src/mtp.c#L1159) |
| `mtp_create_folder()` | internal | [src/mtp.c:1209](../src/mtp.c#L1209) |
| `mtp_get_representative_sample()` | internal | [src/mtp.c:1242](../src/mtp.c#L1242) |
| `mtp_send_representative_sample()` | internal | [src/mtp.c:1289](../src/mtp.c#L1289) |
| `mtp_pack_prop_list()` | static | [src/mtp.c:1365](../src/mtp.c#L1365) |
| `mtp_send_object_prop_list()` | internal | [src/mtp.c:1527](../src/mtp.c#L1527) |
| `mtp_set_object_prop_list()` | internal | [src/mtp.c:1595](../src/mtp.c#L1595) |
| `mtp_get_props_supported()` | internal | [src/mtp.c:1632](../src/mtp.c#L1632) |
| `mtp_get_prop_desc()` | internal | [src/mtp.c:1704](../src/mtp.c#L1704) |
| `mtp_vendor_operation()` | internal | [src/mtp.c:1744](../src/mtp.c#L1744) |

### src/mtp.h

| Function | Scope | Definition |
|---|---|---|
| `mtp_read_string_prop()` | static | [src/mtp.h:597](../src/mtp.h#L597) |
| `mtp_prop_desc_writable()` | static | [src/mtp.h:615](../src/mtp.h#L615) |

### src/mtp_text.h

| Function | Scope | Definition |
|---|---|---|
| `mtp_text_to_array()` | static | [src/mtp_text.h:11](../src/mtp_text.h#L11) |
| `mtp_array_to_text()` | static | [src/mtp_text.h:52](../src/mtp_text.h#L52) |

### src/mtpz.c

| Function | Scope | Definition |
|---|---|---|
| `mtpz_wipe()` | static | [src/mtpz.c:62](../src/mtpz.c#L62) |
| `mtpz_alloc()` | static | [src/mtpz.c:74](../src/mtpz.c#L74) |
| `fgets_strip()` | static | [src/mtpz.c:86](../src/mtpz.c#L86) |
| `hex_to_bytes()` | static | [src/mtpz.c:108](../src/mtpz.c#L108) |
| `mtpz_release_keys()` | static | [src/mtpz.c:132](../src/mtpz.c#L132) |
| `mtpz_load_embedded_keys()` | static | [src/mtpz.c:150](../src/mtpz.c#L150) |
| `mtpz_load_keys_from_file()` | static | [src/mtpz.c:186](../src/mtpz.c#L186) |
| `mtpz_load_keys_now()` | static | [src/mtpz.c:270](../src/mtpz.c#L270) |
| `mtpz_load_keys()` | internal | [src/mtpz.c:291](../src/mtpz.c#L291) |
| `mtpz_free_keys()` | internal | [src/mtpz.c:301](../src/mtpz.c#L301) |
| `mtpz_keys_available()` | internal | [src/mtpz.c:309](../src/mtpz.c#L309) |
| `mtpz_gcrypt_setup()` | static | [src/mtpz.c:322](../src/mtpz.c#L322) |
| `mtpz_init_gcrypt()` | internal | [src/mtpz.c:336](../src/mtpz.c#L336) |
| `mtpz_bswap32()` | static | [src/mtpz.c:346](../src/mtpz.c#L346) |
| `mtpz_rsa_init()` | static | [src/mtpz.c:369](../src/mtpz.c#L369) |
| `mtpz_rsa_free()` | static | [src/mtpz.c:397](../src/mtpz.c#L397) |
| `mtpz_rsa_decrypt()` | static | [src/mtpz.c:404](../src/mtpz.c#L404) |
| `mtpz_rsa_sign()` | static | [src/mtpz.c:443](../src/mtpz.c#L443) |
| `mtpz_hash_init_state()` | static | [src/mtpz.c:463](../src/mtpz.c#L463) |
| `mtpz_hash_reset_state()` | static | [src/mtpz.c:471](../src/mtpz.c#L471) |
| `mtpz_hash_transform_hash()` | static | [src/mtpz.c:487](../src/mtpz.c#L487) |
| `mtpz_hash_finalize_hash()` | static | [src/mtpz.c:527](../src/mtpz.c#L527) |
| `mtpz_hash_custom6A5DC()` | static | [src/mtpz.c:567](../src/mtpz.c#L567) |
| `mtpz_hash_compute_hash()` | static | [src/mtpz.c:597](../src/mtpz.c#L597) |
| `mtpz_hash_f()` | static | [src/mtpz.c:642](../src/mtpz.c#L642) |
| `mtpz_hash_rotate_left()` | static | [src/mtpz.c:653](../src/mtpz.c#L653) |
| `mtpz_encryption_cipher_advanced()` | static | [src/mtpz.c:1184](../src/mtpz.c#L1184) |
| `mtpz_encryption_expand_key()` | static | [src/mtpz.c:1250](../src/mtpz.c#L1250) |
| `mtpz_encryption_expand_key_inner()` | static | [src/mtpz.c:1295](../src/mtpz.c#L1295) |
| `mtpz_encryption_inv_mix_columns()` | static | [src/mtpz.c:1353](../src/mtpz.c#L1353) |
| `mtpz_encryption_decrypt_custom()` | static | [src/mtpz.c:1369](../src/mtpz.c#L1369) |
| `mtpz_encryption_encrypt_custom()` | static | [src/mtpz.c:1442](../src/mtpz.c#L1442) |
| `mtpz_encryption_encrypt_mac()` | static | [src/mtpz.c:1517](../src/mtpz.c#L1517) |
| `mtpz_encode_mtp_string()` | static | [src/mtpz.c:2059](../src/mtpz.c#L2059) |
| `mtpz_handshake()` | internal | [src/mtpz.c:2091](../src/mtpz.c#L2091) |

### src/photo.c

| Function | Scope | Definition |
|---|---|---|
| `zune_free_folder_cache()` | internal | [src/photo.c:24](../src/photo.c#L24) |
| `folder_cache_insert()` | static | [src/photo.c:36](../src/photo.c#L36) |
| `folder_cache_lookup()` | static | [src/photo.c:45](../src/photo.c#L45) |
| `folder_cache_count()` | static | [src/photo.c:53](../src/photo.c#L53) |
| `zune_cache_folder_names()` | internal | [src/photo.c:68](../src/photo.c#L68) |
| `folder_cache_find()` | static | [src/photo.c:104](../src/photo.c#L104) |
| `file_is_jpeg()` | static | [src/photo.c:112](../src/photo.c#L112) |
| `is_picture_format()` | static | [src/photo.c:127](../src/photo.c#L127) |
| `zune_get_photos()` | public | [src/photo.c:138](../src/photo.c#L138) |
| `zune_free_photos()` | public | [src/photo.c:214](../src/photo.c#L214) |
| `zune_get_dimensions()` | public | [src/photo.c:221](../src/photo.c#L221) |
| `zune_get_photo_albums()` | public | [src/photo.c:259](../src/photo.c#L259) |
| `zune_free_photo_albums()` | public | [src/photo.c:313](../src/photo.c#L313) |
| `zune_arm_photo()` | public | [src/photo.c:330](../src/photo.c#L330) |
| `zune_smuggle_photo()` | public | [src/photo.c:373](../src/photo.c#L373) |
| `zune_purge_photo()` | public | [src/photo.c:496](../src/photo.c#L496) |
| `zune_extract_photo()` | public | [src/photo.c:512](../src/photo.c#L512) |

### src/playlist.c

| Function | Scope | Definition |
|---|---|---|
| `zune_get_playlists()` | public | [src/playlist.c:12](../src/playlist.c#L12) |
| `zune_free_playlists()` | public | [src/playlist.c:97](../src/playlist.c#L97) |
| `zune_forge_playlist()` | public | [src/playlist.c:124](../src/playlist.c#L124) |
| `zune_rewire_playlist()` | public | [src/playlist.c:198](../src/playlist.c#L198) |
| `zune_purge_playlist()` | public | [src/playlist.c:229](../src/playlist.c#L229) |

### src/podcast.c

| Function | Scope | Definition |
|---|---|---|
| `podcast_ready()` | static | [src/podcast.c:15](../src/podcast.c#L15) |
| `podcast_text_valid()` | static | [src/podcast.c:23](../src/podcast.c#L23) |
| `podcast_filename_valid()` | static | [src/podcast.c:44](../src/podcast.c#L44) |
| `podcast_read_text()` | static | [src/podcast.c:54](../src/podcast.c#L54) |
| `zune_free_podcast_series()` | public | [src/podcast.c:84](../src/podcast.c#L84) |
| `zune_get_podcast_series()` | public | [src/podcast.c:93](../src/podcast.c#L93) |
| `podcast_preflight()` | static | [src/podcast.c:123](../src/podcast.c#L123) |
| `podcast_date_readback()` | static | [src/podcast.c:141](../src/podcast.c#L141) |
| `podcast_title_filename()` | static | [src/podcast.c:159](../src/podcast.c#L159) |
| `podcast_log_filename()` | static | [src/podcast.c:170](../src/podcast.c#L170) |
| `podcast_verify_properties()` | static | [src/podcast.c:190](../src/podcast.c#L190) |
| `podcast_cleanup()` | static | [src/podcast.c:262](../src/podcast.c#L262) |
| `zune_forge_podcast_series()` | public | [src/podcast.c:275](../src/podcast.c#L275) |
| `podcast_date_valid()` | static | [src/podcast.c:312](../src/podcast.c#L312) |
| `zune_smuggle_podcast_episode()` | public | [src/podcast.c:329](../src/podcast.c#L329) |
| `zune_find_podcast_episode()` | public | [src/podcast.c:428](../src/podcast.c#L428) |
| `podcast_series_by_id()` | static | [src/podcast.c:485](../src/podcast.c#L485) |
| `podcast_series_by_feed()` | static | [src/podcast.c:491](../src/podcast.c#L491) |
| `podcast_source_by_identity()` | static | [src/podcast.c:496](../src/podcast.c#L496) |
| `podcast_source_by_id()` | static | [src/podcast.c:502](../src/podcast.c#L502) |
| `podcast_u32_order()` | static | [src/podcast.c:508](../src/podcast.c#L508) |
| `zune_free_podcast_episode_sources()` | public | [src/podcast.c:513](../src/podcast.c#L513) |
| `zune_get_podcast_episode_sources()` | public | [src/podcast.c:520](../src/podcast.c#L520) |

### src/podcast_state.c

| Function | Scope | Definition |
|---|---|---|
| `state_by_id()` | static | [src/podcast_state.c:14](../src/podcast_state.c#L14) |
| `state_by_offset()` | static | [src/podcast_state.c:19](../src/podcast_state.c#L19) |
| `state_utf8()` | static | [src/podcast_state.c:27](../src/podcast_state.c#L27) |
| `zune_parse_podcast_states()` | internal | [src/podcast_state.c:47](../src/podcast_state.c#L47) |
| `zune_free_podcast_states()` | public | [src/podcast_state.c:144](../src/podcast_state.c#L144) |
| `zune_get_podcast_states()` | public | [src/podcast_state.c:151](../src/podcast_state.c#L151) |

### src/ptp.c

| Function | Scope | Definition |
|---|---|---|
| `ptp_usb_write_timeout()` | static | [src/ptp.c:64](../src/ptp.c#L64) |
| `ptp_usb_write()` | static | [src/ptp.c:83](../src/ptp.c#L83) |
| `ptp_usb_read_timeout()` | static | [src/ptp.c:96](../src/ptp.c#L96) |
| `ptp_usb_read()` | static | [src/ptp.c:113](../src/ptp.c#L113) |
| `ptp_cancel_transaction()` | static | [src/ptp.c:129](../src/ptp.c#L129) |
| `ptp_send_command()` | static | [src/ptp.c:194](../src/ptp.c#L194) |
| `ptp_source_bytes()` | static | [src/ptp.c:246](../src/ptp.c#L246) |
| `ptp_send_data()` | static | [src/ptp.c:277](../src/ptp.c#L277) |
| `ptp_recv_data()` | static | [src/ptp.c:495](../src/ptp.c#L495) |
| `ptp_recv_response_timeout()` | static | [src/ptp.c:687](../src/ptp.c#L687) |
| `ptp_recv_response()` | static | [src/ptp.c:782](../src/ptp.c#L782) |
| `ptp_session_init()` | internal | [src/ptp.c:800](../src/ptp.c#L800) |
| `ptp_open_session()` | internal | [src/ptp.c:819](../src/ptp.c#L819) |
| `ptp_close_session()` | internal | [src/ptp.c:882](../src/ptp.c#L882) |
| `ptp_transaction_with()` | static | [src/ptp.c:931](../src/ptp.c#L931) |
| `ptp_transaction()` | internal | [src/ptp.c:1016](../src/ptp.c#L1016) |
| `ptp_transaction_file()` | internal | [src/ptp.c:1030](../src/ptp.c#L1030) |
| `ptp_get_device_prop_value()` | internal | [src/ptp.c:1060](../src/ptp.c#L1060) |
| `ptp_set_device_prop_value()` | internal | [src/ptp.c:1096](../src/ptp.c#L1096) |

### src/search.c

| Function | Scope | Definition |
|---|---|---|
| `zune_find_track()` | public | [src/search.c:14](../src/search.c#L14) |
| `zune_find_video()` | public | [src/search.c:40](../src/search.c#L40) |
| `zune_find_photo()` | public | [src/search.c:57](../src/search.c#L57) |

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
| `zune_free_audiobook_parts()` | public | [src/spoken_inventory.c:131](../src/spoken_inventory.c#L131) |
| `zune_get_audiobook_parts()` | public | [src/spoken_inventory.c:140](../src/spoken_inventory.c#L140) |

### src/thumbnail.c

| Function | Scope | Definition |
|---|---|---|
| `zune_brand()` | public | [src/thumbnail.c:16](../src/thumbnail.c#L16) |
| `le()` | static | [src/thumbnail.c:36](../src/thumbnail.c#L36) |
| `declared_max()` | static | [src/thumbnail.c:46](../src/thumbnail.c#L46) |
| `zune_get_brand_limits()` | public | [src/thumbnail.c:91](../src/thumbnail.c#L91) |
| `zune_grab_thumb()` | public | [src/thumbnail.c:140](../src/thumbnail.c#L140) |
| `zune_grab_photo_thumb()` | public | [src/thumbnail.c:174](../src/thumbnail.c#L174) |

### src/track.c

| Function | Scope | Definition |
|---|---|---|
| `read_u32_prop()` | static | [src/track.c:13](../src/track.c#L13) |
| `read_u16_prop()` | static | [src/track.c:31](../src/track.c#L31) |
| `zune_get_tracks()` | public | [src/track.c:47](../src/track.c#L47) |
| `zune_free_tracks()` | public | [src/track.c:170](../src/track.c#L170) |
| `get_file_size()` | static | [src/track.c:186](../src/track.c#L186) |
| `format_from_ext()` | static | [src/track.c:196](../src/track.c#L196) |
| `basename_of()` | static | [src/track.c:207](../src/track.c#L207) |
| `send_track_internal()` | static | [src/track.c:216](../src/track.c#L216) |
| `zune_smuggle_track()` | public | [src/track.c:282](../src/track.c#L282) |
| `zune_smuggle_track_tagged()` | public | [src/track.c:301](../src/track.c#L301) |
| `zune_smuggle_track_ex()` | public | [src/track.c:322](../src/track.c#L322) |
| `zune_purge_track()` | public | [src/track.c:358](../src/track.c#L358) |
| `zune_extract_track()` | public | [src/track.c:375](../src/track.c#L375) |
| `zune_verify()` | public | [src/track.c:393](../src/track.c#L393) |
| `zune_get_track_state()` | public | [src/track.c:459](../src/track.c#L459) |
| `zune_set_track_state()` | public | [src/track.c:502](../src/track.c#L502) |
| `zune_probe_object()` | public | [src/track.c:540](../src/track.c#L540) |
| `zune_probe_object_named()` | public | [src/track.c:548](../src/track.c#L548) |
| `zune_get_item_refs()` | public | [src/track.c:570](../src/track.c#L570) |

### src/transcode.c

| Function | Scope | Definition |
|---|---|---|
| `temp_output()` | static | [src/transcode.c:16](../src/transcode.c#L16) |
| `finish_output()` | static | [src/transcode.c:31](../src/transcode.c#L31) |
| `zune_arm_audio()` | public | [src/transcode.c:51](../src/transcode.c#L51) |
| `zune_retag()` | public | [src/transcode.c:83](../src/transcode.c#L83) |
| `model_to_profile()` | static | [src/transcode.c:153](../src/transcode.c#L153) |
| `metadata_arg()` | static | [src/transcode.c:165](../src/transcode.c#L165) |
| `transcode_video_impl()` | static | [src/transcode.c:175](../src/transcode.c#L175) |
| `zune_arm_video()` | public | [src/transcode.c:235](../src/transcode.c#L235) |
| `zune_arm_episode()` | public | [src/transcode.c:241](../src/transcode.c#L241) |

### src/upload.c

| Function | Scope | Definition |
|---|---|---|
| `source_name()` | static | [src/upload.c:17](../src/upload.c#L17) |
| `display_name()` | static | [src/upload.c:21](../src/upload.c#L21) |
| `zune_smuggle_core()` | internal | [src/upload.c:27](../src/upload.c#L27) |

### src/usb_libusb.c

| Function | Scope | Definition |
|---|---|---|
| `get_data()` | static | [src/usb_libusb.c:21](../src/usb_libusb.c#L21) |
| `libusb_backend_open()` | static | [src/usb_libusb.c:25](../src/usb_libusb.c#L25) |
| `libusb_backend_close()` | static | [src/usb_libusb.c:100](../src/usb_libusb.c#L100) |
| `libusb_backend_bulk_read()` | static | [src/usb_libusb.c:115](../src/usb_libusb.c#L115) |
| `libusb_backend_bulk_write()` | static | [src/usb_libusb.c:142](../src/usb_libusb.c#L142) |
| `libusb_backend_clear_halt()` | static | [src/usb_libusb.c:160](../src/usb_libusb.c#L160) |
| `libusb_backend_reset()` | static | [src/usb_libusb.c:166](../src/usb_libusb.c#L166) |
| `libusb_backend_class_request()` | static | [src/usb_libusb.c:172](../src/usb_libusb.c#L172) |
| `zune_usb_get_backend()` | internal | [src/usb_libusb.c:192](../src/usb_libusb.c#L192) |
| `zune_usb_find_device()` | internal | [src/usb_libusb.c:196](../src/usb_libusb.c#L196) |

### src/usb_recovery.c

| Function | Scope | Definition |
|---|---|---|
| `zune_unjam()` | public | [src/usb_recovery.c:13](../src/usb_recovery.c#L13) |

### src/util.c

| Function | Scope | Definition |
|---|---|---|
| `is_executable_file()` | static | [src/util.c:33](../src/util.c#L33) |
| `tool_search()` | static | [src/util.c:42](../src/util.c#L42) |
| `zune_tool_path()` | internal | [src/util.c:60](../src/util.c#L60) |
| `zune_mktemp()` | internal | [src/util.c:105](../src/util.c#L105) |
| `zune_tool_output_ok()` | internal | [src/util.c:153](../src/util.c#L153) |
| `tool_spawn()` | static | [src/util.c:162](../src/util.c#L162) |
| `tool_resolve()` | static | [src/util.c:186](../src/util.c#L186) |
| `tool_wait()` | static | [src/util.c:199](../src/util.c#L199) |
| `zune_run_tool()` | internal | [src/util.c:214](../src/util.c#L214) |
| `path_get_dirname()` | static | [src/util.c:236](../src/util.c#L236) |
| `path_get_basename()` | static | [src/util.c:255](../src/util.c#L255) |
| `strip_trim()` | static | [src/util.c:272](../src/util.c#L272) |
| `clean_series_name()` | static | [src/util.c:293](../src/util.c#L293) |
| `read_number()` | static | [src/util.c:331](../src/util.c#L331) |
| `decode_season_episode()` | static | [src/util.c:344](../src/util.c#L344) |
| `zune_decode_filename()` | public | [src/util.c:438](../src/util.c#L438) |
| `zune_snap_thumb()` | public | [src/util.c:509](../src/util.c#L509) |
| `probe_space()` | static | [src/util.c:572](../src/util.c#L572) |
| `probe_trim()` | static | [src/util.c:588](../src/util.c#L588) |
| `probe_tags_free()` | static | [src/util.c:611](../src/util.c#L611) |
| `probe_number()` | static | [src/util.c:617](../src/util.c#L617) |
| `probe_two_digits()` | static | [src/util.c:637](../src/util.c#L637) |
| `probe_year()` | static | [src/util.c:647](../src/util.c#L647) |
| `probe_text()` | static | [src/util.c:697](../src/util.c#L697) |
| `probe_numeric()` | static | [src/util.c:707](../src/util.c#L707) |
| `probe_fill()` | static | [src/util.c:718](../src/util.c#L718) |
| `probe_duration()` | static | [src/util.c:736](../src/util.c#L736) |
| `probe_dictionary()` | static | [src/util.c:747](../src/util.c#L747) |
| `probe_selected_stream()` | static | [src/util.c:759](../src/util.c#L759) |
| `zune_probe()` | public | [src/util.c:784](../src/util.c#L784) |
| `probe_flat_value()` | static | [src/util.c:825](../src/util.c#L825) |
| `probe_flat_tag()` | static | [src/util.c:857](../src/util.c#L857) |
| `probe_flat_duration()` | static | [src/util.c:866](../src/util.c#L866) |
| `zune_probe()` | public | [src/util.c:874](../src/util.c#L874) |
| `zune_free_metadata()` | public | [src/util.c:978](../src/util.c#L978) |

### src/video.c

| Function | Scope | Definition |
|---|---|---|
| `simple_utf8_to_ucs2()` | static | [src/video.c:22](../src/video.c#L22) |
| `is_video_format()` | static | [src/video.c:52](../src/video.c#L52) |
| `format_from_extension()` | static | [src/video.c:62](../src/video.c#L62) |
| `send_representative_sample()` | static | [src/video.c:77](../src/video.c#L77) |
| `set_video_metadata()` | static | [src/video.c:107](../src/video.c#L107) |
| `set_video_series()` | static | [src/video.c:146](../src/video.c#L146) |
| `send_video_file()` | static | [src/video.c:168](../src/video.c#L168) |
| `zune_get_videos()` | public | [src/video.c:220](../src/video.c#L220) |
| `zune_free_videos()` | public | [src/video.c:339](../src/video.c#L339) |
| `zune_smuggle_video_named()` | public | [src/video.c:349](../src/video.c#L349) |
| `zune_smuggle_movie()` | public | [src/video.c:394](../src/video.c#L394) |
| `zune_get_series_info()` | public | [src/video.c:418](../src/video.c#L418) |
| `zune_smuggle_episode()` | public | [src/video.c:472](../src/video.c#L472) |
| `zune_smuggle_clip()` | public | [src/video.c:517](../src/video.c#L517) |
| `zune_smuggle_other()` | public | [src/video.c:541](../src/video.c#L541) |
| `zune_purge_video()` | public | [src/video.c:564](../src/video.c#L564) |
| `zune_extract_video()` | public | [src/video.c:580](../src/video.c#L580) |

### src/wiretap.c

| Function | Scope | Definition |
|---|---|---|
| `chatter_from_environment()` | static | [src/wiretap.c:22](../src/wiretap.c#L22) |
| `zune_set_chatter()` | public | [src/wiretap.c:35](../src/wiretap.c#L35) |
| `zune_wiretap()` | public | [src/wiretap.c:42](../src/wiretap.c#L42) |
| `zune_log_codes()` | internal | [src/wiretap.c:50](../src/wiretap.c#L50) |
| `zune_log()` | internal | [src/wiretap.c:66](../src/wiretap.c#L66) |

### src/zmdb.c

| Function | Scope | Definition |
|---|---|---|
| `zmdb_span_ok()` | static | [src/zmdb.c:71](../src/zmdb.c#L71) |
| `zmdb_array_ok()` | static | [src/zmdb.c:75](../src/zmdb.c#L75) |
| `zmdb_log_hex()` | static | [src/zmdb.c:79](../src/zmdb.c#L79) |
| `rdi32()` | static | [src/zmdb.c:89](../src/zmdb.c#L89) |
| `rd64()` | static | [src/zmdb.c:93](../src/zmdb.c#L93) |
| `read_utf8()` | static | [src/zmdb.c:98](../src/zmdb.c#L98) |
| `zmdb_index_order()` | static | [src/zmdb.c:150](../src/zmdb.c#L150) |
| `zmdb_find_record()` | static | [src/zmdb.c:158](../src/zmdb.c#L158) |
| `zmdb_resolve_string()` | static | [src/zmdb.c:175](../src/zmdb.c#L175) |
| `zmdb_parse_varint_fields()` | static | [src/zmdb.c:230](../src/zmdb.c#L230) |
| `zmdb_parse_track()` | static | [src/zmdb.c:301](../src/zmdb.c#L301) |
| `zmdb_parse_video()` | static | [src/zmdb.c:376](../src/zmdb.c#L376) |
| `zmdb_parse_picture()` | static | [src/zmdb.c:446](../src/zmdb.c#L446) |
| `zmdb_parse_album()` | static | [src/zmdb.c:470](../src/zmdb.c#L470) |
| `zmdb_parse_artist()` | static | [src/zmdb.c:503](../src/zmdb.c#L503) |
| `zmdb_parse_photo_album()` | static | [src/zmdb.c:524](../src/zmdb.c#L524) |
| `zmdb_discover()` | static | [src/zmdb.c:566](../src/zmdb.c#L566) |
| `zmdb_fail()` | static | [src/zmdb.c:610](../src/zmdb.c#L610) |
| `zmdb_load()` | static | [src/zmdb.c:619](../src/zmdb.c#L619) |
| `zmdb_desc_ok()` | static | [src/zmdb.c:720](../src/zmdb.c#L720) |
| `zmdb_media_slots_ok()` | static | [src/zmdb.c:734](../src/zmdb.c#L734) |
| `zmdb_desc_atom()` | static | [src/zmdb.c:744](../src/zmdb.c#L744) |
| `zmdb_parse()` | static | [src/zmdb.c:750](../src/zmdb.c#L750) |
| `zune_infiltrate_legacy()` | public | [src/zmdb.c:842](../src/zmdb.c#L842) |
| `zmdb_full_preflight()` | static | [src/zmdb.c:872](../src/zmdb.c#L872) |
| `zmdb_alloc_library()` | static | [src/zmdb.c:898](../src/zmdb.c#L898) |
| `zmdb_parse_media()` | static | [src/zmdb.c:930](../src/zmdb.c#L930) |
| `zmdb_log_playlists()` | static | [src/zmdb.c:963](../src/zmdb.c#L963) |
| `zmdb_parse_full()` | static | [src/zmdb.c:979](../src/zmdb.c#L979) |
| `zune_infiltrate()` | public | [src/zmdb.c:1000](../src/zmdb.c#L1000) |
| `zune_free_scan()` | public | [src/zmdb.c:1044](../src/zmdb.c#L1044) |
| `zune_dump_raw()` | public | [src/zmdb.c:1090](../src/zmdb.c#L1090) |
| `zune_infiltrate_deep()` | public | [src/zmdb.c:1122](../src/zmdb.c#L1122) |
| `zune_free_library()` | public | [src/zmdb.c:1256](../src/zmdb.c#L1256) |

### src/zmdb_io.c

| Function | Scope | Definition |
|---|---|---|
| `zune_zmdb_read_response()` | internal | [src/zmdb_io.c:8](../src/zmdb_io.c#L8) |
| `zune_zmdb_read_payload()` | internal | [src/zmdb_io.c:150](../src/zmdb_io.c#L150) |

### src/zune_internal.h

| Function | Scope | Definition |
|---|---|---|
| `zune_response_uncertain()` | static | [src/zune_internal.h:86](../src/zune_internal.h#L86) |
| `zune_wound_is_mortal()` | static | [src/zune_internal.h:137](../src/zune_internal.h#L137) |
| `zune_filename_no_ext()` | static | [src/zune_internal.h:217](../src/zune_internal.h#L217) |
| `zune_prop_listed()` | static | [src/zune_internal.h:236](../src/zune_internal.h#L236) |

### src/hd/hd_art_gallery.h

| Function | Scope | Definition |
|---|---|---|
| `hd_art_gallery_release()` | static | [src/hd/hd_art_gallery.h:18](../src/hd/hd_art_gallery.h#L18) |
| `hd_art_jpeg_decode()` | static | [src/hd/hd_art_gallery.h:23](../src/hd/hd_art_gallery.h#L23) |
| `hd_art_gallery_load()` | static | [src/hd/hd_art_gallery.h:49](../src/hd/hd_art_gallery.h#L49) |
| `hd_art_gallery_load_roles()` | static | [src/hd/hd_art_gallery.h:76](../src/hd/hd_art_gallery.h#L76) |
| `hd_art_gallery_configure()` | static | [src/hd/hd_art_gallery.h:100](../src/hd/hd_art_gallery.h#L100) |

### src/hd/hd_artist_binding.h

| Function | Scope | Definition |
|---|---|---|
| `hd_bind_nonzero()` | static | [src/hd/hd_artist_binding.h:28](../src/hd/hd_artist_binding.h#L28) |
| `hd_bind_ready()` | static | [src/hd/hd_artist_binding.h:33](../src/hd/hd_artist_binding.h#L33) |
| `hd_bind_wire_string()` | static | [src/hd/hd_artist_binding.h:39](../src/hd/hd_artist_binding.h#L39) |
| `hd_bind_name_matches()` | static | [src/hd/hd_artist_binding.h:45](../src/hd/hd_artist_binding.h#L45) |
| `hd_bind_valid_snapshot()` | static | [src/hd/hd_artist_binding.h:73](../src/hd/hd_artist_binding.h#L73) |
| `hd_bind_same_device()` | static | [src/hd/hd_artist_binding.h:81](../src/hd/hd_artist_binding.h#L81) |
| `hd_bind_get()` | static | [src/hd/hd_artist_binding.h:84](../src/hd/hd_artist_binding.h#L84) |
| `hd_bind_descriptor()` | static | [src/hd/hd_artist_binding.h:97](../src/hd/hd_artist_binding.h#L97) |
| `hd_bind_read_state()` | static | [src/hd/hd_artist_binding.h:109](../src/hd/hd_artist_binding.h#L109) |
| `hd_bind_matches()` | static | [src/hd/hd_artist_binding.h:130](../src/hd/hd_artist_binding.h#L130) |
| `hd_artist_binding_snapshot()` | static | [src/hd/hd_artist_binding.h:142](../src/hd/hd_artist_binding.h#L142) |
| `hd_bind_explicit_refusal()` | static | [src/hd/hd_artist_binding.h:153](../src/hd/hd_artist_binding.h#L153) |
| `hd_bind_write_guarded()` | static | [src/hd/hd_artist_binding.h:158](../src/hd/hd_artist_binding.h#L158) |
| `hd_bind_write()` | static | [src/hd/hd_artist_binding.h:186](../src/hd/hd_artist_binding.h#L186) |
| `hd_artist_binding_apply_guarded()` | static | [src/hd/hd_artist_binding.h:190](../src/hd/hd_artist_binding.h#L190) |
| `hd_artist_binding_apply()` | static | [src/hd/hd_artist_binding.h:197](../src/hd/hd_artist_binding.h#L197) |
| `hd_artist_binding_restore()` | static | [src/hd/hd_artist_binding.h:203](../src/hd/hd_artist_binding.h#L203) |

### src/hd/hd_artist_probe.h

| Function | Scope | Definition |
|---|---|---|
| `hd_art_u16()` | static | [src/hd/hd_artist_probe.h:9](../src/hd/hd_artist_probe.h#L9) |
| `hd_art_u32()` | static | [src/hd/hd_artist_probe.h:12](../src/hd/hd_artist_probe.h#L12) |
| `hd_art_string()` | static | [src/hd/hd_artist_probe.h:17](../src/hd/hd_artist_probe.h#L17) |
| `hd_art_sample_count()` | static | [src/hd/hd_artist_probe.h:36](../src/hd/hd_artist_probe.h#L36) |
| `hd_art_stopped()` | static | [src/hd/hd_artist_probe.h:41](../src/hd/hd_artist_probe.h#L41) |
| `hd_art_read()` | static | [src/hd/hd_artist_probe.h:44](../src/hd/hd_artist_probe.h#L44) |
| `hd_art_operations()` | static | [src/hd/hd_artist_probe.h:55](../src/hd/hd_artist_probe.h#L55) |
| `hd_artist_probe_report()` | static | [src/hd/hd_artist_probe.h:94](../src/hd/hd_artist_probe.h#L94) |

### src/hd/hd_cli_teardown.h

| Function | Scope | Definition |
|---|---|---|
| `hd_cli_transaction_uncertain()` | static | [src/hd/hd_cli_teardown.h:7](../src/hd/hd_cli_teardown.h#L7) |
| `hd_cli_sever()` | static | [src/hd/hd_cli_teardown.h:14](../src/hd/hd_cli_teardown.h#L14) |

### src/hd/hd_http_init.h

| Function | Scope | Definition |
|---|---|---|
| `hd_http_init_run()` | static | [src/hd/hd_http_init.h:29](../src/hd/hd_http_init.h#L29) |
| `hd_http_init()` | static | [src/hd/hd_http_init.h:71](../src/hd/hd_http_init.h#L71) |
| `hd_http_init_full_ex()` | static | [src/hd/hd_http_init.h:84](../src/hd/hd_http_init.h#L84) |
| `hd_http_init_full()` | static | [src/hd/hd_http_init.h:140](../src/hd/hd_http_init.h#L140) |

### src/hd/hd_image_server.h

| Function | Scope | Definition |
|---|---|---|
| `hdis_u16()` | static | [src/hd/hd_image_server.h:80](../src/hd/hd_image_server.h#L80) |
| `hdis_u32()` | static | [src/hd/hd_image_server.h:83](../src/hd/hd_image_server.h#L83) |
| `hdis_w16()` | static | [src/hd/hd_image_server.h:87](../src/hd/hd_image_server.h#L87) |
| `hdis_w32()` | static | [src/hd/hd_image_server.h:91](../src/hd/hd_image_server.h#L91) |
| `hdis_sum()` | static | [src/hd/hd_image_server.h:97](../src/hd/hd_image_server.h#L97) |
| `hdis_fold()` | static | [src/hd/hd_image_server.h:107](../src/hd/hd_image_server.h#L107) |
| `hdis_checksum()` | static | [src/hd/hd_image_server.h:112](../src/hd/hd_image_server.h#L112) |
| `hdis_transport_sum()` | static | [src/hd/hd_image_server.h:115](../src/hd/hd_image_server.h#L115) |
| `hdis_ip()` | static | [src/hd/hd_image_server.h:120](../src/hd/hd_image_server.h#L120) |
| `hdis_art_host()` | static | [src/hd/hd_image_server.h:136](../src/hd/hd_image_server.h#L136) |
| `hdis_host()` | static | [src/hd/hd_image_server.h:141](../src/hd/hd_image_server.h#L141) |
| `hdis_xml_text()` | static | [src/hd/hd_image_server.h:160](../src/hd/hd_image_server.h#L160) |
| `hdis_xml_name()` | static | [src/hd/hd_image_server.h:220](../src/hd/hd_image_server.h#L220) |
| `hdis_biography_xml()` | static | [src/hd/hd_image_server.h:229](../src/hd/hd_image_server.h#L229) |
| `hd_image_server_set_biography()` | static | [src/hd/hd_image_server.h:260](../src/hd/hd_image_server.h#L260) |
| `hdis_jpeg_dimensions()` | static | [src/hd/hd_image_server.h:271](../src/hd/hd_image_server.h#L271) |
| `hd_image_server_set_artist()` | static | [src/hd/hd_image_server.h:314](../src/hd/hd_image_server.h#L314) |
| `hdis_set_assets()` | static | [src/hd/hd_image_server.h:403](../src/hd/hd_image_server.h#L403) |
| `hd_image_server_set_assets()` | static | [src/hd/hd_image_server.h:477](../src/hd/hd_image_server.h#L477) |
| `hd_image_server_set_asset_roles()` | static | [src/hd/hd_image_server.h:485](../src/hd/hd_image_server.h#L485) |
| `hdis_image_query()` | static | [src/hd/hd_image_server.h:493](../src/hd/hd_image_server.h#L493) |
| `hdis_catalog_route()` | static | [src/hd/hd_image_server.h:544](../src/hd/hd_image_server.h#L544) |
| `hdis_tcp()` | static | [src/hd/hd_image_server.h:656](../src/hd/hd_image_server.h#L656) |
| `hdis_dns()` | static | [src/hd/hd_image_server.h:686](../src/hd/hd_image_server.h#L686) |
| `hdis_send_response()` | static | [src/hd/hd_image_server.h:735](../src/hd/hd_image_server.h#L735) |
| `hdis_request()` | static | [src/hd/hd_image_server.h:767](../src/hd/hd_image_server.h#L767) |
| `hd_image_server_init()` | static | [src/hd/hd_image_server.h:863](../src/hd/hd_image_server.h#L863) |
| `hd_image_server_packet()` | static | [src/hd/hd_image_server.h:902](../src/hd/hd_image_server.h#L902) |
| `hd_image_server_tick()` | static | [src/hd/hd_image_server.h:1110](../src/hd/hd_image_server.h#L1110) |

### src/hd/hd_network_probe.h

| Function | Scope | Definition |
|---|---|---|
| `hd_network_be16()` | static | [src/hd/hd_network_probe.h:41](../src/hd/hd_network_probe.h#L41) |
| `hd_network_unexpected()` | static | [src/hd/hd_network_probe.h:44](../src/hd/hd_network_probe.h#L44) |
| `hd_network_checksum()` | static | [src/hd/hd_network_probe.h:53](../src/hd/hd_network_probe.h#L53) |
| `hd_network_emit()` | static | [src/hd/hd_network_probe.h:59](../src/hd/hd_network_probe.h#L59) |
| `hd_network_reply()` | static | [src/hd/hd_network_probe.h:69](../src/hd/hd_network_probe.h#L69) |
| `hd_network_ip_summary()` | static | [src/hd/hd_network_probe.h:75](../src/hd/hd_network_probe.h#L75) |
| `hd_network_packet()` | static | [src/hd/hd_network_probe.h:101](../src/hd/hd_network_probe.h#L101) |
| `hd_network_frame()` | static | [src/hd/hd_network_probe.h:176](../src/hd/hd_network_probe.h#L176) |
| `hd_network_feed()` | static | [src/hd/hd_network_probe.h:188](../src/hd/hd_network_probe.h#L188) |
| `hd_network_startup_feed()` | static | [src/hd/hd_network_probe.h:214](../src/hd/hd_network_probe.h#L214) |
| `hd_network_client_reply()` | static | [src/hd/hd_network_probe.h:235](../src/hd/hd_network_probe.h#L235) |
| `hd_network_encode()` | static | [src/hd/hd_network_probe.h:242](../src/hd/hd_network_probe.h#L242) |
| `hd_network_usb_send()` | static | [src/hd/hd_network_probe.h:255](../src/hd/hd_network_probe.h#L255) |
| `hd_network_probe_run_detail()` | static | [src/hd/hd_network_probe.h:267](../src/hd/hd_network_probe.h#L267) |
| `hd_network_probe_run_ex_lifecycle()` | static | [src/hd/hd_network_probe.h:340](../src/hd/hd_network_probe.h#L340) |
| `hd_network_probe_run_ex_status()` | static | [src/hd/hd_network_probe.h:346](../src/hd/hd_network_probe.h#L346) |
| `hd_network_probe_run_ex()` | static | [src/hd/hd_network_probe.h:351](../src/hd/hd_network_probe.h#L351) |
| `hd_network_probe_run()` | static | [src/hd/hd_network_probe.h:356](../src/hd/hd_network_probe.h#L356) |

### src/hd/hd_tunnel_probe.h

| Function | Scope | Definition |
|---|---|---|
| `hd_tunnel_fcs()` | static | [src/hd/hd_tunnel_probe.h:25](../src/hd/hd_tunnel_probe.h#L25) |
| `hd_tunnel_lcp()` | static | [src/hd/hd_tunnel_probe.h:34](../src/hd/hd_tunnel_probe.h#L34) |
| `hd_tunnel_feed()` | static | [src/hd/hd_tunnel_probe.h:58](../src/hd/hd_tunnel_probe.h#L58) |
| `hd_tunnel_ack()` | static | [src/hd/hd_tunnel_probe.h:76](../src/hd/hd_tunnel_probe.h#L76) |
| `hd_tunnel_now_ms()` | static | [src/hd/hd_tunnel_probe.h:96](../src/hd/hd_tunnel_probe.h#L96) |
| `hd_tunnel_pause()` | static | [src/hd/hd_tunnel_probe.h:104](../src/hd/hd_tunnel_probe.h#L104) |
| `hd_tunnel_ready()` | static | [src/hd/hd_tunnel_probe.h:120](../src/hd/hd_tunnel_probe.h#L120) |
| `hd_tunnel_transaction()` | static | [src/hd/hd_tunnel_probe.h:130](../src/hd/hd_tunnel_probe.h#L130) |
| `hd_tunnel_probe_report()` | static | [src/hd/hd_tunnel_probe.h:156](../src/hd/hd_tunnel_probe.h#L156) |

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
| [include/zune.h](../include/zune.h) | 1006 |
| [src/album.c](../src/album.c) | 450 |
| [src/audiobook.c](../src/audiobook.c) | 338 |
| [src/bytes.h](../src/bytes.h) | 44 |
| [src/device.c](../src/device.c) | 583 |
| [src/devinfo.c](../src/devinfo.c) | 65 |
| [src/driverkit_usb.c](../src/driverkit_usb.c) | 435 |
| [src/finalize.c](../src/finalize.c) | 143 |
| [src/hd_artwork.c](../src/hd_artwork.c) | 423 |
| [src/mtp.c](../src/mtp.c) | 1772 |
| [src/mtp.h](../src/mtp.h) | 631 |
| [src/mtp_text.h](../src/mtp_text.h) | 84 |
| [src/mtpz.c](../src/mtpz.c) | 2236 |
| [src/mtpz.h](../src/mtpz.h) | 45 |
| [src/photo.c](../src/photo.c) | 526 |
| [src/playlist.c](../src/playlist.c) | 242 |
| [src/podcast.c](../src/podcast.c) | 630 |
| [src/podcast_state.c](../src/podcast_state.c) | 177 |
| [src/ptp.c](../src/ptp.c) | 1115 |
| [src/ptp.h](../src/ptp.h) | 340 |
| [src/search.c](../src/search.c) | 70 |
| [src/spoken_inventory.c](../src/spoken_inventory.c) | 170 |
| [src/thumbnail.c](../src/thumbnail.c) | 244 |
| [src/track.c](../src/track.c) | 577 |
| [src/transcode.c](../src/transcode.c) | 246 |
| [src/upload.c](../src/upload.c) | 137 |
| [src/usb.h](../src/usb.h) | 146 |
| [src/usb_libusb.c](../src/usb_libusb.c) | 209 |
| [src/usb_recovery.c](../src/usb_recovery.c) | 41 |
| [src/util.c](../src/util.c) | 983 |
| [src/video.c](../src/video.c) | 594 |
| [src/wiretap.c](../src/wiretap.c) | 103 |
| [src/wiretap.h](../src/wiretap.h) | 28 |
| [src/zmdb.c](../src/zmdb.c) | 1274 |
| [src/zmdb_io.c](../src/zmdb_io.c) | 153 |
| [src/zune_internal.h](../src/zune_internal.h) | 244 |
| [src/hd/hd_art_gallery.h](../src/hd/hd_art_gallery.h) | 107 |
| [src/hd/hd_artist_binding.h](../src/hd/hd_artist_binding.h) | 209 |
| [src/hd/hd_artist_probe.h](../src/hd/hd_artist_probe.h) | 191 |
| [src/hd/hd_cli_teardown.h](../src/hd/hd_cli_teardown.h) | 27 |
| [src/hd/hd_http_init.h](../src/hd/hd_http_init.h) | 143 |
| [src/hd/hd_image_server.h](../src/hd/hd_image_server.h) | 1131 |
| [src/hd/hd_network_probe.h](../src/hd/hd_network_probe.h) | 360 |
| [src/hd/hd_tunnel_probe.h](../src/hd/hd_tunnel_probe.h) | 239 |

## Integration examples

- [examples/README.md](../examples/README.md)
- [examples/audiobook_send.c](../examples/audiobook_send.c)
- [examples/common.h](../examples/common.h)
- [examples/device_info.c](../examples/device_info.c)
- [examples/hd_artwork.c](../examples/hd_artwork.c)
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
- [tools/hd_art_album_recovery.h](../tools/hd_art_album_recovery.h)
  - `hd_album_recovery_take()`: [tools/hd_art_album_recovery.h:7](../tools/hd_art_album_recovery.h#L7)
  - `hd_album_recovery_record()`: [tools/hd_art_album_recovery.h:16](../tools/hd_art_album_recovery.h#L16)
  - `hd_album_recovery_object()`: [tools/hd_art_album_recovery.h:25](../tools/hd_art_album_recovery.h#L25)
  - `hd_album_recovery_load()`: [tools/hd_art_album_recovery.h:51](../tools/hd_art_album_recovery.h#L51)
  - `hd_album_recovery_classify()`: [tools/hd_art_album_recovery.h:254](../tools/hd_art_album_recovery.h#L254)
  - `hd_album_recovery_inspect()`: [tools/hd_art_album_recovery.h:327](../tools/hd_art_album_recovery.h#L327)
  - `hd_album_recovery_prepare()`: [tools/hd_art_album_recovery.h:337](../tools/hd_art_album_recovery.h#L337)
- [tools/hd_art_album_sync.h](../tools/hd_art_album_sync.h)
  - `hd_album_string()`: [tools/hd_art_album_sync.h:50](../tools/hd_art_album_sync.h#L50)
  - `hd_album_text()`: [tools/hd_art_album_sync.h:91](../tools/hd_art_album_sync.h#L91)
  - `hd_album_number()`: [tools/hd_art_album_sync.h:99](../tools/hd_art_album_sync.h#L99)
  - `hd_album_read_file()`: [tools/hd_art_album_sync.h:116](../tools/hd_art_album_sync.h#L116)
  - `hd_album_release()`: [tools/hd_art_album_sync.h:158](../tools/hd_art_album_sync.h#L158)
  - `hd_album_local()`: [tools/hd_art_album_sync.h:171](../tools/hd_art_album_sync.h#L171)
  - `hd_album_allowed()`: [tools/hd_art_album_sync.h:277](../tools/hd_art_album_sync.h#L277)
  - `hd_album_record()`: [tools/hd_art_album_sync.h:299](../tools/hd_art_album_sync.h#L299)
  - `hd_album_tx()`: [tools/hd_art_album_sync.h:327](../tools/hd_art_album_sync.h#L327)
  - `hd_album_get()`: [tools/hd_art_album_sync.h:366](../tools/hd_art_album_sync.h#L366)
  - `hd_album_value()`: [tools/hd_art_album_sync.h:372](../tools/hd_art_album_sync.h#L372)
  - `hd_album_handles()`: [tools/hd_art_album_sync.h:383](../tools/hd_art_album_sync.h#L383)
  - `hd_album_mismatch()`: [tools/hd_art_album_sync.h:422](../tools/hd_art_album_sync.h#L422)
  - `hd_album_expect()`: [tools/hd_art_album_sync.h:436](../tools/hd_art_album_sync.h#L436)
  - `hd_album_expect_number()`: [tools/hd_art_album_sync.h:447](../tools/hd_art_album_sync.h#L447)
  - `hd_album_same_text()`: [tools/hd_art_album_sync.h:453](../tools/hd_art_album_sync.h#L453)
  - `hd_album_verify()`: [tools/hd_art_album_sync.h:467](../tools/hd_art_album_sync.h#L467)
  - `hd_album_payload()`: [tools/hd_art_album_sync.h:577](../tools/hd_art_album_sync.h#L577)
  - `hd_album_configure()`: [tools/hd_art_album_sync.h:587](../tools/hd_art_album_sync.h#L587)
  - `hd_album_absent()`: [tools/hd_art_album_sync.h:619](../tools/hd_art_album_sync.h#L619)
  - `hd_album_set_guid()`: [tools/hd_art_album_sync.h:645](../tools/hd_art_album_sync.h#L645)
  - `hd_album_guid_absent()`: [tools/hd_art_album_sync.h:679](../tools/hd_art_album_sync.h#L679)
  - `hd_album_plan_journal()`: [tools/hd_art_album_sync.h:696](../tools/hd_art_album_sync.h#L696)
  - `hd_album_prepare()`: [tools/hd_art_album_sync.h:746](../tools/hd_art_album_sync.h#L746)
  - `hd_album_prop_string()`: [tools/hd_art_album_sync.h:782](../tools/hd_art_album_sync.h#L782)
  - `hd_album_props()`: [tools/hd_art_album_sync.h:791](../tools/hd_art_album_sync.h#L791)
  - `hd_album_new_id()`: [tools/hd_art_album_sync.h:840](../tools/hd_art_album_sync.h#L840)
  - `hd_album_create()`: [tools/hd_art_album_sync.h:852](../tools/hd_art_album_sync.h#L852)
  - `hd_album_refs()`: [tools/hd_art_album_sync.h:904](../tools/hd_art_album_sync.h#L904)
  - `hd_album_link()`: [tools/hd_art_album_sync.h:917](../tools/hd_art_album_sync.h#L917)
  - `hd_album_cover()`: [tools/hd_art_album_sync.h:938](../tools/hd_art_album_sync.h#L938)
  - `hd_album_id_present()`: [tools/hd_art_album_sync.h:967](../tools/hd_art_album_sync.h#L967)
  - `hd_album_extra_verify()`: [tools/hd_art_album_sync.h:974](../tools/hd_art_album_sync.h#L974)
  - `hd_album_extras_present()`: [tools/hd_art_album_sync.h:1033](../tools/hd_art_album_sync.h#L1033)
  - `hd_album_closed_links()`: [tools/hd_art_album_sync.h:1046](../tools/hd_art_album_sync.h#L1046)
  - `hd_album_prefix_proof()`: [tools/hd_art_album_sync.h:1088](../tools/hd_art_album_sync.h#L1088)
  - `hd_album_tick()`: [tools/hd_art_album_sync.h:1178](../tools/hd_art_album_sync.h#L1178)
  - `hd_album_inventory()`: [tools/hd_art_album_sync.h:1228](../tools/hd_art_album_sync.h#L1228)
  - `hd_album_complete()`: [tools/hd_art_album_sync.h:1261](../tools/hd_art_album_sync.h#L1261)
- [tools/hd_art_bind_ready.h](../tools/hd_art_bind_ready.h)
  - `hd_art_ready_options()`: [tools/hd_art_bind_ready.h:22](../tools/hd_art_bind_ready.h#L22)
  - `hd_art_ready_now()`: [tools/hd_art_bind_ready.h:30](../tools/hd_art_bind_ready.h#L30)
  - `hd_art_ready_init()`: [tools/hd_art_bind_ready.h:34](../tools/hd_art_bind_ready.h#L34)
  - `hd_art_ready_cancelled()`: [tools/hd_art_bind_ready.h:40](../tools/hd_art_bind_ready.h#L40)
  - `hd_art_ready_write_allowed()`: [tools/hd_art_bind_ready.h:43](../tools/hd_art_bind_ready.h#L43)
  - `hd_art_ready_prepare()`: [tools/hd_art_bind_ready.h:48](../tools/hd_art_bind_ready.h#L48)
  - `hd_art_ready_tick()`: [tools/hd_art_bind_ready.h:66](../tools/hd_art_bind_ready.h#L66)
  - `hd_art_ready_finish()`: [tools/hd_art_bind_ready.h:86](../tools/hd_art_bind_ready.h#L86)
- [tools/hd_art_binding_cli.h](../tools/hd_art_binding_cli.h)
  - `cmd_hd_art_bind()`: [tools/hd_art_binding_cli.h:6](../tools/hd_art_binding_cli.h#L6)
  - `cmd_hd_art_restore()`: [tools/hd_art_binding_cli.h:24](../tools/hd_art_binding_cli.h#L24)
- [tools/hd_art_first_recovery.h](../tools/hd_art_first_recovery.h)
  - `hd_first_read_all()`: [tools/hd_art_first_recovery.h:5](../tools/hd_art_first_recovery.h#L5)
  - `hd_first_recovery_load()`: [tools/hd_art_first_recovery.h:18](../tools/hd_art_first_recovery.h#L18)
  - `hd_first_recovery_inspect()`: [tools/hd_art_first_recovery.h:107](../tools/hd_art_first_recovery.h#L107)
  - `hd_first_recovery_proof()`: [tools/hd_art_first_recovery.h:197](../tools/hd_art_first_recovery.h#L197)
  - `hd_first_recovery_delete()`: [tools/hd_art_first_recovery.h:231](../tools/hd_art_first_recovery.h#L231)
- [tools/hd_art_first_sync.h](../tools/hd_art_first_sync.h)
  - `hd_first_u16()`: [tools/hd_art_first_sync.h:43](../tools/hd_art_first_sync.h#L43)
  - `hd_first_u32()`: [tools/hd_art_first_sync.h:47](../tools/hd_art_first_sync.h#L47)
  - `hd_first_write_all()`: [tools/hd_art_first_sync.h:51](../tools/hd_art_first_sync.h#L51)
  - `hd_first_record()`: [tools/hd_art_first_sync.h:80](../tools/hd_art_first_sync.h#L80)
  - `hd_first_identity()`: [tools/hd_art_first_sync.h:102](../tools/hd_art_first_sync.h#L102)
  - `hd_first_allowed()`: [tools/hd_art_first_sync.h:108](../tools/hd_art_first_sync.h#L108)
  - `hd_first_tx()`: [tools/hd_art_first_sync.h:116](../tools/hd_art_first_sync.h#L116)
  - `hd_first_get()`: [tools/hd_art_first_sync.h:176](../tools/hd_art_first_sync.h#L176)
  - `hd_first_children()`: [tools/hd_art_first_sync.h:184](../tools/hd_art_first_sync.h#L184)
  - `hd_first_handles()`: [tools/hd_art_first_sync.h:225](../tools/hd_art_first_sync.h#L225)
  - `hd_first_name_match()`: [tools/hd_art_first_sync.h:229](../tools/hd_art_first_sync.h#L229)
  - `hd_first_value()`: [tools/hd_art_first_sync.h:242](../tools/hd_art_first_sync.h#L242)
  - `hd_first_verify()`: [tools/hd_art_first_sync.h:256](../tools/hd_art_first_sync.h#L256)
  - `hd_first_configure()`: [tools/hd_art_first_sync.h:358](../tools/hd_art_first_sync.h#L358)
  - `hd_first_roots()`: [tools/hd_art_first_sync.h:392](../tools/hd_art_first_sync.h#L392)
  - `hd_first_absent_names()`: [tools/hd_art_first_sync.h:451](../tools/hd_art_first_sync.h#L451)
  - `hd_first_new_handle()`: [tools/hd_art_first_sync.h:479](../tools/hd_art_first_sync.h#L479)
  - `hd_first_prop()`: [tools/hd_art_first_sync.h:493](../tools/hd_art_first_sync.h#L493)
  - `hd_first_str()`: [tools/hd_art_first_sync.h:502](../tools/hd_art_first_sync.h#L502)
  - `hd_first_props()`: [tools/hd_art_first_sync.h:511](../tools/hd_art_first_sync.h#L511)
  - `hd_first_create()`: [tools/hd_art_first_sync.h:552](../tools/hd_art_first_sync.h#L552)
  - `hd_first_refs_check()`: [tools/hd_art_first_sync.h:636](../tools/hd_art_first_sync.h#L636)
  - `hd_first_refs_set()`: [tools/hd_art_first_sync.h:658](../tools/hd_art_first_sync.h#L658)
  - `hd_first_create_music()`: [tools/hd_art_first_sync.h:681](../tools/hd_art_first_sync.h#L681)
  - `hd_first_tick()`: [tools/hd_art_first_sync.h:740](../tools/hd_art_first_sync.h#L740)
  - `hd_first_delete()`: [tools/hd_art_first_sync.h:757](../tools/hd_art_first_sync.h#L757)
  - `hd_first_finish()`: [tools/hd_art_first_sync.h:795](../tools/hd_art_first_sync.h#L795)
  - `hd_first_retain()`: [tools/hd_art_first_sync.h:863](../tools/hd_art_first_sync.h#L863)
  - `hd_first_retained_handoff()`: [tools/hd_art_first_sync.h:941](../tools/hd_art_first_sync.h#L941)
  - `hd_first_complete()`: [tools/hd_art_first_sync.h:968](../tools/hd_art_first_sync.h#L968)
  - `hd_first_probe_name()`: [tools/hd_art_first_sync.h:984](../tools/hd_art_first_sync.h#L984)
  - `hd_first_prepare()`: [tools/hd_art_first_sync.h:1005](../tools/hd_art_first_sync.h#L1005)
  - `hd_first_local()`: [tools/hd_art_first_sync.h:1056](../tools/hd_art_first_sync.h#L1056)
  - `hd_first_release()`: [tools/hd_art_first_sync.h:1097](../tools/hd_art_first_sync.h#L1097)
- [tools/hd_art_gallery.h](../tools/hd_art_gallery.h)
- [tools/hd_art_graph_recovery.h](../tools/hd_art_graph_recovery.h)
  - `hd_first_graph_load()`: [tools/hd_art_graph_recovery.h:5](../tools/hd_art_graph_recovery.h#L5)
  - `hd_first_print_text()`: [tools/hd_art_graph_recovery.h:98](../tools/hd_art_graph_recovery.h#L98)
  - `hd_first_graph_inventory()`: [tools/hd_art_graph_recovery.h:109](../tools/hd_art_graph_recovery.h#L109)
  - `hd_first_graph_diagnose()`: [tools/hd_art_graph_recovery.h:187](../tools/hd_art_graph_recovery.h#L187)
  - `hd_first_graph_record_stage()`: [tools/hd_art_graph_recovery.h:252](../tools/hd_art_graph_recovery.h#L252)
  - `hd_first_graph_returned()`: [tools/hd_art_graph_recovery.h:260](../tools/hd_art_graph_recovery.h#L260)
  - `hd_first_graph_recovery_load()`: [tools/hd_art_graph_recovery.h:277](../tools/hd_art_graph_recovery.h#L277)
  - `hd_first_graph_extra_verify()`: [tools/hd_art_graph_recovery.h:428](../tools/hd_art_graph_recovery.h#L428)
  - `hd_first_graph_inbound()`: [tools/hd_art_graph_recovery.h:472](../tools/hd_art_graph_recovery.h#L472)
  - `hd_first_graph_extra_record()`: [tools/hd_art_graph_recovery.h:538](../tools/hd_art_graph_recovery.h#L538)
  - `hd_first_graph_recovery_inspect()`: [tools/hd_art_graph_recovery.h:549](../tools/hd_art_graph_recovery.h#L549)
  - `hd_first_graph_already_absent()`: [tools/hd_art_graph_recovery.h:685](../tools/hd_art_graph_recovery.h#L685)
  - `hd_first_graph_recovery_retain()`: [tools/hd_art_graph_recovery.h:730](../tools/hd_art_graph_recovery.h#L730)
  - `hd_first_graph_recovery_delete()`: [tools/hd_art_graph_recovery.h:743](../tools/hd_art_graph_recovery.h#L743)
- [tools/hd_art_journal.h](../tools/hd_art_journal.h)
  - `hd_art_journal_save()`: [tools/hd_art_journal.h:17](../tools/hd_art_journal.h#L17)
  - `hd_art_journal_load()`: [tools/hd_art_journal.h:40](../tools/hd_art_journal.h#L40)
  - `hd_art_binding_result()`: [tools/hd_art_journal.h:57](../tools/hd_art_journal.h#L57)
- [tools/hd_art_serve.h](../tools/hd_art_serve.h)
  - `hd_art_emit()`: [tools/hd_art_serve.h:21](../tools/hd_art_serve.h#L21)
  - `hd_art_log()`: [tools/hd_art_serve.h:25](../tools/hd_art_serve.h#L25)
  - `hd_art_observation_complete()`: [tools/hd_art_serve.h:41](../tools/hd_art_serve.h#L41)
  - `hd_art_album_proof_budget()`: [tools/hd_art_serve.h:66](../tools/hd_art_serve.h#L66)
  - `hd_art_packet()`: [tools/hd_art_serve.h:72](../tools/hd_art_serve.h#L72)
  - `hd_art_tick()`: [tools/hd_art_serve.h:79](../tools/hd_art_serve.h#L79)
  - `hd_art_load_jpeg()`: [tools/hd_art_serve.h:103](../tools/hd_art_serve.h#L103)
  - `hd_art_load_biography()`: [tools/hd_art_serve.h:118](../tools/hd_art_serve.h#L118)
  - `hd_art_serve_experiment_content()`: [tools/hd_art_serve.h:132](../tools/hd_art_serve.h#L132)
  - `hd_art_serve_experiment_work()`: [tools/hd_art_serve.h:190](../tools/hd_art_serve.h#L190)
  - `hd_art_serve_experiment_assets()`: [tools/hd_art_serve.h:194](../tools/hd_art_serve.h#L194)
  - `hd_art_serve_experiment()`: [tools/hd_art_serve.h:198](../tools/hd_art_serve.h#L198)
  - `hd_art_serve_bound()`: [tools/hd_art_serve.h:202](../tools/hd_art_serve.h#L202)
  - `hd_art_serve_ex()`: [tools/hd_art_serve.h:206](../tools/hd_art_serve.h#L206)
  - `hd_art_serve()`: [tools/hd_art_serve.h:210](../tools/hd_art_serve.h#L210)
- [tools/hd_artist_binding.h](../tools/hd_artist_binding.h)
- [tools/hd_artist_probe.h](../tools/hd_artist_probe.h)
- [tools/hd_cli_teardown.h](../tools/hd_cli_teardown.h)
- [tools/hd_http_init.h](../tools/hd_http_init.h)
- [tools/hd_image_server.h](../tools/hd_image_server.h)
- [tools/hd_network_probe.h](../tools/hd_network_probe.h)
- [tools/hd_tunnel_probe.h](../tools/hd_tunnel_probe.h)
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
  - `die()`: [tools/zunetool.c:37](../tools/zunetool.c#L37)
  - `connect_or_die()`: [tools/zunetool.c:46](../tools/zunetool.c#L46)
  - `cmd_info()`: [tools/zunetool.c:59](../tools/zunetool.c#L59)
  - `cmd_list()`: [tools/zunetool.c:74](../tools/zunetool.c#L74)
  - `cmd_send()`: [tools/zunetool.c:91](../tools/zunetool.c#L91)
  - `cmd_purge()`: [tools/zunetool.c:113](../tools/zunetool.c#L113)
  - `cmd_torture()`: [tools/zunetool.c:126](../tools/zunetool.c#L126)
  - `interrupt_video_test()`: [tools/zunetool.c:162](../tools/zunetool.c#L162)
  - `video_test_string()`: [tools/zunetool.c:167](../tools/zunetool.c#L167)
  - `video_test_number()`: [tools/zunetool.c:182](../tools/zunetool.c#L182)
  - `video_test_readback()`: [tools/zunetool.c:197](../tools/zunetool.c#L197)
  - `cmd_video_title_test()`: [tools/zunetool.c:218](../tools/zunetool.c#L218)
  - `cmd_audit()`: [tools/zunetool.c:322](../tools/zunetool.c#L322)
  - `cmd_refs()`: [tools/zunetool.c:407](../tools/zunetool.c#L407)
  - `cmd_playlists()`: [tools/zunetool.c:437](../tools/zunetool.c#L437)
  - `cmd_mkplaylist()`: [tools/zunetool.c:454](../tools/zunetool.c#L454)
  - `cmd_zmdbdump()`: [tools/zunetool.c:473](../tools/zunetool.c#L473)
  - `cmd_extract()`: [tools/zunetool.c:482](../tools/zunetool.c#L482)
  - `cmd_media_capabilities()`: [tools/zunetool.c:492](../tools/zunetool.c#L492)
  - `cmd_hd_art_probe()`: [tools/zunetool.c:504](../tools/zunetool.c#L504)
  - `hd_tunnel_interrupt()`: [tools/zunetool.c:519](../tools/zunetool.c#L519)
  - `cmd_hd_tunnel_probe()`: [tools/zunetool.c:520](../tools/zunetool.c#L520)
  - `cmd_hd_art_album_inspect()`: [tools/zunetool.c:540](../tools/zunetool.c#L540)
  - `cmd_hd_art_first_inspect()`: [tools/zunetool.c:556](../tools/zunetool.c#L556)
  - `cmd_hd_art_recover_graph()`: [tools/zunetool.c:566](../tools/zunetool.c#L566)
  - `cmd_hd_art_recover_music()`: [tools/zunetool.c:587](../tools/zunetool.c#L587)
  - `cmd_hd_art_serve()`: [tools/zunetool.c:610](../tools/zunetool.c#L610)
  - `cmd_hd_network_probe()`: [tools/zunetool.c:723](../tools/zunetool.c#L723)
  - `cmd_podcasts()`: [tools/zunetool.c:741](../tools/zunetool.c#L741)
  - `main()`: [tools/zunetool.c:760](../tools/zunetool.c#L760)
- [tests/abort-flags.c](../tests/abort-flags.c)
  - `check()`: [tests/abort-flags.c:9](../tests/abort-flags.c#L9)
  - `transfer()`: [tests/abort-flags.c:18](../tests/abort-flags.c#L18)
  - `main()`: [tests/abort-flags.c:24](../tests/abort-flags.c#L24)
- [tests/artist_reuse.c](../tests/artist_reuse.c)
  - `zune_set_error()`: [tests/artist_reuse.c:7](../tests/artist_reuse.c#L7)
  - `zune_clear_error()`: [tests/artist_reuse.c:8](../tests/artist_reuse.c#L8)
  - `zune_autopsy_name()`: [tests/artist_reuse.c:9](../tests/artist_reuse.c#L9)
  - `mtp_get_props_supported()`: [tests/artist_reuse.c:10](../tests/artist_reuse.c#L10)
  - `mtp_get_object_handles()`: [tests/artist_reuse.c:13](../tests/artist_reuse.c#L13)
  - `mtp_get_object_prop_value()`: [tests/artist_reuse.c:20](../tests/artist_reuse.c#L20)
  - `mtp_ucs2_to_string()`: [tests/artist_reuse.c:29](../tests/artist_reuse.c#L29)
  - `mtp_send_object_prop_list()`: [tests/artist_reuse.c:30](../tests/artist_reuse.c#L30)
  - `mtp_send_object()`: [tests/artist_reuse.c:39](../tests/artist_reuse.c#L39)
  - `mtp_delete_object()`: [tests/artist_reuse.c:42](../tests/artist_reuse.c#L42)
  - `check()`: [tests/artist_reuse.c:44](../tests/artist_reuse.c#L44)
  - `main()`: [tests/artist_reuse.c:48](../tests/artist_reuse.c#L48)
- [tests/audiobook-protocol.c](../tests/audiobook-protocol.c)
  - `read32()`: [tests/audiobook-protocol.c:20](../tests/audiobook-protocol.c#L20)
  - `read16()`: [tests/audiobook-protocol.c:23](../tests/audiobook-protocol.c#L23)
  - `put32()`: [tests/audiobook-protocol.c:24](../tests/audiobook-protocol.c#L24)
  - `zune_set_error()`: [tests/audiobook-protocol.c:25](../tests/audiobook-protocol.c#L25)
  - `zune_clear_error()`: [tests/audiobook-protocol.c:28](../tests/audiobook-protocol.c#L28)
  - `zune_get_error()`: [tests/audiobook-protocol.c:29](../tests/audiobook-protocol.c#L29)
  - `check()`: [tests/audiobook-protocol.c:30](../tests/audiobook-protocol.c#L30)
  - `object()`: [tests/audiobook-protocol.c:34](../tests/audiobook-protocol.c#L34)
  - `duplicate()`: [tests/audiobook-protocol.c:38](../tests/audiobook-protocol.c#L38)
  - `property()`: [tests/audiobook-protocol.c:47](../tests/audiobook-protocol.c#L47)
  - `reply()`: [tests/audiobook-protocol.c:51](../tests/audiobook-protocol.c#L51)
  - `reset()`: [tests/audiobook-protocol.c:56](../tests/audiobook-protocol.c#L56)
  - `zune_forge_folder()`: [tests/audiobook-protocol.c:68](../tests/audiobook-protocol.c#L68)
  - `type_for()`: [tests/audiobook-protocol.c:76](../tests/audiobook-protocol.c#L76)
  - `ptp_transaction()`: [tests/audiobook-protocol.c:81](../tests/audiobook-protocol.c#L81)
  - `main()`: [tests/audiobook-protocol.c:162](../tests/audiobook-protocol.c#L162)
- [tests/audiobook_gate.c](../tests/audiobook_gate.c)
  - `__wrap_zune_breach()`: [tests/audiobook_gate.c:13](../tests/audiobook_gate.c#L13)
  - `__wrap_mtp_get_object_prop_value()`: [tests/audiobook_gate.c:17](../tests/audiobook_gate.c#L17)
  - `__wrap_mtp_get_object_handles()`: [tests/audiobook_gate.c:28](../tests/audiobook_gate.c#L28)
  - `check()`: [tests/audiobook_gate.c:40](../tests/audiobook_gate.c#L40)
  - `main()`: [tests/audiobook_gate.c:47](../tests/audiobook_gate.c#L47)
- [tests/brand-limits.c](../tests/brand-limits.c)
  - `zune_set_error()`: [tests/brand-limits.c:12](../tests/brand-limits.c#L12)
  - `zune_clear_error()`: [tests/brand-limits.c:18](../tests/brand-limits.c#L18)
  - `zune_get_error()`: [tests/brand-limits.c:19](../tests/brand-limits.c#L19)
  - `zune_autopsy_name()`: [tests/brand-limits.c:20](../tests/brand-limits.c#L20)
  - `zune_is_aborted()`: [tests/brand-limits.c:21](../tests/brand-limits.c#L21)
  - `zune_unjam()`: [tests/brand-limits.c:22](../tests/brand-limits.c#L22)
  - `check()`: [tests/brand-limits.c:24](../tests/brand-limits.c#L24)
  - `format_of()`: [tests/brand-limits.c:45](../tests/brand-limits.c#L45)
  - `copy()`: [tests/brand-limits.c:56](../tests/brand-limits.c#L56)
  - `made()`: [tests/brand-limits.c:65](../tests/brand-limits.c#L65)
  - `ptp_transaction()`: [tests/brand-limits.c:78](../tests/brand-limits.c#L78)
  - `limits()`: [tests/brand-limits.c:140](../tests/brand-limits.c#L140)
  - `main()`: [tests/brand-limits.c:146](../tests/brand-limits.c#L146)
- [tests/error-convention.c](../tests/error-convention.c)
  - `check()`: [tests/error-convention.c:13](../tests/error-convention.c#L13)
  - `plant()`: [tests/error-convention.c:19](../tests/error-convention.c#L19)
  - `planted()`: [tests/error-convention.c:20](../tests/error-convention.c#L20)
  - `main()`: [tests/error-convention.c:37](../tests/error-convention.c#L37)
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
- [tests/forge-partial.c](../tests/forge-partial.c)
  - `zune_set_error()`: [tests/forge-partial.c:11](../tests/forge-partial.c#L11)
  - `zune_clear_error()`: [tests/forge-partial.c:17](../tests/forge-partial.c#L17)
  - `zune_get_error()`: [tests/forge-partial.c:18](../tests/forge-partial.c#L18)
  - `zune_autopsy_name()`: [tests/forge-partial.c:19](../tests/forge-partial.c#L19)
  - `check()`: [tests/forge-partial.c:21](../tests/forge-partial.c#L21)
  - `ptp_transaction()`: [tests/forge-partial.c:31](../tests/forge-partial.c#L31)
  - `reset()`: [tests/forge-partial.c:77](../tests/forge-partial.c#L77)
  - `main()`: [tests/forge-partial.c:83](../tests/forge-partial.c#L83)
- [tests/hd_art_album_sync.c](../tests/hd_art_album_sync.c)
  - `now()`: [tests/hd_art_album_sync.c:30](../tests/hd_art_album_sync.c#L30)
  - `check()`: [tests/hd_art_album_sync.c:31](../tests/hd_art_album_sync.c#L31)
  - `find()`: [tests/hd_art_album_sync.c:39](../tests/hd_art_album_sync.c#L39)
  - `prop()`: [tests/hd_art_album_sync.c:45](../tests/hd_art_album_sync.c#L45)
  - `set()`: [tests/hd_art_album_sync.c:54](../tests/hd_art_album_sync.c#L54)
  - `textprop()`: [tests/hd_art_album_sync.c:60](../tests/hd_art_album_sync.c#L60)
  - `copyout()`: [tests/hd_art_album_sync.c:66](../tests/hd_art_album_sync.c#L66)
  - `ptp_transaction()`: [tests/hd_art_album_sync.c:72](../tests/hd_art_album_sync.c#L72)
  - `write_plan()`: [tests/hd_art_album_sync.c:229](../tests/hd_art_album_sync.c#L229)
  - `setup()`: [tests/hd_art_album_sync.c:239](../tests/hd_art_album_sync.c#L239)
  - `teardown()`: [tests/hd_art_album_sync.c:270](../tests/hd_art_album_sync.c#L270)
  - `graph()`: [tests/hd_art_album_sync.c:271](../tests/hd_art_album_sync.c#L271)
  - `main()`: [tests/hd_art_album_sync.c:277](../tests/hd_art_album_sync.c#L277)
- [tests/hd_art_bind_ready.c](../tests/hd_art_bind_ready.c)
  - `clock_mock()`: [tests/hd_art_bind_ready.c:8](../tests/hd_art_bind_ready.c#L8)
  - `ptp_transaction()`: [tests/hd_art_bind_ready.c:9](../tests/hd_art_bind_ready.c#L9)
  - `snapshot_mock()`: [tests/hd_art_bind_ready.c:14](../tests/hd_art_bind_ready.c#L14)
  - `save_mock()`: [tests/hd_art_bind_ready.c:18](../tests/hd_art_bind_ready.c#L18)
  - `apply_mock()`: [tests/hd_art_bind_ready.c:21](../tests/hd_art_bind_ready.c#L21)
  - `restore_mock()`: [tests/hd_art_bind_ready.c:29](../tests/hd_art_bind_ready.c#L29)
  - `check()`: [tests/hd_art_bind_ready.c:32](../tests/hd_art_bind_ready.c#L32)
  - `setup()`: [tests/hd_art_bind_ready.c:33](../tests/hd_art_bind_ready.c#L33)
  - `prepare()`: [tests/hd_art_bind_ready.c:38](../tests/hd_art_bind_ready.c#L38)
  - `main()`: [tests/hd_art_bind_ready.c:39](../tests/hd_art_bind_ready.c#L39)
- [tests/hd_art_first_sync.c](../tests/hd_art_first_sync.c)
  - `mock_now()`: [tests/hd_art_first_sync.c:23](../tests/hd_art_first_sync.c#L23)
  - `check()`: [tests/hd_art_first_sync.c:24](../tests/hd_art_first_sync.c#L24)
  - `lookup()`: [tests/hd_art_first_sync.c:31](../tests/hd_art_first_sync.c#L31)
  - `response_data()`: [tests/hd_art_first_sync.c:37](../tests/hd_art_first_sync.c#L37)
  - `wire_string()`: [tests/hd_art_first_sync.c:44](../tests/hd_art_first_sync.c#L44)
  - `journal_before_write()`: [tests/hd_art_first_sync.c:52](../tests/hd_art_first_sync.c#L52)
  - `parse_creation()`: [tests/hd_art_first_sync.c:59](../tests/hd_art_first_sync.c#L59)
  - `ptp_transaction()`: [tests/hd_art_first_sync.c:122](../tests/hd_art_first_sync.c#L122)
  - `setup()`: [tests/hd_art_first_sync.c:419](../tests/hd_art_first_sync.c#L419)
  - `teardown()`: [tests/hd_art_first_sync.c:459](../tests/hd_art_first_sync.c#L459)
  - `prepare()`: [tests/hd_art_first_sync.c:466](../tests/hd_art_first_sync.c#L466)
  - `complete()`: [tests/hd_art_first_sync.c:471](../tests/hd_art_first_sync.c#L471)
  - `main()`: [tests/hd_art_first_sync.c:478](../tests/hd_art_first_sync.c#L478)
- [tests/hd_art_gallery.c](../tests/hd_art_gallery.c)
  - `check()`: [tests/hd_art_gallery.c:4](../tests/hd_art_gallery.c#L4)
  - `emit()`: [tests/hd_art_gallery.c:5](../tests/hd_art_gallery.c#L5)
  - `jpeg()`: [tests/hd_art_gallery.c:6](../tests/hd_art_gallery.c#L6)
  - `main()`: [tests/hd_art_gallery.c:21](../tests/hd_art_gallery.c#L21)
- [tests/hd_art_journal.c](../tests/hd_art_journal.c)
  - `connect_or_die()`: [tests/hd_art_journal.c:3](../tests/hd_art_journal.c#L3)
  - `main()`: [tests/hd_art_journal.c:5](../tests/hd_art_journal.c#L5)
- [tests/hd_artist_binding.c](../tests/hd_artist_binding.c)
  - `put16()`: [tests/hd_artist_binding.c:10](../tests/hd_artist_binding.c#L10)
  - `put32()`: [tests/hd_artist_binding.c:11](../tests/hd_artist_binding.c#L11)
  - `wire_ascii()`: [tests/hd_artist_binding.c:12](../tests/hd_artist_binding.c#L12)
  - `check()`: [tests/hd_artist_binding.c:17](../tests/hd_artist_binding.c#L17)
  - `reset()`: [tests/hd_artist_binding.c:20](../tests/hd_artist_binding.c#L20)
  - `ptp_transaction()`: [tests/hd_artist_binding.c:30](../tests/hd_artist_binding.c#L30)
  - `snapshot()`: [tests/hd_artist_binding.c:96](../tests/hd_artist_binding.c#L96)
  - `rejected_snapshot()`: [tests/hd_artist_binding.c:101](../tests/hd_artist_binding.c#L101)
  - `guard_before_write()`: [tests/hd_artist_binding.c:106](../tests/hd_artist_binding.c#L106)
  - `main()`: [tests/hd_artist_binding.c:109](../tests/hd_artist_binding.c#L109)
- [tests/hd_artist_probe.c](../tests/hd_artist_probe.c)
  - `u16()`: [tests/hd_artist_probe.c:16](../tests/hd_artist_probe.c#L16)
  - `u32()`: [tests/hd_artist_probe.c:17](../tests/hd_artist_probe.c#L17)
  - `allocate()`: [tests/hd_artist_probe.c:18](../tests/hd_artist_probe.c#L18)
  - `ptp_transaction()`: [tests/hd_artist_probe.c:21](../tests/hd_artist_probe.c#L21)
  - `check()`: [tests/hd_artist_probe.c:105](../tests/hd_artist_probe.c#L105)
  - `reset()`: [tests/hd_artist_probe.c:109](../tests/hd_artist_probe.c#L109)
  - `run()`: [tests/hd_artist_probe.c:115](../tests/hd_artist_probe.c#L115)
  - `main()`: [tests/hd_artist_probe.c:123](../tests/hd_artist_probe.c#L123)
- [tests/hd_artwork_api.c](../tests/hd_artwork_api.c)
  - `test_now()`: [tests/hd_artwork_api.c:5](../tests/hd_artwork_api.c#L5)
  - `test_pause()`: [tests/hd_artwork_api.c:6](../tests/hd_artwork_api.c#L6)
  - `zune_set_error()`: [tests/hd_artwork_api.c:20](../tests/hd_artwork_api.c#L20)
  - `zune_clear_error()`: [tests/hd_artwork_api.c:21](../tests/hd_artwork_api.c#L21)
  - `mtp_get_object_handles()`: [tests/hd_artwork_api.c:22](../tests/hd_artwork_api.c#L22)
  - `mtp_get_object_prop_value()`: [tests/hd_artwork_api.c:28](../tests/hd_artwork_api.c#L28)
  - `mtp_get_object_references()`: [tests/hd_artwork_api.c:40](../tests/hd_artwork_api.c#L40)
  - `zune_free_albums()`: [tests/hd_artwork_api.c:43](../tests/hd_artwork_api.c#L43)
  - `mtp_send_object()`: [tests/hd_artwork_api.c:47](../tests/hd_artwork_api.c#L47)
  - `ptp_transaction()`: [tests/hd_artwork_api.c:50](../tests/hd_artwork_api.c#L50)
  - `check()`: [tests/hd_artwork_api.c:83](../tests/hd_artwork_api.c#L83)
  - `device()`: [tests/hd_artwork_api.c:84](../tests/hd_artwork_api.c#L84)
  - `reset()`: [tests/hd_artwork_api.c:85](../tests/hd_artwork_api.c#L85)
  - `ready()`: [tests/hd_artwork_api.c:86](../tests/hd_artwork_api.c#L86)
  - `uncertain_ready()`: [tests/hd_artwork_api.c:87](../tests/hd_artwork_api.c#L87)
  - `emit_ok()`: [tests/hd_artwork_api.c:88](../tests/hd_artwork_api.c#L88)
  - `main()`: [tests/hd_artwork_api.c:89](../tests/hd_artwork_api.c#L89)
- [tests/hd_cli_teardown.c](../tests/hd_cli_teardown.c)
  - `close_usb()`: [tests/hd_cli_teardown.c:5](../tests/hd_cli_teardown.c#L5)
  - `zune_sever()`: [tests/hd_cli_teardown.c:11](../tests/hd_cli_teardown.c#L11)
  - `device()`: [tests/hd_cli_teardown.c:16](../tests/hd_cli_teardown.c#L16)
  - `check()`: [tests/hd_cli_teardown.c:22](../tests/hd_cli_teardown.c#L22)
  - `main()`: [tests/hd_cli_teardown.c:23](../tests/hd_cli_teardown.c#L23)
- [tests/hd_http_init.c](../tests/hd_http_init.c)
  - `ptp_transaction()`: [tests/hd_http_init.c:12](../tests/hd_http_init.c#L12)
  - `check()`: [tests/hd_http_init.c:64](../tests/hd_http_init.c#L64)
  - `reset()`: [tests/hd_http_init.c:68](../tests/hd_http_init.c#L68)
  - `report()`: [tests/hd_http_init.c:74](../tests/hd_http_init.c#L74)
  - `full_tests()`: [tests/hd_http_init.c:82](../tests/hd_http_init.c#L82)
  - `main()`: [tests/hd_http_init.c:122](../tests/hd_http_init.c#L122)
- [tests/hd_image_server.c](../tests/hd_image_server.c)
  - `emit()`: [tests/hd_image_server.c:15](../tests/hd_image_server.c#L15)
  - `init()`: [tests/hd_image_server.c:25](../tests/hd_image_server.c#L25)
  - `packet()`: [tests/hd_image_server.c:35](../tests/hd_image_server.c#L35)
  - `input()`: [tests/hd_image_server.c:57](../tests/hd_image_server.c#L57)
  - `handshake()`: [tests/hd_image_server.c:63](../tests/hd_image_server.c#L63)
  - `image_test()`: [tests/hd_image_server.c:72](../tests/hd_image_server.c#L72)
  - `bad_and_retransmit()`: [tests/hd_image_server.c:107](../tests/hd_image_server.c#L107)
  - `dns_query()`: [tests/hd_image_server.c:137](../tests/hd_image_server.c#L137)
  - `dns_test()`: [tests/hd_image_server.c:157](../tests/hd_image_server.c#L157)
  - `route_test()`: [tests/hd_image_server.c:189](../tests/hd_image_server.c#L189)
  - `failed_emit()`: [tests/hd_image_server.c:199](../tests/hd_image_server.c#L199)
  - `rejection_test()`: [tests/hd_image_server.c:205](../tests/hd_image_server.c#L205)
  - `mss_and_fragment_test()`: [tests/hd_image_server.c:245](../tests/hd_image_server.c#L245)
  - `udp_test()`: [tests/hd_image_server.c:280](../tests/hd_image_server.c#L280)
  - `fwlink_diagnostic_test()`: [tests/hd_image_server.c:307](../tests/hd_image_server.c#L307)
  - `element()`: [tests/hd_image_server.c:347](../tests/hd_image_server.c#L347)
  - `collect_response()`: [tests/hd_image_server.c:355](../tests/hd_image_server.c#L355)
  - `catalog_test()`: [tests/hd_image_server.c:387](../tests/hd_image_server.c#L387)
  - `catalog_configuration_test()`: [tests/hd_image_server.c:504](../tests/hd_image_server.c#L504)
  - `multi_init()`: [tests/hd_image_server.c:559](../tests/hd_image_server.c#L559)
  - `multi_asset_test()`: [tests/hd_image_server.c:579](../tests/hd_image_server.c#L579)
  - `multi_validation_test()`: [tests/hd_image_server.c:687](../tests/hd_image_server.c#L687)
  - `separate_roles_test()`: [tests/hd_image_server.c:739](../tests/hd_image_server.c#L739)
  - `multi_interleaved_test()`: [tests/hd_image_server.c:810](../tests/hd_image_server.c#L810)
  - `biography_test()`: [tests/hd_image_server.c:853](../tests/hd_image_server.c#L853)
  - `port_input()`: [tests/hd_image_server.c:908](../tests/hd_image_server.c#L908)
  - `concurrent_capacity_test()`: [tests/hd_image_server.c:915](../tests/hd_image_server.c#L915)
  - `main()`: [tests/hd_image_server.c:983](../tests/hd_image_server.c#L983)
- [tests/hd_network_probe.c](../tests/hd_network_probe.c)
  - `test_now()`: [tests/hd_network_probe.c:12](../tests/hd_network_probe.c#L12)
  - `test_pause()`: [tests/hd_network_probe.c:13](../tests/hd_network_probe.c#L13)
  - `check()`: [tests/hd_network_probe.c:14](../tests/hd_network_probe.c#L14)
  - `save()`: [tests/hd_network_probe.c:15](../tests/hd_network_probe.c#L15)
  - `init()`: [tests/hd_network_probe.c:16](../tests/hd_network_probe.c#L16)
  - `set_length()`: [tests/hd_network_probe.c:17](../tests/hd_network_probe.c#L17)
  - `ip_packet()`: [tests/hd_network_probe.c:18](../tests/hd_network_probe.c#L18)
  - `negotiate()`: [tests/hd_network_probe.c:26](../tests/hd_network_probe.c#L26)
  - `callback()`: [tests/hd_network_probe.c:34](../tests/hd_network_probe.c#L34)
  - `state_tests()`: [tests/hd_network_probe.c:35](../tests/hd_network_probe.c#L35)
  - `framing_tests()`: [tests/hd_network_probe.c:68](../tests/hd_network_probe.c#L68)
  - `diagnostic_tests()`: [tests/hd_network_probe.c:81](../tests/hd_network_probe.c#L81)
  - `startup_reply()`: [tests/hd_network_probe.c:94](../tests/hd_network_probe.c#L94)
  - `startup_tests()`: [tests/hd_network_probe.c:95](../tests/hd_network_probe.c#L95)
  - `ptp_transaction()`: [tests/hd_network_probe.c:125](../tests/hd_network_probe.c#L125)
  - `lifecycle_tick()`: [tests/hd_network_probe.c:154](../tests/hd_network_probe.c#L154)
  - `budget_tick()`: [tests/hd_network_probe.c:162](../tests/hd_network_probe.c#L162)
  - `run_transport()`: [tests/hd_network_probe.c:168](../tests/hd_network_probe.c#L168)
  - `image_emit()`: [tests/hd_network_probe.c:230](../tests/hd_network_probe.c#L230)
  - `image_input()`: [tests/hd_network_probe.c:231](../tests/hd_network_probe.c#L231)
  - `observed_ack_budget_test()`: [tests/hd_network_probe.c:232](../tests/hd_network_probe.c#L232)
  - `main()`: [tests/hd_network_probe.c:261](../tests/hd_network_probe.c#L261)
- [tests/hd_tunnel_probe.c](../tests/hd_tunnel_probe.c)
  - `fake_now()`: [tests/hd_tunnel_probe.c:28](../tests/hd_tunnel_probe.c#L28)
  - `fake_pause()`: [tests/hd_tunnel_probe.c:29](../tests/hd_tunnel_probe.c#L29)
  - `check()`: [tests/hd_tunnel_probe.c:32](../tests/hd_tunnel_probe.c#L32)
  - `payload()`: [tests/hd_tunnel_probe.c:36](../tests/hd_tunnel_probe.c#L36)
  - `ptp_transaction()`: [tests/hd_tunnel_probe.c:40](../tests/hd_tunnel_probe.c#L40)
  - `reset()`: [tests/hd_tunnel_probe.c:122](../tests/hd_tunnel_probe.c#L122)
  - `run()`: [tests/hd_tunnel_probe.c:127](../tests/hd_tunnel_probe.c#L127)
  - `codec_tests()`: [tests/hd_tunnel_probe.c:134](../tests/hd_tunnel_probe.c#L134)
  - `main()`: [tests/hd_tunnel_probe.c:168](../tests/hd_tunnel_probe.c#L168)
- [tests/honest-failures.c](../tests/honest-failures.c)
  - `clear_error()`: [tests/honest-failures.c:14](../tests/honest-failures.c#L14)
  - `copy_of()`: [tests/honest-failures.c:16](../tests/honest-failures.c#L16)
  - `ptp_transaction()`: [tests/honest-failures.c:23](../tests/honest-failures.c#L23)
  - `check()`: [tests/honest-failures.c:75](../tests/honest-failures.c#L75)
  - `main()`: [tests/honest-failures.c:81](../tests/honest-failures.c#L81)
- [tests/media_capabilities.c](../tests/media_capabilities.c)
  - `ptp_transaction()`: [tests/media_capabilities.c:12](../tests/media_capabilities.c#L12)
  - `check()`: [tests/media_capabilities.c:55](../tests/media_capabilities.c#L55)
  - `report()`: [tests/media_capabilities.c:61](../tests/media_capabilities.c#L61)
  - `main()`: [tests/media_capabilities.c:77](../tests/media_capabilities.c#L77)
- [tests/media_prep.c](../tests/media_prep.c)
  - `zune_set_error()`: [tests/media_prep.c:8](../tests/media_prep.c#L8)
  - `zune_clear_error()`: [tests/media_prep.c:16](../tests/media_prep.c#L16)
  - `exists()`: [tests/media_prep.c:18](../tests/media_prep.c#L18)
  - `main()`: [tests/media_prep.c:24](../tests/media_prep.c#L24)
- [tests/metadata_probe.c](../tests/metadata_probe.c)
  - `zune_set_error()`: [tests/metadata_probe.c:7](../tests/metadata_probe.c#L7)
  - `zune_clear_error()`: [tests/metadata_probe.c:8](../tests/metadata_probe.c#L8)
  - `check()`: [tests/metadata_probe.c:11](../tests/metadata_probe.c#L11)
  - `same()`: [tests/metadata_probe.c:17](../tests/metadata_probe.c#L17)
  - `empty()`: [tests/metadata_probe.c:21](../tests/metadata_probe.c#L21)
  - `load()`: [tests/metadata_probe.c:27](../tests/metadata_probe.c#L27)
  - `main()`: [tests/metadata_probe.c:34](../tests/metadata_probe.c#L34)
- [tests/mtp-helpers.c](../tests/mtp-helpers.c)
  - `check()`: [tests/mtp-helpers.c:7](../tests/mtp-helpers.c#L7)
  - `mtp_get_prop_desc()`: [tests/mtp-helpers.c:17](../tests/mtp-helpers.c#L17)
  - `mtp_get_object_prop_value()`: [tests/mtp-helpers.c:27](../tests/mtp-helpers.c#L27)
  - `mtp_ucs2_to_string()`: [tests/mtp-helpers.c:33](../tests/mtp-helpers.c#L33)
  - `desc()`: [tests/mtp-helpers.c:41](../tests/mtp-helpers.c#L41)
  - `main()`: [tests/mtp-helpers.c:49](../tests/mtp-helpers.c#L49)
- [tests/mtp-unpack.c](../tests/mtp-unpack.c)
  - `ptp_transaction()`: [tests/mtp-unpack.c:15](../tests/mtp-unpack.c#L15)
  - `check()`: [tests/mtp-unpack.c:36](../tests/mtp-unpack.c#L36)
  - `put32()`: [tests/mtp-unpack.c:42](../tests/mtp-unpack.c#L42)
  - `array_reply()`: [tests/mtp-unpack.c:47](../tests/mtp-unpack.c#L47)
  - `storage_ids()`: [tests/mtp-unpack.c:55](../tests/mtp-unpack.c#L55)
  - `references()`: [tests/mtp-unpack.c:58](../tests/mtp-unpack.c#L58)
  - `handles()`: [tests/mtp-unpack.c:61](../tests/mtp-unpack.c#L61)
  - `uint32_arrays()`: [tests/mtp-unpack.c:65](../tests/mtp-unpack.c#L65)
  - `props_supported()`: [tests/mtp-unpack.c:100](../tests/mtp-unpack.c#L100)
  - `outbound_arrays()`: [tests/mtp-unpack.c:112](../tests/mtp-unpack.c#L112)
  - `put_string()`: [tests/mtp-unpack.c:131](../tests/mtp-unpack.c#L131)
  - `device_info()`: [tests/mtp-unpack.c:138](../tests/mtp-unpack.c#L138)
  - `devinfo()`: [tests/mtp-unpack.c:157](../tests/mtp-unpack.c#L157)
  - `main()`: [tests/mtp-unpack.c:192](../tests/mtp-unpack.c#L192)
- [tests/mtpz-keys.c](../tests/mtpz-keys.c)
  - `check()`: [tests/mtpz-keys.c:18](../tests/mtpz-keys.c#L18)
  - `__wrap_fopen()`: [tests/mtpz-keys.c:30](../tests/mtpz-keys.c#L30)
  - `__wrap_free()`: [tests/mtpz-keys.c:43](../tests/mtpz-keys.c#L43)
  - `__wrap_gcry_control()`: [tests/mtpz-keys.c:68](../tests/mtpz-keys.c#L68)
  - `__wrap_gcry_check_version()`: [tests/mtpz-keys.c:74](../tests/mtpz-keys.c#L74)
  - `repeat()`: [tests/mtpz-keys.c:84](../tests/mtpz-keys.c#L84)
  - `write_keys()`: [tests/mtpz-keys.c:89](../tests/mtpz-keys.c#L89)
  - `tap()`: [tests/mtpz-keys.c:106](../tests/mtpz-keys.c#L106)
  - `remove_keys()`: [tests/mtpz-keys.c:114](../tests/mtpz-keys.c#L114)
  - `main()`: [tests/mtpz-keys.c:120](../tests/mtpz-keys.c#L120)
- [tests/mtpz-nullcheck.c](../tests/mtpz-nullcheck.c)
  - `test_malloc()`: [tests/mtpz-nullcheck.c:22](../tests/mtpz-nullcheck.c#L22)
  - `test_calloc()`: [tests/mtpz-nullcheck.c:23](../tests/mtpz-nullcheck.c#L23)
  - `test_strdup()`: [tests/mtpz-nullcheck.c:25](../tests/mtpz-nullcheck.c#L25)
  - `check()`: [tests/mtpz-nullcheck.c:35](../tests/mtpz-nullcheck.c#L35)
  - `mpi_hex()`: [tests/mtpz-nullcheck.c:47](../tests/mtpz-nullcheck.c#L47)
  - `install_keys()`: [tests/mtpz-nullcheck.c:62](../tests/mtpz-nullcheck.c#L62)
  - `build_response()`: [tests/mtpz-nullcheck.c:89](../tests/mtpz-nullcheck.c#L89)
  - `ptp_transaction()`: [tests/mtpz-nullcheck.c:139](../tests/mtpz-nullcheck.c#L139)
  - `ptp_set_device_prop_value()`: [tests/mtpz-nullcheck.c:167](../tests/mtpz-nullcheck.c#L167)
  - `main()`: [tests/mtpz-nullcheck.c:172](../tests/mtpz-nullcheck.c#L172)
- [tests/photo-upload.c](../tests/photo-upload.c)
  - `zune_set_error()`: [tests/photo-upload.c:14](../tests/photo-upload.c#L14)
  - `zune_clear_error()`: [tests/photo-upload.c:22](../tests/photo-upload.c#L22)
  - `zune_get_error()`: [tests/photo-upload.c:23](../tests/photo-upload.c#L23)
  - `zune_autopsy_name()`: [tests/photo-upload.c:24](../tests/photo-upload.c#L24)
  - `zune_is_aborted()`: [tests/photo-upload.c:25](../tests/photo-upload.c#L25)
  - `zune_unjam()`: [tests/photo-upload.c:26](../tests/photo-upload.c#L26)
  - `add_folder()`: [tests/photo-upload.c:48](../tests/photo-upload.c#L48)
  - `object_info_for()`: [tests/photo-upload.c:55](../tests/photo-upload.c#L55)
  - `ptp_transaction()`: [tests/photo-upload.c:72](../tests/photo-upload.c#L72)
  - `check()`: [tests/photo-upload.c:154](../tests/photo-upload.c#L154)
  - `write_file()`: [tests/photo-upload.c:160](../tests/photo-upload.c#L160)
  - `temp_files()`: [tests/photo-upload.c:166](../tests/photo-upload.c#L166)
  - `reset_counters()`: [tests/photo-upload.c:176](../tests/photo-upload.c#L176)
  - `main()`: [tests/photo-upload.c:182](../tests/photo-upload.c#L182)
- [tests/podcast-state.c](../tests/podcast-state.c)
  - `check()`: [tests/podcast-state.c:6](../tests/podcast-state.c#L6)
  - `zune_set_error()`: [tests/podcast-state.c:9](../tests/podcast-state.c#L9)
  - `zune_clear_error()`: [tests/podcast-state.c:11](../tests/podcast-state.c#L11)
  - `zune_get_error()`: [tests/podcast-state.c:12](../tests/podcast-state.c#L12)
  - `put32()`: [tests/podcast-state.c:13](../tests/podcast-state.c#L13)
  - `put16()`: [tests/podcast-state.c:15](../tests/podcast-state.c#L15)
  - `fixture()`: [tests/podcast-state.c:19](../tests/podcast-state.c#L19)
  - `parse()`: [tests/podcast-state.c:38](../tests/podcast-state.c#L38)
  - `fake_read()`: [tests/podcast-state.c:49](../tests/podcast-state.c#L49)
  - `fake_write()`: [tests/podcast-state.c:60](../tests/podcast-state.c#L60)
  - `transport_fixture()`: [tests/podcast-state.c:68](../tests/podcast-state.c#L68)
  - `get()`: [tests/podcast-state.c:75](../tests/podcast-state.c#L75)
  - `main()`: [tests/podcast-state.c:84](../tests/podcast-state.c#L84)
- [tests/podcasts.c](../tests/podcasts.c)
  - `read32()`: [tests/podcasts.c:14](../tests/podcasts.c#L14)
  - `read16()`: [tests/podcasts.c:15](../tests/podcasts.c#L15)
  - `put32()`: [tests/podcasts.c:16](../tests/podcasts.c#L16)
  - `check()`: [tests/podcasts.c:17](../tests/podcasts.c#L17)
  - `zune_set_error()`: [tests/podcasts.c:18](../tests/podcasts.c#L18)
  - `zune_clear_error()`: [tests/podcasts.c:19](../tests/podcasts.c#L19)
  - `zune_get_error()`: [tests/podcasts.c:20](../tests/podcasts.c#L20)
  - `object()`: [tests/podcasts.c:21](../tests/podcasts.c#L21)
  - `prop()`: [tests/podcasts.c:22](../tests/podcasts.c#L22)
  - `reply()`: [tests/podcasts.c:23](../tests/podcasts.c#L23)
  - `reset()`: [tests/podcasts.c:24](../tests/podcasts.c#L24)
  - `duplicate()`: [tests/podcasts.c:29](../tests/podcasts.c#L29)
  - `type_for()`: [tests/podcasts.c:33](../tests/podcasts.c#L33)
  - `ptp_transaction()`: [tests/podcasts.c:40](../tests/podcasts.c#L40)
  - `forge()`: [tests/podcasts.c:121](../tests/podcasts.c#L121)
  - `main()`: [tests/podcasts.c:122](../tests/podcasts.c#L122)
- [tests/ptp-cancel.c](../tests/ptp-cancel.c)
  - `usleep()`: [tests/ptp-cancel.c:20](../tests/ptp-cancel.c#L20)
  - `__wrap_clock_gettime()`: [tests/ptp-cancel.c:22](../tests/ptp-cancel.c#L22)
  - `check()`: [tests/ptp-cancel.c:30](../tests/ptp-cancel.c#L30)
  - `put16()`: [tests/ptp-cancel.c:45](../tests/ptp-cancel.c#L45)
  - `put32()`: [tests/ptp-cancel.c:46](../tests/ptp-cancel.c#L46)
  - `fake_write()`: [tests/ptp-cancel.c:48](../tests/ptp-cancel.c#L48)
  - `fake_read()`: [tests/ptp-cancel.c:58](../tests/ptp-cancel.c#L58)
  - `fake_clear_halt()`: [tests/ptp-cancel.c:78](../tests/ptp-cancel.c#L78)
  - `fake_class()`: [tests/ptp-cancel.c:84](../tests/ptp-cancel.c#L84)
  - `reset()`: [tests/ptp-cancel.c:107](../tests/ptp-cancel.c#L107)
  - `session()`: [tests/ptp-cancel.c:114](../tests/ptp-cancel.c#L114)
  - `main()`: [tests/ptp-cancel.c:123](../tests/ptp-cancel.c#L123)
- [tests/ptp_file_adapter.c](../tests/ptp_file_adapter.c)
  - `ptp_transaction_file()`: [tests/ptp_file_adapter.c:8](../tests/ptp_file_adapter.c#L8)
- [tests/ptp_optional_data.c](../tests/ptp_optional_data.c)
  - `put16()`: [tests/ptp_optional_data.c:9](../tests/ptp_optional_data.c#L9)
  - `put32()`: [tests/ptp_optional_data.c:10](../tests/ptp_optional_data.c#L10)
  - `packet()`: [tests/ptp_optional_data.c:11](../tests/ptp_optional_data.c#L11)
  - `read_usb()`: [tests/ptp_optional_data.c:15](../tests/ptp_optional_data.c#L15)
  - `write_usb()`: [tests/ptp_optional_data.c:21](../tests/ptp_optional_data.c#L21)
  - `reset()`: [tests/ptp_optional_data.c:27](../tests/ptp_optional_data.c#L27)
  - `run()`: [tests/ptp_optional_data.c:28](../tests/ptp_optional_data.c#L28)
  - `idle()`: [tests/ptp_optional_data.c:40](../tests/ptp_optional_data.c#L40)
  - `main()`: [tests/ptp_optional_data.c:41](../tests/ptp_optional_data.c#L41)
- [tests/run-abort-flags.sh](../tests/run-abort-flags.sh)
- [tests/run-artist-reuse.sh](../tests/run-artist-reuse.sh)
- [tests/run-audiobook-gate.sh](../tests/run-audiobook-gate.sh)
- [tests/run-audiobook-protocol.sh](../tests/run-audiobook-protocol.sh)
- [tests/run-brand-limits.sh](../tests/run-brand-limits.sh)
- [tests/run-error-convention.sh](../tests/run-error-convention.sh)
- [tests/run-examples.sh](../tests/run-examples.sh)
- [tests/run-forge-partial.sh](../tests/run-forge-partial.sh)
- [tests/run-fuzz-smoke.sh](../tests/run-fuzz-smoke.sh)
- [tests/run-hd-art-album-cli.sh](../tests/run-hd-art-album-cli.sh)
- [tests/run-hd-art-album-sync.sh](../tests/run-hd-art-album-sync.sh)
- [tests/run-hd-art-bind-ready.sh](../tests/run-hd-art-bind-ready.sh)
- [tests/run-hd-art-first-sync.sh](../tests/run-hd-art-first-sync.sh)
- [tests/run-hd-art-gallery-cli.sh](../tests/run-hd-art-gallery-cli.sh)
- [tests/run-hd-art-gallery.sh](../tests/run-hd-art-gallery.sh)
- [tests/run-hd-art-journal.sh](../tests/run-hd-art-journal.sh)
- [tests/run-hd-artist-binding.sh](../tests/run-hd-artist-binding.sh)
- [tests/run-hd-artist-probe.sh](../tests/run-hd-artist-probe.sh)
- [tests/run-hd-artwork-api.sh](../tests/run-hd-artwork-api.sh)
- [tests/run-hd-cli-teardown.sh](../tests/run-hd-cli-teardown.sh)
- [tests/run-hd-http-init.sh](../tests/run-hd-http-init.sh)
- [tests/run-hd-image-server.sh](../tests/run-hd-image-server.sh)
- [tests/run-hd-network-probe.sh](../tests/run-hd-network-probe.sh)
- [tests/run-hd-tunnel-probe.sh](../tests/run-hd-tunnel-probe.sh)
- [tests/run-honest-failures.sh](../tests/run-honest-failures.sh)
- [tests/run-media-capabilities.sh](../tests/run-media-capabilities.sh)
- [tests/run-media-prep.sh](../tests/run-media-prep.sh)
- [tests/run-metadata-probe.sh](../tests/run-metadata-probe.sh)
- [tests/run-mtp-unpack.sh](../tests/run-mtp-unpack.sh)
- [tests/run-mtpz-keys.sh](../tests/run-mtpz-keys.sh)
- [tests/run-mtpz-nullcheck.sh](../tests/run-mtpz-nullcheck.sh)
- [tests/run-photo-upload.sh](../tests/run-photo-upload.sh)
- [tests/run-podcast-state.sh](../tests/run-podcast-state.sh)
- [tests/run-podcasts.sh](../tests/run-podcasts.sh)
- [tests/run-ptp-cancel.sh](../tests/run-ptp-cancel.sh)
- [tests/run-ptp-optional-data.sh](../tests/run-ptp-optional-data.sh)
- [tests/run-send-retry.sh](../tests/run-send-retry.sh)
- [tests/run-shrink-giants.sh](../tests/run-shrink-giants.sh)
- [tests/run-spoken-inventory.sh](../tests/run-spoken-inventory.sh)
- [tests/run-symbols.sh](../tests/run-symbols.sh)
- [tests/run-tool-exec.sh](../tests/run-tool-exec.sh)
- [tests/run-transcode-profiles.sh](../tests/run-transcode-profiles.sh)
- [tests/run-upload-engine.sh](../tests/run-upload-engine.sh)
- [tests/run-upload-stream.sh](../tests/run-upload-stream.sh)
- [tests/run-video-named.sh](../tests/run-video-named.sh)
- [tests/run-wiretap.sh](../tests/run-wiretap.sh)
- [tests/run-zmdb-hostile.sh](../tests/run-zmdb-hostile.sh)
- [tests/run-zmdb-transport.sh](../tests/run-zmdb-transport.sh)
- [tests/send-retry.c](../tests/send-retry.c)
  - `zune_set_error()`: [tests/send-retry.c:16](../tests/send-retry.c#L16)
  - `zune_clear_error()`: [tests/send-retry.c:22](../tests/send-retry.c#L22)
  - `zune_get_error()`: [tests/send-retry.c:23](../tests/send-retry.c#L23)
  - `zune_autopsy_name()`: [tests/send-retry.c:24](../tests/send-retry.c#L24)
  - `zune_is_aborted()`: [tests/send-retry.c:28](../tests/send-retry.c#L28)
  - `zune_unjam()`: [tests/send-retry.c:29](../tests/send-retry.c#L29)
  - `ptp_transaction()`: [tests/send-retry.c:31](../tests/send-retry.c#L31)
  - `check()`: [tests/send-retry.c:68](../tests/send-retry.c#L68)
  - `reset()`: [tests/send-retry.c:74](../tests/send-retry.c#L74)
  - `write_file()`: [tests/send-retry.c:80](../tests/send-retry.c#L80)
  - `main()`: [tests/send-retry.c:86](../tests/send-retry.c#L86)
- [tests/shrink-giants.c](../tests/shrink-giants.c)
  - `check()`: [tests/shrink-giants.c:12](../tests/shrink-giants.c#L12)
  - `decode_golden()`: [tests/shrink-giants.c:136](../tests/shrink-giants.c#L136)
  - `decode_bounds()`: [tests/shrink-giants.c:181](../tests/shrink-giants.c#L181)
  - `bytes()`: [tests/shrink-giants.c:199](../tests/shrink-giants.c#L199)
  - `names()`: [tests/shrink-giants.c:213](../tests/shrink-giants.c#L213)
  - `main()`: [tests/shrink-giants.c:233](../tests/shrink-giants.c#L233)
- [tests/spoken-inventory.c](../tests/spoken-inventory.c)
  - `zune_set_error()`: [tests/spoken-inventory.c:9](../tests/spoken-inventory.c#L9)
  - `zune_clear_error()`: [tests/spoken-inventory.c:12](../tests/spoken-inventory.c#L12)
  - `check()`: [tests/spoken-inventory.c:13](../tests/spoken-inventory.c#L13)
  - `next()`: [tests/spoken-inventory.c:17](../tests/spoken-inventory.c#L17)
  - `mtp_get_object_handles()`: [tests/spoken-inventory.c:22](../tests/spoken-inventory.c#L22)
  - `mtp_get_object_info()`: [tests/spoken-inventory.c:34](../tests/spoken-inventory.c#L34)
  - `mtp_free_object_info()`: [tests/spoken-inventory.c:41](../tests/spoken-inventory.c#L41)
  - `number()`: [tests/spoken-inventory.c:42](../tests/spoken-inventory.c#L42)
  - `string()`: [tests/spoken-inventory.c:46](../tests/spoken-inventory.c#L46)
  - `mtp_get_object_prop_value()`: [tests/spoken-inventory.c:53](../tests/spoken-inventory.c#L53)
  - `reset()`: [tests/spoken-inventory.c:69](../tests/spoken-inventory.c#L69)
  - `main()`: [tests/spoken-inventory.c:74](../tests/spoken-inventory.c#L74)
- [tests/symbols.c](../tests/symbols.c)
  - `main()`: [tests/symbols.c:11](../tests/symbols.c#L11)
- [tests/tool_exec.c](../tests/tool_exec.c)
  - `zune_set_error()`: [tests/tool_exec.c:19](../tests/tool_exec.c#L19)
  - `zune_clear_error()`: [tests/tool_exec.c:25](../tests/tool_exec.c#L25)
  - `zune_get_error()`: [tests/tool_exec.c:26](../tests/tool_exec.c#L26)
  - `mtp_get_representative_sample()`: [tests/tool_exec.c:29](../tests/tool_exec.c#L29)
  - `mtp_get_object_to_file()`: [tests/tool_exec.c:37](../tests/tool_exec.c#L37)
  - `check()`: [tests/tool_exec.c:47](../tests/tool_exec.c#L47)
  - `read_log()`: [tests/tool_exec.c:57](../tests/tool_exec.c#L57)
  - `clear_log()`: [tests/tool_exec.c:72](../tests/tool_exec.c#L72)
  - `who_is()`: [tests/tool_exec.c:82](../tests/tool_exec.c#L82)
  - `argv_is()`: [tests/tool_exec.c:101](../tests/tool_exec.c#L101)
  - `exists()`: [tests/tool_exec.c:120](../tests/tool_exec.c#L120)
  - `injected()`: [tests/tool_exec.c:126](../tests/tool_exec.c#L126)
  - `write_file()`: [tests/tool_exec.c:137](../tests/tool_exec.c#L137)
  - `long_path()`: [tests/tool_exec.c:145](../tests/tool_exec.c#L145)
  - `test_snap_thumb()`: [tests/tool_exec.c:158](../tests/tool_exec.c#L158)
  - `test_arm_photo()`: [tests/tool_exec.c:255](../tests/tool_exec.c#L255)
  - `test_photo_thumb()`: [tests/tool_exec.c:270](../tests/tool_exec.c#L270)
  - `ends_with()`: [tests/tool_exec.c:287](../tests/tool_exec.c#L287)
  - `release()`: [tests/tool_exec.c:292](../tests/tool_exec.c#L292)
  - `temp_files()`: [tests/tool_exec.c:299](../tests/tool_exec.c#L299)
  - `valid_utf8()`: [tests/tool_exec.c:310](../tests/tool_exec.c#L310)
  - `private_temp()`: [tests/tool_exec.c:322](../tests/tool_exec.c#L322)
  - `test_temp_files()`: [tests/tool_exec.c:329](../tests/tool_exec.c#L329)
  - `test_lookup()`: [tests/tool_exec.c:390](../tests/tool_exec.c#L390)
  - `main()`: [tests/tool_exec.c:542](../tests/tool_exec.c#L542)
- [tests/transcode_profiles.c](../tests/transcode_profiles.c)
  - `zune_set_error()`: [tests/transcode_profiles.c:7](../tests/transcode_profiles.c#L7)
  - `zune_clear_error()`: [tests/transcode_profiles.c:15](../tests/transcode_profiles.c#L15)
  - `main()`: [tests/transcode_profiles.c:17](../tests/transcode_profiles.c#L17)
- [tests/upload-engine.c](../tests/upload-engine.c)
  - `zune_set_error()`: [tests/upload-engine.c:13](../tests/upload-engine.c#L13)
  - `zune_clear_error()`: [tests/upload-engine.c:19](../tests/upload-engine.c#L19)
  - `zune_get_error()`: [tests/upload-engine.c:20](../tests/upload-engine.c#L20)
  - `zune_autopsy_name()`: [tests/upload-engine.c:21](../tests/upload-engine.c#L21)
  - `zune_is_aborted()`: [tests/upload-engine.c:22](../tests/upload-engine.c#L22)
  - `zune_unjam()`: [tests/upload-engine.c:24](../tests/upload-engine.c#L24)
  - `usleep()`: [tests/upload-engine.c:25](../tests/upload-engine.c#L25)
  - `check()`: [tests/upload-engine.c:27](../tests/upload-engine.c#L27)
  - `next()`: [tests/upload-engine.c:45](../tests/upload-engine.c#L45)
  - `ptp_transaction()`: [tests/upload-engine.c:47](../tests/upload-engine.c#L47)
  - `reset()`: [tests/upload-engine.c:89](../tests/upload-engine.c#L89)
  - `run()`: [tests/upload-engine.c:102](../tests/upload-engine.c#L102)
  - `main()`: [tests/upload-engine.c:115](../tests/upload-engine.c#L115)
- [tests/upload-stream.c](../tests/upload-stream.c)
  - `check()`: [tests/upload-stream.c:11](../tests/upload-stream.c#L11)
  - `__wrap_malloc()`: [tests/upload-stream.c:19](../tests/upload-stream.c#L19)
  - `rd32()`: [tests/upload-stream.c:36](../tests/upload-stream.c#L36)
  - `rd16()`: [tests/upload-stream.c:37](../tests/upload-stream.c#L37)
  - `wr32()`: [tests/upload-stream.c:38](../tests/upload-stream.c#L38)
  - `respond()`: [tests/upload-stream.c:40](../tests/upload-stream.c#L40)
  - `payload()`: [tests/upload-stream.c:49](../tests/upload-stream.c#L49)
  - `fake_write()`: [tests/upload-stream.c:59](../tests/upload-stream.c#L59)
  - `fake_read()`: [tests/upload-stream.c:82](../tests/upload-stream.c#L82)
  - `fake_halt()`: [tests/upload-stream.c:93](../tests/upload-stream.c#L93)
  - `upload()`: [tests/upload-stream.c:98](../tests/upload-stream.c#L98)
  - `main()`: [tests/upload-stream.c:115](../tests/upload-stream.c#L115)
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
  - `zune_clear_error()`: [tests/video_named.c:169](../tests/video_named.c#L169)
  - `zune_get_error()`: [tests/video_named.c:170](../tests/video_named.c#L170)
  - `send_named()`: [tests/video_named.c:172](../tests/video_named.c#L172)
  - `main()`: [tests/video_named.c:180](../tests/video_named.c#L180)
- [tests/wiretap.c](../tests/wiretap.c)
  - `ptp_transaction()`: [tests/wiretap.c:12](../tests/wiretap.c#L12)
  - `check()`: [tests/wiretap.c:26](../tests/wiretap.c#L26)
  - `record()`: [tests/wiretap.c:39](../tests/wiretap.c#L39)
  - `reset()`: [tests/wiretap.c:49](../tests/wiretap.c#L49)
  - `truncated_storage_reply()`: [tests/wiretap.c:52](../tests/wiretap.c#L52)
  - `capture_default()`: [tests/wiretap.c:64](../tests/wiretap.c#L64)
  - `emit_talk()`: [tests/wiretap.c:83](../tests/wiretap.c#L83)
  - `replace_self()`: [tests/wiretap.c:87](../tests/wiretap.c#L87)
  - `log_many()`: [tests/wiretap.c:94](../tests/wiretap.c#L94)
  - `main()`: [tests/wiretap.c:100](../tests/wiretap.c#L100)
- [tests/zmdb-hostile.c](../tests/zmdb-hostile.c)
  - `zune_set_error()`: [tests/zmdb-hostile.c:17](../tests/zmdb-hostile.c#L17)
  - `zune_clear_error()`: [tests/zmdb-hostile.c:23](../tests/zmdb-hostile.c#L23)
  - `zune_get_error()`: [tests/zmdb-hostile.c:24](../tests/zmdb-hostile.c#L24)
  - `zune_zmdb_read_payload()`: [tests/zmdb-hostile.c:29](../tests/zmdb-hostile.c#L29)
  - `zune_get_playlists()`: [tests/zmdb-hostile.c:38](../tests/zmdb-hostile.c#L38)
  - `zune_free_videos()`: [tests/zmdb-hostile.c:44](../tests/zmdb-hostile.c#L44)
  - `check()`: [tests/zmdb-hostile.c:54](../tests/zmdb-hostile.c#L54)
  - `put16()`: [tests/zmdb-hostile.c:77](../tests/zmdb-hostile.c#L77)
  - `put32()`: [tests/zmdb-hostile.c:78](../tests/zmdb-hostile.c#L78)
  - `desc()`: [tests/zmdb-hostile.c:82](../tests/zmdb-hostile.c#L82)
  - `set_desc()`: [tests/zmdb-hostile.c:83](../tests/zmdb-hostile.c#L83)
  - `add_record()`: [tests/zmdb-hostile.c:90](../tests/zmdb-hostile.c#L90)
  - `build()`: [tests/zmdb-hostile.c:108](../tests/zmdb-hostile.c#L108)
  - `rejected()`: [tests/zmdb-hostile.c:144](../tests/zmdb-hostile.c#L144)
  - `parses_one_track()`: [tests/zmdb-hostile.c:157](../tests/zmdb-hostile.c#L157)
  - `index_offset_of()`: [tests/zmdb-hostile.c:178](../tests/zmdb-hostile.c#L178)
  - `hostile_cases()`: [tests/zmdb-hostile.c:182](../tests/zmdb-hostile.c#L182)
  - `zune_zmdb_read_response()`: [tests/zmdb-hostile.c:321](../tests/zmdb-hostile.c#L321)
  - `public_paths()`: [tests/zmdb-hostile.c:334](../tests/zmdb-hostile.c#L334)
  - `real_dump()`: [tests/zmdb-hostile.c:404](../tests/zmdb-hostile.c#L404)
  - `classic_table_bound()`: [tests/zmdb-hostile.c:428](../tests/zmdb-hostile.c#L428)
  - `build_compact_classic()`: [tests/zmdb-hostile.c:446](../tests/zmdb-hostile.c#L446)
  - `compact_classic_cases()`: [tests/zmdb-hostile.c:469](../tests/zmdb-hostile.c#L469)
  - `main()`: [tests/zmdb-hostile.c:504](../tests/zmdb-hostile.c#L504)
- [tests/zmdb-transport.c](../tests/zmdb-transport.c)
  - `zune_set_error()`: [tests/zmdb-transport.c:11](../tests/zmdb-transport.c#L11)
  - `zune_clear_error()`: [tests/zmdb-transport.c:19](../tests/zmdb-transport.c#L19)
  - `check()`: [tests/zmdb-transport.c:22](../tests/zmdb-transport.c#L22)
  - `enqueue()`: [tests/zmdb-transport.c:33](../tests/zmdb-transport.c#L33)
  - `pending()`: [tests/zmdb-transport.c:43](../tests/zmdb-transport.c#L43)
  - `reset_pipe()`: [tests/zmdb-transport.c:45](../tests/zmdb-transport.c#L45)
  - `fake_read()`: [tests/zmdb-transport.c:51](../tests/zmdb-transport.c#L51)
  - `fake_write()`: [tests/zmdb-transport.c:70](../tests/zmdb-transport.c#L70)
  - `put32()`: [tests/zmdb-transport.c:81](../tests/zmdb-transport.c#L81)
  - `script()`: [tests/zmdb-transport.c:86](../tests/zmdb-transport.c#L86)
  - `run()`: [tests/zmdb-transport.c:117](../tests/zmdb-transport.c#L117)
  - `main()`: [tests/zmdb-transport.c:133](../tests/zmdb-transport.c#L133)

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
| [HARDENING_0.3.0.md](HARDENING_0.3.0.md) | Operation Clean Getaway: libzune 0.3.0 Hardening Spec |
| [HD_ARTWORK_API.md](HD_ARTWORK_API.md) | Zune HD artist content C API |
| [HD_ARTWORK_RESEARCH.md](HD_ARTWORK_RESEARCH.md) | Zune HD Now Playing artist-art research |
| [HD_ARTWORK_TRANSPORT.md](HD_ARTWORK_TRANSPORT.md) | How Zune HD artwork travels over USB |
| [LINUX_TESTING.md](LINUX_TESTING.md) | Testing libzune on Linux |
| [MACOS_ZUNE_QUIRKS.md](MACOS_ZUNE_QUIRKS.md) | macOS Zune Quirks — Everything That's Different from Linux |
| [METADATA_PROBE.md](METADATA_PROBE.md) | Metadata probe contract — tester release R2 |
| [MTPZ_PROTOCOL.md](MTPZ_PROTOCOL.md) | MTPZ Authentication Protocol |
| [PROJECT_OVERVIEW.md](PROJECT_OVERVIEW.md) | libzune project overview |
| [PUBLIC_CREDENTIALS.md](PUBLIC_CREDENTIALS.md) | Private and public authentication builds |
| [RELEASE_0.2.0.md](RELEASE_0.2.0.md) | libzune 0.2.0 |
| [RELEASE_0.3.0.md](RELEASE_0.3.0.md) | libzune 0.3.0 |
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
