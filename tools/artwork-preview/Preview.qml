import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import QtQuick.Dialogs
import Qt5Compat.GraphicalEffects
import ZuunedPrintPreview

ApplicationWindow {
    id: window
    objectName: "artworkPrintPreview"
    visible: true
    flags: Qt.Dialog
    width: PreviewTheme.windowWidth
    height: PreviewTheme.windowHeight
    minimumWidth: PreviewTheme.minimumWidth
    minimumHeight: PreviewTheme.minimumHeight
    title: "ZUUNED · Your art. Your ink."
    color: Theme.bg
    readonly property var sample: PrintPreview.samples[PrintPreview.index] || ({})
    readonly property bool artistSample: sample.kind === "artist"
    readonly property bool posterSample: sample.kind === "movie" || sample.kind === "series"

    component QuietAction: AbstractButton {
        id: action
        hoverEnabled: true
        padding: Theme.spaceSm
        implicitWidth: contentItem.implicitWidth + leftPadding + rightPadding
        implicitHeight: Theme.spaceXxxl
        contentItem: Text {
            text: action.text
            textFormat: Text.PlainText
            color: action.checked || action.hovered || action.activeFocus ? Theme.activePink : Theme.textSecondary
            font.pixelSize: Theme.customizeBodySize
            verticalAlignment: Text.AlignVCenter
        }
        background: Rectangle {
            anchors.bottom: parent.bottom
            height: action.checked ? Theme.spaceXxxs : Theme.hairline
            color: action.checked || action.activeFocus ? Theme.pink : Theme.transparent
        }
        HoverHandler { cursorShape: Qt.PointingHandCursor }
    }

    component InkSlider: Slider {
        id: control
        from: 0
        to: 100
        stepSize: 1
        implicitWidth: PreviewTheme.controlWidth
        implicitHeight: Theme.spaceXxxl
        leftPadding: Theme.spaceSm
        rightPadding: Theme.spaceSm
        background: Rectangle {
            x: control.leftPadding
            y: control.topPadding + control.availableHeight / 2 - height / 2
            implicitHeight: Theme.spaceXxxs
            width: control.availableWidth
            height: implicitHeight
            color: Theme.borderLight
            Rectangle {
                width: control.visualPosition * parent.width
                height: parent.height
                color: Theme.pink
            }
        }
        handle: Rectangle {
            x: control.leftPadding + control.visualPosition * (control.availableWidth - width)
            y: control.topPadding + control.availableHeight / 2 - height / 2
            width: Theme.spaceMd
            height: width
            radius: width / 2
            color: control.pressed || control.activeFocus ? Theme.activePink : Theme.textPrimary
            border.width: control.activeFocus ? Theme.spaceXxxs : 0
            border.color: Theme.pink
        }
    }

    component ArtistThumbnail: Item {
        id: thumb
        property url source
        implicitWidth: PreviewTheme.largeThumbnail
        implicitHeight: implicitWidth
        Rectangle {
            anchors.fill: parent
            radius: width / 2
            color: Theme.artworkPlaceholder
        }
        Image {
            anchors.fill: parent
            source: thumb.source
            sourceSize.width: Math.ceil(thumb.width * 2)
            sourceSize.height: Math.ceil(thumb.height * 2)
            fillMode: Image.PreserveAspectCrop
            asynchronous: true
            retainWhileLoading: true
            layer.enabled: true
            layer.effect: OpacityMask {
                maskSource: Rectangle {
                    width: thumb.width
                    height: thumb.height
                    radius: width / 2
                }
            }
        }
    }

    ScrollView {
        id: pageScroll
        anchors.fill: parent
        anchors.margins: Theme.spaceXxxl
        contentWidth: availableWidth
        contentHeight: page.height
        clip: true
        ScrollBar.horizontal.policy: ScrollBar.AlwaysOff
        ScrollBar.vertical: ScrollBar {
            width: Theme.spaceXs
            policy: ScrollBar.AsNeeded
            contentItem: Rectangle { radius: Theme.spaceXxxs; color: Theme.textDim }
            background: Item {}
        }

    ColumnLayout {
        id: page
        width: pageScroll.availableWidth
        height: Math.max(implicitHeight, pageScroll.availableHeight)
        spacing: Theme.spaceXl

        ColumnLayout {
            Layout.fillWidth: true
            spacing: Theme.spaceSm
            Text {
                text: "ZUUNED / PRINT STUDY"
                font.pixelSize: Theme.customizeLabelSize
                font.letterSpacing: Theme.spaceXxxs
                color: Theme.textSecondary
            }
            GradientText {
                text: "YOUR ART. YOUR INK."
                font.family: Theme.displayFamily
                font.pixelSize: PreviewTheme.headingSize
            }
            Text {
                text: "Keep the face. Change the print."
                font.pixelSize: Theme.customizeBodySize
                color: Theme.textSecondary
            }
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.spaceLg
            ListView {
                id: samples
                objectName: "printSamples"
                Layout.fillWidth: true
                Layout.preferredHeight: PreviewTheme.sampleHeight
                orientation: ListView.Horizontal
                spacing: Theme.spaceLg
                clip: true
                boundsBehavior: Flickable.StopAtBounds
                model: PrintPreview.samples
                currentIndex: PrintPreview.index
                onCurrentIndexChanged: positionViewAtIndex(currentIndex, ListView.Contain)
                delegate: AbstractButton {
                    id: choice
                    required property var modelData
                    required property int index
                    objectName: "printSample" + index
                    width: Math.min(PreviewTheme.sampleMaximumWidth,
                                    choiceLabel.implicitWidth + Theme.spaceLg)
                    height: samples.height
                    hoverEnabled: true
                    onClicked: PrintPreview.index = index
                    Accessible.name: modelData.kind + ": " + modelData.name
                    contentItem: Column {
                        spacing: Theme.spaceXs
                        Text {
                            text: choice.modelData.kind
                            textFormat: Text.PlainText
                            font.pixelSize: Theme.customizeLabelSize
                            font.letterSpacing: Theme.hairline
                            color: Theme.textDim
                        }
                        Text {
                            id: choiceLabel
                            width: parent.width
                            text: choice.modelData.name
                            textFormat: Text.PlainText
                            font.pixelSize: Theme.customizeBodySize
                            color: PrintPreview.index === choice.index ? Theme.textPrimary
                                : choice.hovered ? Theme.textMid : Theme.textSecondary
                            elide: Text.ElideRight
                        }
                    }
                    background: Rectangle {
                        anchors.bottom: parent.bottom
                        height: Theme.spaceXxxs
                        color: PrintPreview.index === choice.index ? Theme.pink
                            : choice.activeFocus ? Theme.borderLight : Theme.transparent
                    }
                    HoverHandler { cursorShape: Qt.PointingHandCursor }
                }
                ScrollBar.horizontal: ScrollBar {
                    height: Theme.spaceXxs
                    policy: ScrollBar.AsNeeded
                    contentItem: Rectangle { radius: Theme.spaceXxxs; color: Theme.textDim }
                    background: Item {}
                }
            }
            QuietAction {
                text: "your computer ↗"
                onClicked: fileDialog.open()
            }
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.spaceXxxl
            ColumnLayout {
                Layout.fillWidth: true
                Layout.maximumWidth: PreviewTheme.controlWidth
                spacing: 0
                RowLayout {
                    Layout.fillWidth: true
                    Text { text: "Detail"; font.pixelSize: Theme.customizeBodySize; color: Theme.textPrimary }
                    Item { Layout.fillWidth: true }
                    Text { text: PrintPreview.detail; font.pixelSize: Theme.customizeCaptionSize; color: Theme.textDim }
                }
                InkSlider {
                    objectName: "printDetail"
                    Layout.fillWidth: true
                    value: PrintPreview.detail
                    onMoved: PrintPreview.detail = Math.round(value)
                    Accessible.name: "Detail"
                }
            }
            ColumnLayout {
                Layout.fillWidth: true
                Layout.maximumWidth: PreviewTheme.controlWidth
                spacing: 0
                RowLayout {
                    Layout.fillWidth: true
                    Text { text: "Texture"; font.pixelSize: Theme.customizeBodySize; color: Theme.textPrimary }
                    Item { Layout.fillWidth: true }
                    Text { text: PrintPreview.texture; font.pixelSize: Theme.customizeCaptionSize; color: Theme.textDim }
                }
                InkSlider {
                    objectName: "printTexture"
                    Layout.fillWidth: true
                    value: PrintPreview.texture
                    onMoved: PrintPreview.texture = Math.round(value)
                    Accessible.name: "Texture"
                }
            }
            Item { Layout.fillWidth: true }
            ColumnLayout {
                spacing: Theme.spaceSm
                Text { text: "PULLED FROM YOUR ART"; font.pixelSize: Theme.customizeLabelSize; color: Theme.textDim }
                Row {
                    spacing: Theme.spaceXs
                    Repeater {
                        model: PrintPreview.palette
                        Rectangle {
                            required property var modelData
                            width: Theme.spaceLg
                            height: width
                            radius: Theme.radiusSm
                            color: modelData
                        }
                    }
                }
            }
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.spaceLg
            Text {
                text: "HALFTONE"
                font.pixelSize: Theme.customizeLabelSize
                font.letterSpacing: Theme.hairline
                color: Theme.textDim
            }
            QuietAction {
                objectName: "printColorInk"
                text: "color"
                checked: !PrintPreview.monochrome
                onClicked: PrintPreview.monochrome = false
                Accessible.name: "Halftone color ink"
            }
            QuietAction {
                objectName: "printBlackInk"
                text: "black & white"
                checked: PrintPreview.monochrome
                onClicked: PrintPreview.monochrome = true
                Accessible.name: "Halftone black and white ink"
            }
            ColumnLayout {
                Layout.fillWidth: true
                Layout.maximumWidth: PreviewTheme.controlWidth
                Layout.leftMargin: Theme.spaceLg
                spacing: 0
                RowLayout {
                    Layout.fillWidth: true
                    Text { text: "Dot size"; font.pixelSize: Theme.customizeBodySize; color: Theme.textPrimary }
                    Item { Layout.fillWidth: true }
                    Text { text: PrintPreview.dotSize; font.pixelSize: Theme.customizeCaptionSize; color: Theme.textDim }
                }
                InkSlider {
                    objectName: "printDotSize"
                    Layout.fillWidth: true
                    value: PrintPreview.dotSize
                    onMoved: PrintPreview.dotSize = Math.round(value)
                    Accessible.name: "Halftone dot size"
                }
                RowLayout {
                    Layout.fillWidth: true
                    Text { text: "fine"; font.pixelSize: Theme.customizeCaptionSize; color: Theme.textDim }
                    Item { Layout.fillWidth: true }
                    Text { text: "comic"; font.pixelSize: Theme.customizeCaptionSize; color: Theme.textDim }
                }
            }
            Item { Layout.fillWidth: true }
        }

        RowLayout {
            id: comparisons
            objectName: "printComparisons"
            Layout.fillWidth: true
            Layout.fillHeight: true
            // The artwork sets this group's natural height. Extra window
            // height belongs below the comparison, not between its images.
            Layout.maximumHeight: implicitHeight
            spacing: Theme.spaceLg
            Repeater {
                model: 4
                delegate: ColumnLayout {
                    id: variant
                    required property int index
                    readonly property var variantData: PrintPreview.variants[index] || ({})
                    readonly property url imageSource: variantData.url || (index === 0 ? window.sample.sourceUrl || "" : "")
                    readonly property var fallbackTitles: ["Original", "Clean ink", "Halftone", "Worn print"]
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    Layout.preferredWidth: comparisons.width / 4
                    spacing: Theme.spaceSm

                    Text {
                        text: variant.variantData.title || variant.fallbackTitles[variant.index]
                        textFormat: Text.PlainText
                        font.pixelSize: PreviewTheme.variantHeadingSize
                        font.weight: Font.Light
                        color: Theme.textPrimary
                    }
                    Item {
                        id: artFrame
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        readonly property real naturalHeight: width / (window.posterSample
                            ? PreviewTheme.posterAspect : PreviewTheme.albumAspect)
                        Layout.preferredHeight: naturalHeight
                        Layout.maximumHeight: naturalHeight
                        Layout.minimumHeight: Math.min(PreviewTheme.artworkMinimumHeight, naturalHeight)
                        Rectangle {
                            anchors.centerIn: parent
                            width: Math.min(parent.width,
                                parent.height * (window.posterSample ? PreviewTheme.posterAspect : PreviewTheme.albumAspect))
                            height: width / (window.posterSample ? PreviewTheme.posterAspect : PreviewTheme.albumAspect)
                            color: Theme.artworkPlaceholder
                            visible: largeArt.status !== Image.Ready
                        }
                        Image {
                            id: largeArt
                            objectName: "printVariant" + variant.index
                            anchors.fill: parent
                            source: variant.imageSource
                            fillMode: Image.PreserveAspectFit
                            verticalAlignment: Image.AlignTop
                            sourceSize.width: Math.ceil(width * 2)
                            sourceSize.height: Math.ceil(height * 2)
                            asynchronous: true
                            retainWhileLoading: true
                        }
                    }
                    RowLayout {
                        visible: window.artistSample
                        Layout.fillWidth: true
                        Layout.preferredHeight: PreviewTheme.thumbnailsHeight
                        spacing: Theme.spaceMd
                        Item { Layout.fillWidth: true }
                        ColumnLayout {
                            spacing: Theme.spaceXs
                            ArtistThumbnail {
                                Layout.alignment: Qt.AlignHCenter
                                Layout.preferredWidth: PreviewTheme.smallThumbnail
                                Layout.preferredHeight: PreviewTheme.smallThumbnail
                                source: variant.imageSource
                            }
                            Text {
                                Layout.alignment: Qt.AlignHCenter
                                text: "48 px"
                                font.pixelSize: Theme.customizeCaptionSize
                                color: Theme.textDim
                            }
                        }
                        ColumnLayout {
                            spacing: Theme.spaceXs
                            ArtistThumbnail {
                                Layout.alignment: Qt.AlignHCenter
                                Layout.preferredWidth: PreviewTheme.largeThumbnail
                                Layout.preferredHeight: PreviewTheme.largeThumbnail
                                source: variant.imageSource
                            }
                            Text {
                                Layout.alignment: Qt.AlignHCenter
                                text: "80 px"
                                font.pixelSize: Theme.customizeCaptionSize
                                color: Theme.textDim
                            }
                        }
                        Item { Layout.fillWidth: true }
                    }
                    Text {
                        Layout.alignment: Qt.AlignHCenter
                        text: variant.index === 0 ? "your original"
                            : variant.variantData.milliseconds !== undefined
                                ? Math.round(variant.variantData.milliseconds) + " ms" : ""
                        font.pixelSize: Theme.customizeCaptionSize
                        color: Theme.textDim
                    }
                }
            }
        }

        Item {
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.minimumHeight: 0
            Layout.preferredHeight: 0
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.spaceSm
            Rectangle {
                width: Theme.spaceXxs
                height: width
                radius: width / 2
                color: PrintPreview.error ? Theme.warning : PrintPreview.busy ? Theme.orange : Theme.green
            }
            Text {
                Layout.fillWidth: true
                text: PrintPreview.error || (PrintPreview.busy ? "Printing…" : "Drag the sliders. Find your print.")
                textFormat: Text.PlainText
                font.pixelSize: Theme.customizeCaptionSize
                color: PrintPreview.error ? Theme.warning : Theme.textSecondary
                elide: Text.ElideRight
                Accessible.role: PrintPreview.error ? Accessible.AlertMessage : Accessible.StaticText
            }
        }
    }
    }

    FileDialog {
        id: fileDialog
        title: "Your computer · choose artwork"
        nameFilters: ["Artwork (*.png *.jpg *.jpeg *.webp *.bmp)"]
        onAccepted: PrintPreview.addImage(selectedFile)
    }
}
