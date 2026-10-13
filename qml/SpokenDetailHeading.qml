import QtQuick
import QtQuick.Layouts

// Album-detail identity hierarchy, shared by local and device spoken pages.
ColumnLayout {
    id: heading
    property string category: ""
    property string title: ""
    property string author: ""
    property string narrator: ""
    property string summary: ""
    default property alias actions: actionFlow.data
    spacing: Theme.spaceXs
    data: [
    Text {
        text: heading.category.toUpperCase()
        color: Theme.textDim
        font.pixelSize: Theme.spokenCaption
        font.weight: Font.DemiBold
        font.letterSpacing: Theme.spaceXxs
    },
    GradientText {
        objectName: "spokenDetailTitle"
        Layout.fillWidth: true
        text: heading.title
        wrap: true
        font.family: Theme.displayFamily
        font.pixelSize: Theme.spokenDetailTitleSize
    },
    Text {
        Layout.fillWidth: true
        text: heading.author
        visible: !!text
        color: Theme.pink
        font.pixelSize: Theme.spokenBody
        wrapMode: Text.Wrap
    },
    Text {
        Layout.fillWidth: true
        text: heading.narrator ? "narrated by " + heading.narrator : ""
        visible: !!text
        color: Theme.textSecondary
        font.pixelSize: Theme.spokenCaption
        wrapMode: Text.Wrap
    },
    Text {
        Layout.fillWidth: true
        text: heading.summary
        color: Theme.textSecondary
        font.pixelSize: Theme.spokenCaption
        wrapMode: Text.Wrap
    },
    Flow {
        id: actionFlow
        objectName: "spokenDetailActions"
        Layout.fillWidth: true
        Layout.topMargin: Theme.spaceSm
        spacing: Theme.spaceSm
    }
    ]
}
