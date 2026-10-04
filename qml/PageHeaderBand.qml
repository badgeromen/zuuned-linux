import QtQuick
import QtQuick.Layouts

// The app-chrome tab band (2026-09-06): ONE fixed height on every
// page, so pivots and right-side clusters sit at the exact same Y no
// matter which section is open — only the content below changes.
// Before this, each page composed its own header row and the row's
// height (set by its tallest neighbor: search stacks, count columns,
// titles) moved the tabs up and down between sections — unrefined.
//
// Pages drop a PivotBar + spacer + right cluster in; keep children on
// the default vertical center.
RowLayout {
    Layout.fillWidth: true
    // A band NEVER dictates its column's width. Without this, the
    // row's implicit width (spine + pivots + counts + search + inset)
    // became the page ColumnLayout's implicit width; when that
    // exceeded the real column — any narrow window with the device
    // panel open — the layout sized its fillWidth children to the
    // BAND's width instead, so the content below (poster grids, their
    // scrollbar and A–Z rail) was laid out against a phantom width and
    // clipped at the panel edge (Orson 2026-09-12). fillWidth still
    // gives the band the column's real width; only its implicit
    // contribution is silenced.
    Layout.preferredWidth: 0
    Layout.preferredHeight: 52
    Layout.minimumHeight: 52
    Layout.maximumHeight: 52
    spacing: Theme.spaceLg
}
