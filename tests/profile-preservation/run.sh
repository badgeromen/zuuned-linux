#!/usr/bin/env bash
set -euo pipefail
root=$(cd "$(dirname "$0")/../.." && pwd)
source "$root/packaging/containers/smoke-runtime.sh"
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
before="$work/old.db"
after="$work/new.db"
report="$work/report.txt"
sqlite3 "$before" <<'SQL'
CREATE TABLE collection_customizations(kind TEXT NOT NULL,item_key TEXT NOT NULL,identity_json TEXT NOT NULL DEFAULT '{}',art_path TEXT NOT NULL DEFAULT '',PRIMARY KEY(kind,item_key));
INSERT INTO collection_customizations VALUES('album','private-key','{"title":"private-title","nested":{"a":1,"b":[1,2]},"_artworkMatch":{"id":"pinned"},"_artworkChoiceRevision":7,"_artworkDiscovery":{"status":"needsMatch"}}','private-path');
INSERT INTO collection_customizations VALUES('artist','cache-only','{"_artworkDiscovery":{"status":"providerUnavailable"}}','');
SQL
count=0
check() {
    local expected=$1 label=$2 sql=$3 result=pass
    cp "$before" "$after"
    sqlite3 "$after" "$sql"
    local old_hash new_hash
    old_hash=$(sha256sum "$before")
    new_hash=$(sha256sum "$after")
    check_collection_preservation "$before" "$after" "$report" || result=fail
    [[ $result == "$expected" ]] || { cat "$report"; echo "FAIL: $label"; exit 1; }
    [[ $(sha256sum "$before") == "$old_hash" && $(sha256sum "$after") == "$new_hash" ]]
    ! grep -Eq 'private-key|private-title|private-path|pinned' "$report"
    ((++count))
    echo "PASS: $label"
}
check pass unchanged 'SELECT 1;' >/dev/null
check pass 'discovery cache update' "UPDATE collection_customizations SET identity_json=json_set(identity_json,'\$._artworkDiscovery.status','providerUnavailable','\$._artworkDiscovery.recordedAt',123);"
check pass 'new discovery-only album' "INSERT INTO collection_customizations VALUES('album','new','{\"_artworkDiscovery\":{\"status\":\"providerUnavailable\"}}','');"
check pass 'new discovery-only artist' "INSERT INTO collection_customizations VALUES('artist','new','{\"_artworkDiscovery\":{}}','');"
check pass 'JSON formatting and property order' "UPDATE collection_customizations SET identity_json='{ \"_artworkDiscovery\":{}, \"_artworkChoiceRevision\":7, \"_artworkMatch\":{\"id\":\"pinned\"}, \"nested\":{\"b\":[1,2],\"a\":1}, \"title\":\"private-title\" }' WHERE kind='album';"
check fail 'manual title loss' "UPDATE collection_customizations SET identity_json=json_remove(identity_json,'\$.title') WHERE kind='album';"
check fail 'manual provider match change' "UPDATE collection_customizations SET identity_json=json_set(identity_json,'\$._artworkMatch.id','changed') WHERE kind='album';"
check fail 'choice revision loss' "UPDATE collection_customizations SET identity_json=json_remove(identity_json,'\$._artworkChoiceRevision') WHERE kind='album';"
check fail 'art path change' "UPDATE collection_customizations SET art_path='' WHERE kind='album';"
check fail 'original row missing' "DELETE FROM collection_customizations WHERE kind='album';"
check fail 'original cache row missing' "DELETE FROM collection_customizations WHERE kind='artist';"
check fail 'original rowid changed' "UPDATE collection_customizations SET rowid=rowid+100;"
check fail 'original key changed' "UPDATE collection_customizations SET item_key='changed' WHERE kind='album';"
check fail 'array order changed' "UPDATE collection_customizations SET identity_json=json_set(identity_json,'\$.nested.b',json('[2,1]')) WHERE kind='album';"
check fail 'extra new manual identity' "INSERT INTO collection_customizations VALUES('album','new','{\"_artworkDiscovery\":{},\"title\":\"changed\"}','');"
check fail 'new art path' "INSERT INTO collection_customizations VALUES('album','new','{\"_artworkDiscovery\":{}}','new.jpg');"
check fail 'new unsupported collection kind' "INSERT INTO collection_customizations VALUES('mixtape','new','{\"_artworkDiscovery\":{}}','');"
check fail 'new missing discovery' "INSERT INTO collection_customizations VALUES('album','new','{}','');"
check fail 'new null discovery' "INSERT INTO collection_customizations VALUES('album','new','{\"_artworkDiscovery\":null}','');"
check fail 'new array discovery' "INSERT INTO collection_customizations VALUES('album','new','{\"_artworkDiscovery\":[]}','');"
check fail 'invalid JSON' "UPDATE collection_customizations SET identity_json='broken' WHERE kind='album';"
check fail 'identity object lost' "UPDATE collection_customizations SET identity_json='[]' WHERE kind='album';"
check fail 'unexpected schema' 'ALTER TABLE collection_customizations ADD COLUMN future TEXT;'
printf '%s preservation checks passed; input hashes and bounded private-value-free reports checked for every case.\n' "$count"
