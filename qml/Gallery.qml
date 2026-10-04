pragma Singleton
import QtQuick

// The capped-gallery math (grid study, approved 2026-09-06): width buys
// bigger art, never more columns. One source of truth for every grid —
// library and device alike.
QtObject {
    // Air between cells — the study's numbers
    readonly property int gutter: 32
    readonly property int rowGap: 46

    // Small-screen density (headroom, ux/7): Main binds these off
    // spaceScarce — scarce windows scale every ideal cell down and
    // lift the column caps so a small screen shows MORE art, not
    // less (the study's "width buys bigger art" inverts there, and
    // only there). At 1.0 / 0 every plan is byte-identical to the
    // study. One knob here reflows every grid AND the canonical
    // spine together, so the tabs stay aligned across sections.
    property real density: 1.0
    property int extraCols: 0

    // Column plan for an available width: columns cap at maxCols and
    // the cell grows within ±~20% of its ideal, so ultrawide gets a
    // centered block of big art instead of sixteen cramped columns.
    // `dens`/`xtra` opt a surface OUT of the small-screen
    // densification: the video shelves cap how many posters appear at
    // once ON PURPOSE (Hick's law — a wall of covers overwhelms), so
    // they keep their poster size and column cap at every window size.
    // Omit both to follow the global knobs (music, photos).
    //
    // THE BLOCK FILLS ITS SPAN (2026-09-12). Taking the most columns
    // that fit AT the ideal size, then clamping the cell, left a
    // remainder pooling on one side: at avail 565 / ideal 190 that was
    // 2 columns wanting 266px cells, clamped to 230, stranding 73px.
    // That remainder WAS the oversized gutter, and the orphaned space
    // between the A–Z rail and the scrollbar. So pick the column count
    // whose natural cell lands CLOSEST to the ideal instead — that
    // count consumes the span exactly. Error is measured on the log of
    // the ratio so "40% too big" and "40% too small" weigh the same;
    // the ±20% clamp survives only as a last resort for spans no count
    // can serve.
    function plan(avail, ideal, maxCols, dens, xtra) {
        const g = gutter
        const i = Math.max(1, Math.round(
            ideal * (dens === undefined ? density : dens)))
        const cap = Math.max(1, maxCols + (xtra === undefined ? extraCols : xtra))
        let cols = 1
        let bestErr = Infinity
        for (let c = 1; c <= cap; c++) {
            const nat = (avail - (c - 1) * g) / c
            // Past this the art is too small to read — and every
            // further column only shrinks it
            if (c > 1 && nat < i * 0.55)
                break
            const err = Math.abs(Math.log(nat / i))
            if (err < bestErr - 1e-9) {
                bestErr = err
                cols = c
            }
        }
        const cell = Math.min(Math.round(i * 1.21),
                     Math.max(Math.round(i * 0.84),
                              Math.floor((avail - (cols - 1) * g) / cols)))
        return { cols: cols, cell: cell,
                 width: cols * cell + (cols - 1) * g }
    }

    // ── The app-wide canonical spine ──
    // ONE formula for where content begins on ANY page (the canonical
    // frame: 210-ideal cells, 7 columns, rail lane 34, inset 32) so the
    // tabs sit at the same x in every section — music, videos, photos,
    // playlists, and Settings alike. Pass the page root's width.
    readonly property int railLane: 34
    readonly property int edgeInset: 32
    function canonicalPlan(w) {
        return plan(Math.max(220, w - railLane - edgeInset * 2), 210, 7)
    }
    function spine(w) {
        return edgeInset + Math.max(0,
            (w - railLane - edgeInset * 2 - canonicalPlan(w).width) / 2)
    }
    function frameW(w) { return canonicalPlan(w).width }

    function letterOf(v) {
        const c = (v || "").charAt(0).toLowerCase()
        return /[a-z]/.test(c) ? c : "#"
    }

    // Split sorted rows into letter sections for the in-grid headers.
    // nameOf pulls the sort name off a row.
    function sectioned(rows, nameOf) {
        const out = []
        let cur = null
        for (const r of rows) {
            const l = letterOf(nameOf(r))
            if (!cur || cur.letter !== l) {
                cur = { letter: l, items: [] }
                out.push(cur)
            }
            cur.items.push(r)
        }
        return out
    }
}
