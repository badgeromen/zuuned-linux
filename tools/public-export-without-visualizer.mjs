// Export-only transforms. Never modify files in the private working tree.
export function omitVisualizer(name) {
    return [
        'src/player/VisualizerService.cpp', 'src/player/VisualizerService.h',
        'src/player/PortalRenderer.h', 'src/player/VisualizerPresets.h',
        'src/player/PortalState.h', 'src/player/PortalShapeMorph.h',
        'src/player/MusicStreamIdentity.h', 'docs/MUSIC_VISUALIZER.md',
        'tests/spoken-media/native/VisualizerService.qml',
    ].includes(name) || name.startsWith('tests/visualizer/');
}

function replaceExactly(text, before, after, name) {
    const count = text.split(before).length - 1;
    if (count !== 1) throw Error('Visualizer export block drift in ' + name + '; nothing exported.');
    return text.replace(before, after);
}

// Deliberately exact boundaries: source changes require this release transform
// and its fixtures to be reviewed again instead of partially stripping code.
const transforms = {
    'CMakeLists.txt': [
        ['    src/player/VisualizerService.cpp\n    src/player/VisualizerService.h\n', ''],
        [`# Experimental MilkDrop renderer. No configure-time or runtime downloads.
option(ZUUNED_VISUALIZER "Enable experimental projectM fullscreen visualizer" OFF)
if(ZUUNED_VISUALIZER)
    pkg_check_modules(PROJECTM REQUIRED IMPORTED_TARGET projectM-4>=4.1)
    pkg_check_modules(PULSE REQUIRED IMPORTED_TARGET libpulse)
    target_compile_definitions(zuuned PRIVATE ZUUNED_VISUALIZER)
    target_link_libraries(zuuned PRIVATE PkgConfig::PROJECTM PkgConfig::PULSE)
endif()

`, ''],
    ],
    'qml/Main.qml': [[`    Connections {
        target: VideoPlayerService
        function onStateChanged() {
            if (VideoPlayerService.active) VisualizerService.close()
        }
    }
`, '']],
    'qml/NowPlayingIsland.qml': [[`        Button {
            id: visualizeButton
            visible: !island.spoken && VisualizerService.available
            enabled: !PlayerService.stopped && !VideoPlayerService.active
            text: "visualize · fullscreen"
            Layout.fillWidth: true
            padding: Theme.spaceSm
            contentItem: Text {
                text: visualizeButton.text
                color: visualizeButton.enabled ? Theme.pink : Theme.textDim
                horizontalAlignment: Text.AlignHCenter
            }
            background: Rectangle {
                color: visualizeButton.hovered ? Theme.cardHover : Theme.cardBg
                border.color: visualizeButton.activeFocus ? Theme.focusRing : Theme.borderLight
                border.width: Theme.hairline
                radius: Theme.radiusMd
            }
            onClicked: VisualizerService.open(PlayerService)
        }
`, '']],
    'src/player/MpvController.cpp': [
        ['#include "MusicStreamIdentity.h"\n', ''],
        [`        // Distinguish music from this process's video audio for an opt-in,
        // per-stream visualizer monitor. Does not change output routing.
        mpv_set_option_string(m_mpv, "audio-client-name", musicStreamIdentity().constData());
`, ''],
    ],
    'tests/spoken-media/native/CMakeLists.txt': [
        ['set(QML_FILES Main.qml DeviceService.qml SyncEngine.qml TrayState.qml VisualizerService.qml VideoPlayerService.qml',
         'set(QML_FILES Main.qml DeviceService.qml SyncEngine.qml TrayState.qml VideoPlayerService.qml'],
        ['set_source_files_properties(DeviceService.qml SyncEngine.qml TrayState.qml VisualizerService.qml VideoPlayerService.qml',
         'set_source_files_properties(DeviceService.qml SyncEngine.qml TrayState.qml VideoPlayerService.qml'],
    ],
};

export function stripVisualizer(staged) {
    const byName = new Map(staged.map(item => [item.name, item]));
    for (const [name, replacements] of Object.entries(transforms)) {
        const item = byName.get(name);
        if (!item) throw Error('Missing visualizer export input ' + name + '; nothing exported.');
        let text = item.bytes.toString('utf8');
        for (const [before, after] of replacements) text = replaceExactly(text, before, after, name);
        item.bytes = Buffer.from(text);
    }
    // Catch newly introduced source/dependency/test references outside the known
    // blocks. The export helper and historical documentation are not app code.
    const privateSymbol = /VisualizerService|PortalRenderer|PortalState|PortalShapeMorph|VisualizerPresets|MusicStreamIdentity|musicStreamIdentity|ZUUNED_VISUALIZER|projectM-4|PkgConfig::PROJECTM|tests\/visualizer/;
    for (const item of staged) {
        if ((item.name === 'CMakeLists.txt' || /^(?:src|qml|tests|ci|packaging)\//.test(item.name))
            && !item.name.startsWith('tests/public-source/')
            && /\.(?:qml|[ch]|cpp|hpp|mjs|sh|txt)$/.test(item.name)
            && privateSymbol.test(item.bytes.toString('utf8')))
            throw Error('Unreviewed visualizer reference in ' + item.name + '; nothing exported.');
    }
    return Object.keys(transforms);
}
