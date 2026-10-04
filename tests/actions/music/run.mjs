import { mkdtempSync, mkdirSync, readFileSync, readdirSync, rmSync, symlinkSync, writeFileSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { dirname, join, resolve } from 'node:path';
import { fileURLToPath } from 'node:url';
import { spawnSync } from 'node:child_process';

const testDir = dirname(fileURLToPath(import.meta.url));
const repo = resolve(testDir, '../../..');
const root = mkdtempSync(join(tmpdir(), 'zuuned-music-actions-'));
const moduleDir = join(root, 'imports/Zuuned');
mkdirSync(moduleDir, { recursive: true });
mkdirSync(join(root, 'runtime'), { mode: 0o700 });
const stubs = {
  DeviceService: `property bool connected: false
    property bool busy: false
    property bool devicePresent: false
    property var artPaths: ({})
    property var onDeviceKeys: ({})
    property int musicIdentityRevision: 0
    function musicIdentityPeers(track) { return [] }
    function hasDeviceTrack(track, peers) { return onDeviceKeys[((track.albumartist || track.artist) + "\\t" + track.album + "\\t" + track.title).toLowerCase()] === true }
    property int trackCount: rows.length
    property var rows: []
    property var genresList: []
    property var saved: []
    property var deleted: []
    property QtObject tracks: QtObject {
        signal modelReset()
        signal rowsInserted()
        signal rowsRemoved()
        signal dataChanged()
        function rowsSnapshot() { return DeviceService.rows }
    }
    signal trackSaved(string filename, bool ok)
    function requestArt() {}
    function saveTrackToLibrary(id) { saved = saved.concat([id]) }
    function purgeItems(ids) { deleted = ids }`,
  LibraryService: `property var rows: []
    property int localTrackIdentityRevision: 0
    property bool localTrackFound: false
    function hasLocalTrack(artist, album, title, disc, track, discReliable, trackReliable, sourcePeers) { return localTrackFound }
    function musicIdentityPeers(track) { return [] }
    property int trackCount: rows.length
    property var artistsList: []
    property var albumsList: []
    property var genresList: []
    property var artPaths: ({})
    property var artistImages: ({})
    property var playlists: []
    property var playlistRows: ({})
    property var collectionArts: ({})
    property int collectionArtRevision: 0
    property var applications: []
    function collectionArt(kind, key) { return collectionArts[kind + ":" + key] || "" }
    property bool scanning: false
    property string scanStage: ""
    property var playlistAdds: []
    property var customization: ({})
    property QtObject tracks: QtObject {
        signal modelReset()
        signal rowsInserted()
        signal rowsRemoved()
        signal dataChanged()
        function rowsSnapshot() { return LibraryService.rows }
    }
    signal customizeArtReady(string requestId, var urls, string error)
    signal libraryChanged()
    signal customizeIdentityReady(string requestId, var matches, string error)
    signal customizationFinished(string requestId, bool success, string error)
    function requestArtistImage() {}
    function requestArt() {}
    function artKey(artist, album) { return artist + "\\n" + album }
    function dominantColor() { return "#dd3388" }
    function customizeContext(kind, context) { customization = context; return context }
    function requestCustomizeArt() {}
    function playlistTracks(id) { return playlistRows[String(id)] || [] }
    function applyCustomization(requestId, kind, context, draft) {
        applications = applications.concat([{requestId, kind, context, draft}])
    }
    function playlistCoverPaths() { return [] }
    function addToPlaylist(id, ids) { playlistAdds = ids }`,
  PlayerService: `property bool playing: false
    property var queue: []
    property int queueIndex: -1
    property var nextItems: []
    property var appended: []
    function setQueue(items, index) { queue = items; queueIndex = index }
    function appendToQueue(items) { appended = items }
    function playNext(items) { nextItems = items }`,
  SyncEngine: `property var added: []
    function addTracks(items) { added = items; return {added: items.length} }`,
  TrayState: `property bool building: false
    property int playlistId: -1
    property string name: ""
    property var tracks: []
    signal pulsed()
    property var added: []
    property int dragging: 0
    function addTracks(items) { added = items }
    function dragStarted() { dragging++ }
    function dragEnded() { dragging-- }
    function openNew() { building = true }
    function openExisting(id, title) { playlistId = id; name = title; tracks = LibraryService.playlistTracks(id); building = true }
    function refreshMetadata() {}`,
  AppSettings: `property string headerFont: "marker"
    property bool letterBreaks: false
    readonly property string artworkStyle: "original"
    readonly property int artworkCleanDetail: 100
    readonly property int artworkHalftoneTexture: 14
    readonly property int artworkHalftoneDotSize: 17
    readonly property bool artworkHalftoneMonochrome: false
    readonly property int artworkWornTexture: 14`,
  Prefs: `property string playerStyle: "disc"`
};
let qmldir = 'module Zuuned\n';
for (const name of readdirSync(join(repo, 'qml'))) {
  if (name === 'fonts' || name === 'images' || name.endsWith('.js')) {
    symlinkSync(join(repo, 'qml', name), join(moduleDir, name));
    continue;
  }
  if (!name.endsWith('.qml')) continue;
  const type = name.slice(0, -4);
  if (Object.hasOwn(stubs, type)) continue;
  symlinkSync(join(repo, 'qml', name), join(moduleDir, name));
  qmldir += `${readFileSync(join(repo, 'qml', name), 'utf8').includes('pragma Singleton') ? 'singleton ' : ''}${type} 1.0 ${name}\n`;
}
for (const [name, body] of Object.entries(stubs)) {
  writeFileSync(join(moduleDir, `${name}.qml`), `pragma Singleton\nimport QtQuick\nQtObject {\n${body}\n}\n`);
  qmldir += `singleton ${name} 1.0 ${name}.qml\n`;
}
writeFileSync(join(moduleDir, 'ArtworkRequest.qml'), readFileSync(join(repo, 'tests/artwork-print/stubs/ArtworkRequest.qml')));
qmldir += 'ArtworkRequest 1.0 ArtworkRequest.qml\n';
writeFileSync(join(moduleDir, 'qmldir'), qmldir);
try {
  const result = spawnSync('/usr/lib/qt6/bin/qmltestrunner', [
    '-import', join(root, 'imports'), '-input', process.env.MUSIC_TEST_INPUT || testDir, ...process.argv.slice(2)
  ], { stdio: 'inherit', env: { ...process.env, QT_QPA_PLATFORM: process.env.MUSIC_TEST_PLATFORM || 'offscreen',
    QT_QUICK_BACKEND: process.env.MUSIC_TEST_RENDERER === 'opengl' ? 'rhi' : 'software',
    QSG_RHI_BACKEND: process.env.MUSIC_TEST_RENDERER === 'opengl' ? 'opengl' : '', QT_QPA_PLATFORMTHEME: '',
    XDG_RUNTIME_DIR: join(root, 'runtime'), XDG_CONFIG_HOME: join(root, 'config'),
    XDG_DATA_HOME: join(root, 'data'), XDG_CACHE_HOME: join(root, 'cache') } });
  process.exitCode = result.status ?? 1;
} finally { rmSync(root, { recursive: true, force: true }); }
