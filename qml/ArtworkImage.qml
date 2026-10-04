import QtQuick
import Zuuned

// Shared display treatment. `source` always remains the chosen original URL;
// customization, drag payloads and device transfers never receive a print cache URL.
Item {
    id: artwork
    property url source
    property size sourceSize: Qt.size(-1, -1)
    property int fillMode: Image.Stretch
    property int horizontalAlignment: Image.AlignHCenter
    property int verticalAlignment: Image.AlignVCenter
    property bool asynchronous: true
    property bool cache: true
    property bool retainWhileLoading: false
    property bool mipmap: true
    property bool mirror: false
    property bool treatmentEnabled: true
    property string printStyle: AppSettings.artworkStyle
    property int printDetail: printStyle === "cleanInk" ? AppSettings.artworkCleanDetail : 100
    property int printTexture: printStyle === "wornPrint"
        ? AppSettings.artworkWornTexture : AppSettings.artworkHalftoneTexture
    property int printDotSize: AppSettings.artworkHalftoneDotSize
    property bool printMonochrome: AppSettings.artworkHalftoneMonochrome
    readonly property bool printing: request.busy || printed.status === Image.Loading
    readonly property bool treated: _readySource.toString() !== ""
    readonly property int status: treated ? Image.Ready : original.status
    readonly property real progress: treated ? 1 : original.progress
    readonly property real paintedWidth: treated ? printed.paintedWidth : original.paintedWidth
    readonly property real paintedHeight: treated ? printed.paintedHeight : original.paintedHeight
    readonly property url displaySource: treated ? _readySource : source
    property url _printedSource
    property url _readySource
    readonly property size _decodeSize: Qt.size(
        sourceSize.width > 0 ? sourceSize.width : Math.min(1024, Math.max(1, Math.ceil(width * 2))),
        sourceSize.height > 0 ? sourceSize.height : -1)

    function clearPrint() {
        _readySource = ""
        _printedSource = ""
        if (request) request.displayed("")
    }
    onSourceChanged: clearPrint()
    onPrintStyleChanged: { if (printStyle === "original") clearPrint() }
    onTreatmentEnabledChanged: { if (!treatmentEnabled) clearPrint() }
    function refresh() { request.refresh() }

    ArtworkRequest {
        id: request
        source: artwork.source
        style: artwork.treatmentEnabled ? artwork.printStyle : "original"
        detail: artwork.printDetail
        texture: artwork.printTexture
        dotSize: artwork.printDotSize
        monochrome: artwork.printMonochrome
        onResultChanged: {
            if (result.toString() !== "") artwork._printedSource = result
        }
        onErrorChanged: { if (error !== "") artwork.clearPrint() }
    }

    Image {
        id: original
        anchors.fill: parent
        source: artwork.source
        sourceSize: artwork._decodeSize
        fillMode: artwork.fillMode
        horizontalAlignment: artwork.horizontalAlignment
        verticalAlignment: artwork.verticalAlignment
        asynchronous: artwork.asynchronous
        cache: artwork.cache
        // Changing the media source must never display another item's artwork.
        retainWhileLoading: false
        mipmap: artwork.mipmap
        mirror: artwork.mirror
        smooth: artwork.smooth
        // Opacity updates dirty the texture even inside a hidden composite
        // mask source. Toggling child visibility there can leave a stale node.
        opacity: artwork.treated ? 0 : 1
    }
    Image {
        id: printed
        anchors.fill: parent
        source: artwork._printedSource
        sourceSize: artwork._decodeSize
        fillMode: artwork.fillMode
        horizontalAlignment: artwork.horizontalAlignment
        verticalAlignment: artwork.verticalAlignment
        asynchronous: artwork.asynchronous
        cache: artwork.cache
        retainWhileLoading: true
        mipmap: artwork.mipmap
        mirror: artwork.mirror
        smooth: artwork.smooth
        opacity: artwork.treated ? 1 : 0
        onStatusChanged: {
            if (status === Image.Ready) {
                artwork._readySource = source
                request.displayed(source)
            }
            else if (status === Image.Error || status === Image.Null) artwork._readySource = ""
            if (status === Image.Error) artwork.clearPrint()
        }
    }
}
