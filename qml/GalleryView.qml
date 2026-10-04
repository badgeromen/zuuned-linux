import QtQuick
import QtQuick.Controls.Basic
import Zuuned

// The capped-and-centered gallery scroller (grid study, 2026-09-06):
// columns cap per shape, cells grow with width, the block centers, and
// the A–Z rail rides the block's right shoulder.
//
// THE RAIL FILTERS — tap a letter, see only that letter; tap it again,
// see everything. The clean unbroken grid is the design (Orson,
// 2026-09-06). Marker-letter section BREAKS are an opt-in
// (Settings → Appearance → letter breaks); with breaks on, the rail
// jumps to sections instead.
//
// Pages hand in SORTED rows plus a delegate Component whose root
// declares `required property var modelData`; cell width comes from
// this view's `plan.cell` (reference it by the gallery's id).
Item {
    id: root

    property var rows: []
    // Sort/section name off a row
    property var nameOf: (r) => (r.title ?? r.name ?? "")
    property int idealCell: 190
    property int maxCols: 8
    // Delegate height for a cell width (art + caption)
    property var cellHeightFor: (w) => Math.round(w * 1.5) + 50
    // Rail + letter logic; off for shapes with no alphabet (photos)
    property bool lettered: true
    property Component cardDelegate
    // Rail filter ("" = everything) — hosts clear it on tab change
    property string letterFilter: ""

    readonly property bool sectionsOn: lettered && AppSettings.letterBreaks

    readonly property int railLane: lettered ? 34 : 0
    // Minimum side inset — the same floor the host toolbars use, so
    // pivots and grid NEVER disagree about the left edge
    readonly property int edgeInset: Theme.spaceXxxl
    // Optional shared page spine: when ≥ 0 the block LEFT-ALIGNS here
    // instead of centering itself. Pivot pages pin every view to ONE
    // x so the tabs never jump between tabs (per-tab centering made
    // every pivot click a horizontal teleport — jarring, Orson
    // 2026-09-06). Pass a root-width-derived value only (loop rule).
    property real anchorX: -1

    // Available width for the column plan. Centered mode assumes a
    // symmetric inset on both sides. LEFT-ANCHORED mode must instead
    // measure from the anchor to the right safe edge (rail lane +
    // inset) — the anchor is a canonical spine derived from a
    // DIFFERENT plan (210/7) than this view (e.g. 190/8, and denser
    // on scarce windows), so the symmetric-inset assumption let the
    // block overrun the frame and clip under the device panel on
    // narrow windows (Orson 2026-09-10). Bounding cols by the real
    // room right of the anchor makes overflow impossible.
    readonly property real planAvail: anchorX >= 0
        ? Math.max(220, width - railLane - edgeInset - anchorX)
        : Math.max(220, width - railLane - edgeInset * 2)
    // Small-window behaviour, split in two so a shelf can take the
    // half it wants:
    //   densify      — cells SCALE DOWN with the global density, so a
    //                  narrow column shows its art whole instead of
    //                  clipping it (Orson 2026-09-12: "just scale the
    //                  images down when we get to a certain size").
    //   liftColumnCap — also raises the column cap. The video shelves
    //                  set this FALSE: a smaller poster must never
    //                  become an excuse for MORE of them at once
    //                  (Hick's law — that restraint is the design).
    property bool densify: true
    property bool liftColumnCap: true
    readonly property var plan: Gallery.plan(planAvail, idealCell, maxCols,
        densify ? undefined : 1.0,
        liftColumnCap ? undefined : 0)

    // The block's left edge (internal use)
    readonly property real contentX: anchorX >= 0 ? anchorX
        : edgeInset + Math.max(0,
              (width - railLane - edgeInset * 2 - plan.width) / 2)

    // The spine for HOST toolbars — same math, but from a width the
    // host's own layout does NOT assign (pass the page root's width).
    // Binding a toolbar margin to contentX loops the layout: contentX
    // reads this view's width, which that very layout sets during
    // polish → margin change → re-polish → the app never settles.
    function planFor(w) {
        return Gallery.plan(Math.max(220, w - railLane - edgeInset * 2),
                            idealCell, maxCols)
    }
    function spineFor(w) {
        const p = planFor(w)
        return edgeInset + Math.max(0,
            (w - railLane - edgeInset * 2 - p.width) / 2)
    }

    readonly property var shownRows:
        (!lettered || sectionsOn || letterFilter === "")
            ? rows
            : rows.filter(r => Gallery.letterOf(nameOf(r)) === letterFilter)
    readonly property var sections: sectionsOn
        ? Gallery.sectioned(shownRows, nameOf)
        : [{ letter: "", items: shownRows }]
    // Every letter present in the FULL data — the rail's live set stays
    // complete while a filter narrows the view
    readonly property var lettersInRows: {
        const out = {}
        for (const r of rows)
            out[Gallery.letterOf(nameOf(r))] = true
        return Object.keys(out)
    }

    // ── Default path: a REAL GridView, virtualized. Only visible
    // cards exist, only visible cards request art/thumbnails — a
    // 250-poster wall costs a screenful, not the whole library.
    // (Repeater-per-section instantiated everything at once: the lag.)
    //
    // The view gets an EXPLICIT x + width of exactly cols × cellWidth:
    // GridView margins do NOT constrain where rows wrap (cells flow
    // across the full view width), so sizing the view itself is the
    // only way to pin the column count and center the block.
    GridView {
        id: flatGrid
        visible: !root.sectionsOn
        x: root.contentX
        width: root.plan.cols * cellWidth
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        ScrollBar.vertical: ZuneScrollBar {
            // Off the block, at the gallery's own right edge — inside
            // the view it would sit on the rail. Reparented bars don't
            // inherit the view's visibility — bind it.
            parent: root
            visible: flatGrid.visible && size < 1.0
            anchors.top: parent.top
            anchors.bottom: parent.bottom
            anchors.right: parent.right
        }
        cellWidth: root.plan.cell + Gallery.gutter
        cellHeight: root.cellHeightFor(root.plan.cell) + Gallery.rowGap
        topMargin: Theme.spaceMd
        bottomMargin: Theme.spaceXl
        model: root.sectionsOn ? [] : root.shownRows
        delegate: root.cardDelegate
    }

    // ── Opt-in letter-breaks path (AppSettings.letterBreaks):
    // sections as ListView delegates — whole sections instantiate,
    // acceptable at section granularity, never the default.
    ListView {
        id: list
        visible: root.sectionsOn
        anchors.fill: parent
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        ScrollBar.vertical: ZuneScrollBar {}
        model: root.sectionsOn ? root.sections : []
        spacing: Gallery.rowGap
        // Sections are whole delegates; keep a screenful prepared so
        // rail jumps land on laid-out content.
        cacheBuffer: 600

        delegate: Column {
            required property var modelData
            width: list.width
            spacing: Theme.spaceLg

            // Marker letter + hairline — ONLY in opt-in breaks mode
            Item {
                visible: root.sectionsOn
                width: parent.width
                height: visible ? 44 : 0
                Text {
                    x: root.contentX
                    anchors.bottom: parent.bottom
                    text: modelData.letter === "#" ? "#"
                          : modelData.letter.toUpperCase()
                    font.family: Theme.displayFamily
                    font.pixelSize: 30
                    color: Theme.activePink
                    rotation: -2
                }
                Rectangle {
                    x: root.contentX + 46
                    width: root.plan.width - 46
                    anchors.bottom: parent.bottom
                    anchors.bottomMargin: 8
                    height: 1
                    color: Theme.separator
                }
            }

            Grid {
                x: root.contentX
                columns: root.plan.cols
                columnSpacing: Gallery.gutter
                rowSpacing: Gallery.rowGap
                Repeater {
                    model: modelData.items
                    delegate: root.cardDelegate
                }
            }
        }
    }

    AlphabetRail {
        x: Math.min(root.contentX + root.plan.width + 12,
                    root.width - width - 2)
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        anchors.topMargin: Theme.spaceXl
        anchors.bottomMargin: Theme.spaceXl
        z: 5
        visible: root.lettered && root.lettersInRows.length > 1
        available: root.lettersInRows
        activeLetter: root.sectionsOn ? "" : root.letterFilter
        onJump: letter => {
            if (root.sectionsOn) {
                const i = root.sections.findIndex(s => s.letter === letter)
                if (i >= 0)
                    list.positionViewAtIndex(i, ListView.Beginning)
            } else {
                // Filter mode (the default): the rail already toggles —
                // clicking the active letter hands back ""
                root.letterFilter = letter
            }
        }
    }
}
