import QtQuick
import Qt5Compat.GraphicalEffects

// Match AlbumArt's image mask; a rounded background alone does not clip pixels.
ArtworkImage {
    id: artwork
    property real cornerRadius: Theme.radiusMd
    layer.enabled: cornerRadius > 0
    layer.effect: OpacityMask {
        maskSource: Item {
            width: artwork.width
            height: artwork.height
            Rectangle {
                anchors.centerIn: parent
                width: artwork.fillMode === Image.PreserveAspectFit && artwork.paintedWidth > 0
                    ? Math.min(parent.width, artwork.paintedWidth) : parent.width
                height: artwork.fillMode === Image.PreserveAspectFit && artwork.paintedHeight > 0
                    ? Math.min(parent.height, artwork.paintedHeight) : parent.height
                radius: artwork.cornerRadius
            }
        }
    }
}
