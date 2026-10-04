# Photo and playlist action checks

Run the production QML views against recording singletons, with isolated
temporary XDG directories and Qt's offscreen renderer. No app instance, real
library, network service, or device is opened.

```sh
bash tests/actions/photo-run.sh
PHOTO_TEST_INPUT=tests/actions/playlist bash tests/actions/photo-run.sh
```

The photo checks exercise real clicks on local/device thumbnail actions and
the full-screen/filmstrip transfer actions, including a mouse drag through
the gallery to a drop target underneath it. They check local vs device ID
payloads, captured album names, disconnect guards, encoded local paths, and
late resolver callbacks from a closed gallery session.

The playlist checks use the actual device playlist rows and Builder. They
cover device save/delete IDs and disconnected guards, membership refresh,
offline name/order/metadata editing, preservation of album artist/year/genre,
pointer clicks through the Builder's hover-only edit/up/down/remove controls,
and a real Builder member drag carrying authoritative library metadata.

The test module links production QML; only external service singletons and
appearance preferences are replaced by files in `photo-stubs/`.

Custom photo album checks cover staged New/Edit/Add, truthful save failure,
offline folder/selected-group/full-screen/filmstrip drags, Ctrl/Shift selection,
selection across folders, selection source order, a 5,000-photo selection with
fewer than 100 instantiated grid children, and draft-aware thumbnail plus
buttons. Picker callbacks retain destination intent and reject a closed or
replaced draft. Album deletion preserves media and device transfers use only
direct members. The photo suite passes 22 checks including init/cleanup.
The photo suite also passes using `PHOTO_TEST_PLATFORM=xcb PHOTO_TEST_BACKEND=rhi
QSG_RHI_BACKEND=opengl` on the desktop GPU. The separate photo-builder suite has
10 passing checks in both rendering modes, including real bitmap thumbnails.
Software Qt Quick alone cannot validate existing OpacityMask layers. Neither
suite establishes physical device delivery; see `../photo-albums/README.md`.
