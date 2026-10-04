import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

// Display-only print choices. The owner supplies already available artwork;
// opening Appearance never starts an artwork lookup or modifies the library.
ColumnLayout {
    id: root
    property url sampleSource: ""
    property bool fineTuning: false
    readonly property string style: AppSettings.artworkStyle
    readonly property string styleLabel: style === "cleanInk" ? "clean ink"
                                        : style === "halftone" ? "halftone"
                                        : style === "wornPrint" ? "worn print" : "original"
    Layout.fillWidth: true
    Layout.maximumWidth: Theme.artworkSettingsMaxWidth
    spacing: Theme.spaceLg
    onStyleChanged: fineTuning = false

    component PrintAction: Button {
        id: action
        property bool selected: false
        implicitHeight: Theme.artworkSettingsControlHeight
        leftPadding: Theme.spaceXs
        rightPadding: Theme.spaceXs
        topPadding: Theme.spaceSm
        bottomPadding: Theme.spaceSm
        hoverEnabled: true
        focusPolicy: Qt.StrongFocus
        contentItem: Text {
            text: action.text
            font.pixelSize: Theme.customizeBodySize
            font.weight: Font.Light
            color: action.selected ? Theme.pink
                  : action.hovered || action.activeFocus ? Theme.textPrimary : Theme.textSecondary
            verticalAlignment: Text.AlignVCenter
        }
        background: Item {
            Rectangle {
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.bottom: parent.bottom
                height: Theme.hairline * 2
                color: Theme.pink
                visible: action.selected || action.activeFocus
            }
        }
    }

    component PrintSlider: ColumnLayout {
        id: control
        required property string label
        required property int amount
        property string startLabel: ""
        property string endLabel: ""
        property alias slider: slider
        signal moved(int amount)
        Layout.fillWidth: true
        Layout.maximumWidth: Theme.artworkSettingsSliderWidth
        spacing: Theme.spaceXxs
        RowLayout {
            Layout.fillWidth: true
            Text {
                Layout.fillWidth: true
                text: control.label
                font.pixelSize: Theme.customizeBodySize
                color: Theme.textPrimary
            }
            Text {
                text: control.amount
                font.pixelSize: Theme.customizeCaptionSize
                color: Theme.textSecondary
            }
        }
        Slider {
            id: slider
            objectName: control.objectName + "Slider"
            Layout.fillWidth: true
            implicitHeight: Theme.artworkSettingsControlHeight
            from: 0
            to: 100
            stepSize: 1
            value: control.amount
            Accessible.name: control.label
            onMoved: control.moved(Math.round(value))
            background: Rectangle {
                x: slider.leftPadding
                y: slider.topPadding + slider.availableHeight / 2 - height / 2
                width: slider.availableWidth
                height: Theme.hairline * 2
                color: Theme.borderLight
                Rectangle {
                    width: slider.visualPosition * parent.width
                    height: parent.height
                    color: Theme.pink
                }
            }
            handle: Rectangle {
                x: slider.leftPadding + slider.visualPosition * (slider.availableWidth - width)
                y: slider.topPadding + slider.availableHeight / 2 - height / 2
                width: Theme.spaceMd
                height: width
                radius: width / 2
                color: slider.pressed || slider.activeFocus ? Theme.pink : Theme.textPrimary
            }
        }
        RowLayout {
            visible: control.startLabel.length > 0
            Layout.fillWidth: true
            Text {
                Layout.fillWidth: true
                text: control.startLabel
                font.pixelSize: Theme.customizeCaptionSize
                color: Theme.textSecondary
            }
            Text {
                text: control.endLabel
                font.pixelSize: Theme.customizeCaptionSize
                color: Theme.textSecondary
            }
        }
    }

    Flow {
        Layout.fillWidth: true
        spacing: Theme.spaceMd
        Repeater {
            model: [
                { label: "original", value: "original" },
                { label: "clean ink", value: "cleanInk" },
                { label: "halftone", value: "halftone" },
                { label: "worn print", value: "wornPrint" }
            ]
            delegate: PrintAction {
                required property var modelData
                objectName: "artworkStyle_" + modelData.value
                text: modelData.label
                selected: root.style === modelData.value
                Accessible.role: Accessible.PageTab
                Accessible.name: text
                onClicked: AppSettings.artworkStyle = modelData.value
            }
        }
    }

    RowLayout {
        id: samples
        Layout.fillWidth: true
        Layout.maximumWidth: Theme.artworkSettingsPreviewSize * 2 + Theme.spaceLg
        spacing: Theme.spaceLg
        ColumnLayout {
            Layout.fillWidth: true
            Layout.minimumWidth: 0
            Layout.preferredWidth: Theme.artworkSettingsPreviewSize
            spacing: Theme.spaceSm
            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: width
                color: Theme.artworkPlaceholder
                Image {
                    id: originalSample
                    objectName: "artworkOriginalSample"
                    anchors.fill: parent
                    source: root.sampleSource
                    sourceSize: Qt.size(Theme.artworkSettingsPreviewSize * 2,
                                        Theme.artworkSettingsPreviewSize * 2)
                    fillMode: Image.PreserveAspectFit
                    asynchronous: true
                    mipmap: true
                }
            }
            Text {
                text: "your original"
                font.pixelSize: Theme.customizeCaptionSize
                color: Theme.textSecondary
            }
        }
        ColumnLayout {
            Layout.fillWidth: true
            Layout.minimumWidth: 0
            Layout.preferredWidth: Theme.artworkSettingsPreviewSize
            spacing: Theme.spaceSm
            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: width
                color: Theme.artworkPlaceholder
                ArtworkImage {
                    objectName: "artworkTreatedSample"
                    anchors.fill: parent
                    source: root.sampleSource
                    sourceSize: originalSample.sourceSize
                    fillMode: Image.PreserveAspectFit
                    asynchronous: true
                }
            }
            Text {
                text: root.styleLabel
                font.pixelSize: Theme.customizeCaptionSize
                color: root.style === "original" ? Theme.textSecondary : Theme.pink
            }
        }
    }

    Text {
        visible: root.sampleSource.toString().length === 0
        Layout.fillWidth: true
        text: "Your collection artwork will appear here."
        font.pixelSize: Theme.customizeCaptionSize
        color: Theme.textSecondary
        wrapMode: Text.WordWrap
    }

    PrintAction {
        objectName: "artworkFineTune"
        visible: root.style !== "original"
        text: root.fineTuning ? "fine-tune −" : "fine-tune +"
        selected: root.fineTuning
        Accessible.name: "Fine-tune " + root.styleLabel
        onClicked: root.fineTuning = !root.fineTuning
    }

    ColumnLayout {
        objectName: "artworkTuningControls"
        visible: root.style !== "original" && root.fineTuning
        Layout.fillWidth: true
        spacing: Theme.spaceMd
        PrintSlider {
            objectName: "artworkCleanDetail"
            visible: root.style === "cleanInk"
            label: "Detail"
            amount: AppSettings.artworkCleanDetail
            onMoved: (amount) => AppSettings.artworkCleanDetail = amount
        }
        PrintSlider {
            objectName: "artworkHalftoneTexture"
            visible: root.style === "halftone"
            label: "Print strength"
            amount: AppSettings.artworkHalftoneTexture
            onMoved: (amount) => AppSettings.artworkHalftoneTexture = amount
        }
        PrintSlider {
            objectName: "artworkHalftoneDotSize"
            visible: root.style === "halftone"
            label: "Dot size"
            startLabel: "fine"
            endLabel: "comic"
            amount: AppSettings.artworkHalftoneDotSize
            onMoved: (amount) => AppSettings.artworkHalftoneDotSize = amount
        }
        RowLayout {
            visible: root.style === "halftone"
            spacing: Theme.spaceMd
            PrintAction {
                objectName: "artworkHalftoneColor"
                text: "color"
                selected: !AppSettings.artworkHalftoneMonochrome
                onClicked: AppSettings.artworkHalftoneMonochrome = false
            }
            PrintAction {
                objectName: "artworkHalftoneMonochrome"
                text: "black & white"
                selected: AppSettings.artworkHalftoneMonochrome
                onClicked: AppSettings.artworkHalftoneMonochrome = true
            }
        }
        PrintSlider {
            objectName: "artworkWornTexture"
            visible: root.style === "wornPrint"
            label: "Wear"
            amount: AppSettings.artworkWornTexture
            onMoved: (amount) => AppSettings.artworkWornTexture = amount
        }
        PrintAction {
            objectName: "artworkResetLook"
            text: "reset this look"
            onClicked: AppSettings.resetArtworkStyle()
        }
    }
}
