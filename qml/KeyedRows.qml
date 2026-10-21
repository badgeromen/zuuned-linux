import QtQuick

// A ListModel kept in step with an array of rows by key: rows are removed,
// inserted and updated in place, so a view keeps its scroll position. A
// ListView or GridView given a new JS array jumps back to the top, and the
// spoken library changes every few seconds while something plays.
// Views read each row as modelData (the one role is "row").
ListModel {
    id: keyed
    // The row's identity; rows sharing a key rebuild the whole list.
    property var keyOf: r => r.key !== undefined ? r.key : r.id
    function canonical(v) {
        if (Array.isArray(v)) return "[" + v.map(canonical).join(",") + "]"
        if (v && typeof v === "object")
            return "{" + Object.keys(v).sort().map(k => JSON.stringify(k) + ":" + canonical(v[k])).join(",") + "}"
        return JSON.stringify(v)
    }
    // fresh: a new question (another show, filter or order), rebuilt from
    // scratch. Otherwise rows that kept their relative order stay put; a
    // reorder rebuilds.
    function apply(rows, fresh) {
        const rebuild = () => { clear(); for (const row of rows) append({row: row}) }
        const keys = rows.map(r => String(keyOf(r)))
        const next = new Set(keys)
        if (fresh || next.size !== keys.length) { rebuild(); return }
        for (let i = count - 1; i >= 0; --i)
            if (!next.has(String(keyOf(get(i).row)))) remove(i)
        const kept = new Set()
        for (let i = 0; i < count; ++i) kept.add(String(keyOf(get(i).row)))
        const order = keys.filter(k => kept.has(k))
        for (let i = 0; i < count; ++i)
            if (String(keyOf(get(i).row)) !== order[i]) { rebuild(); return }
        for (let i = 0; i < rows.length; ++i) {
            if (i >= count || String(keyOf(get(i).row)) !== keys[i]) insert(i, {row: rows[i]})
            else if (canonical(get(i).row) !== canonical(rows[i])) set(i, {row: rows[i]})
        }
    }
}
