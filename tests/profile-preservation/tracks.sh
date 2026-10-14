#!/usr/bin/env bash
set -euo pipefail
root=$(cd "$(dirname "$0")/../.." && pwd)
source "$root/packaging/containers/smoke-runtime.sh"
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
media="$work/private song.flac"
printf x > "$media"
mtime=$(date -d "$(stat -c %y "$media")" +%s%N)
ctime=$(date -d "$(stat -c %z "$media")" +%s%N)
sqlite3 "$work/old.db" "CREATE TABLE tracks(id INTEGER PRIMARY KEY,filepath TEXT,filesize INTEGER,probe_mtime_ns INTEGER,probe_ctime_ns INTEGER,audio_fingerprint TEXT); INSERT INTO tracks VALUES(1,'$media',1,$mtime,$ctime,'audio-v1:old');"
count=0
check() {
    local expected=$1 label=$2 before=$3 after=$4 result=pass hashes
    cp "$work/old.db" "$work/previous.db"
    sqlite3 "$work/previous.db" "$before"
    cp "$work/previous.db" "$work/current.db"
    sqlite3 "$work/current.db" "$after"
    hashes=$(sha256sum "$work/previous.db" "$work/current.db")
    check_track_fingerprint_preservation "$work/previous.db" "$work/current.db" "$work/report" || result=fail
    [[ $result == "$expected" ]] || { cat "$work/report"; echo "FAIL: $label"; exit 1; }
    [[ $hashes == "$(sha256sum "$work/previous.db" "$work/current.db")" ]]
    ! grep -q 'private song\|audio-v1:old' "$work/report"
    ((++count)); echo "PASS: $label"
}
check pass unchanged 'SELECT 1;' 'SELECT 1;'
check fail 'unchanged source invalidated' 'SELECT 1;' "UPDATE tracks SET audio_fingerprint='';"
check pass 'missing source invalidated' "UPDATE tracks SET filepath='$work/missing';" "UPDATE tracks SET audio_fingerprint='';"
check pass 'directory source invalidated' "UPDATE tracks SET filepath='$work';" "UPDATE tracks SET audio_fingerprint='';"
check pass 'changed size invalidated' 'UPDATE tracks SET filesize=2;' "UPDATE tracks SET audio_fingerprint='';"
check pass 'changed nanosecond mtime invalidated' 'UPDATE tracks SET probe_mtime_ns=probe_mtime_ns+1;' "UPDATE tracks SET audio_fingerprint='';"
check pass 'changed nanosecond ctime invalidated' 'UPDATE tracks SET probe_ctime_ns=probe_ctime_ns+1;' "UPDATE tracks SET audio_fingerprint='';"
check fail 'replacement hash prohibited' 'SELECT 1;' "UPDATE tracks SET audio_fingerprint='audio-v1:new';"
check fail 'new hash prohibited' "UPDATE tracks SET audio_fingerprint='';" "UPDATE tracks SET audio_fingerprint='audio-v1:new';"
ln -s "$media" "$work/link"
check fail 'unchanged symlink target preserved' "UPDATE tracks SET filepath='$work/link';" "UPDATE tracks SET audio_fingerprint='';"
check fail 'NUL path rejected' "UPDATE tracks SET filepath=filepath||char(0)||'suffix';" "UPDATE tracks SET audio_fingerprint='';"
printf '%s track fingerprint checks passed; input hashes and private-free reports checked.\n' "$count"
# Exercise the actual upgrade-table block too, ensuring the fingerprint
# exception cannot hide metadata, identity or membership changes.
block=$(sed -n '/^    while IFS= read -r table; do$/,/^    done < <(sqlite3 -readonly .*sqlite_schema/p' "$root/packaging/containers/smoke-runtime.sh")
[[ -n $block ]]
mkdir "$work/input"
for mutation in "UPDATE tracks SET filepath='changed',audio_fingerprint='';" 'UPDATE tracks SET id=99;' 'DELETE FROM tracks;' 'UPDATE tracks SET probe_mtime_ns=0;'; do
    cp "$work/old.db" "$work/input/library.db"
    cp "$work/old.db" "$work/current.db"
    sqlite3 "$work/current.db" "$mutation"
    if (
        fail() { exit 17; }
        upgrade_input="$work/input"
        upgrade_input_sql="$upgrade_input"
        upgrade_db="$work/current.db"
        evidence="$work"
        eval "$block"
    ); then
        echo 'FAIL: actual upgrade block accepted non-fingerprint mutation'; exit 1
    fi
    echo 'PASS: actual upgrade block rejected non-fingerprint mutation'
done
# A v5 profile has no prior audio-copy evidence or nanosecond probe columns.
# New columns may be populated while every original column stays protected.
rm "$work/input/library.db" "$work/current.db"
sqlite3 "$work/input/library.db" "PRAGMA user_version=5; CREATE TABLE tracks(id INTEGER PRIMARY KEY,filepath TEXT,title TEXT); INSERT INTO tracks VALUES(1,'old-file','original-title');"
cp "$work/input/library.db" "$work/current.db"
sqlite3 "$work/current.db" "ALTER TABLE tracks ADD COLUMN audio_fingerprint TEXT DEFAULT ''; ALTER TABLE tracks ADD COLUMN probe_mtime_ns INTEGER DEFAULT 0; ALTER TABLE tracks ADD COLUMN probe_ctime_ns INTEGER DEFAULT 0; UPDATE tracks SET audio_fingerprint='new-evidence'; PRAGMA user_version=9;"
(
    fail() { echo 'FAIL: v5 comparison unexpectedly failed'; exit 1; }
    upgrade_input="$work/input"
    upgrade_input_sql="$upgrade_input"
    upgrade_db="$work/current.db"
    evidence="$work"
    eval "$block"
)
grep -q 'No prior audio fingerprint evidence' "$work/upgrade-track-fingerprints.txt"
echo 'PASS: old-schema tracks preserve original columns without claiming prior fingerprint evidence'
sqlite3 "$work/current.db" "UPDATE tracks SET title='changed';"
if (
    fail() { exit 17; }
    upgrade_input="$work/input"
    upgrade_input_sql="$upgrade_input"
    upgrade_db="$work/current.db"
    evidence="$work"
    eval "$block"
); then
    echo 'FAIL: v5 original title change accepted'; exit 1
fi
grep -q 'title_changed_projections=2' "$work/upgrade-tracks-differences.txt"
echo 'PASS: old-schema original metadata change still fails with bounded diagnostics'
