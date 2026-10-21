#!/usr/bin/env bash
# T3.5: the error convention. Three layers, all offline:
#  1. source: each public definition starts with zune_clear_error() exactly
#     when the generated return-convention table in docs/API_REFERENCE.md says
#     the call clears the error, and status functions return ZUNE_OK/ZUNE_BLOWN
#     rather than bare 0/-1;
#  2. behaviour: tests/error-convention.c calls every public function under
#     ASan/UBSan after planting an earlier error;
#  3. deprecation: the five superseded functions warn and name a replacement.
set -euo pipefail
cd "$(dirname "$0")/.."
fixture_dir=$(mktemp -d /tmp/libzune-error-convention.XXXXXX)
trap 'rm -rf "$fixture_dir"' EXIT
fail=0

# ---- 1. Source ----
awk -F'|' '/^## Return conventions/ { on = 1; next } /^## / { on = 0 } on && /^\| `zune_/ {
    name = $2; gsub(/[ `]/, "", name)
    kind = $4; sub(/^ +/, "", kind); sub(/[:;].*$/, "", kind); sub(/ +$/, "", kind); gsub(/ /, "_", kind)
    clears = $5; gsub(/ /, "", clears)
    print name, clears, kind
}' docs/API_REFERENCE.md > "$fixture_dir/table"
if [ "$(wc -l < "$fixture_dir/table")" -lt 100 ]; then
    echo "FAIL: return-convention table missing from docs/API_REFERENCE.md"; exit 1
fi

# Report each public definition: name, its first statement, whether the body
# calls zune_clear_error, and how many bare 0/-1 status returns it has.
awk -v table="$fixture_dir/table" '
function strip(s,   out, i, c, n) {
    out = ""; n = length(s)
    for (i = 1; i <= n; i++) {
        c = substr(s, i, 1)
        if (incomment) { if (substr(s, i, 2) == "*/") { incomment = 0; i++ } continue }
        if (substr(s, i, 2) == "/*") { incomment = 1; i++; continue }
        if (substr(s, i, 2) == "//") break
        if (c == "\"" || c == "\x27") {
            q = c; out = out c
            for (i++; i <= n; i++) { c = substr(s, i, 1); if (c == "\\") { i++; continue } if (c == q) break }
            out = out q; continue
        }
        out = out c
    }
    return out
}
function count(s, ch,   t) { t = s; return gsub(ch, "", t) }
BEGIN {
    while ((getline line < table) > 0) { split(line, f, " "); clears[f[1]] = f[2]; kind[f[1]] = f[3] }
}
FNR == 1 { state = 0; incomment = 0 }
{
    code = strip($0)
    if (state == 0) {
        if (code ~ /^[A-Za-z]/ && code !~ /^(static|typedef|extern|return|struct [A-Za-z_]+ \{)/ &&
            match(code, /zune_[a-z0-9_]+[ \t]*\(/)) {
            n = substr(code, RSTART, RLENGTH); sub(/[ \t]*\($/, "", n)
            if (n in clears) { name = n; state = 1 }
        }
    }
    if (state == 1) {
        if (index(code, "{")) {
            state = 2; depth = 0; first = ""; calls_clear = 0; literal = 0
            rest = substr(code, index(code, "{") + 1)
            depth = 1 + count(rest, "{") - count(rest, "}")
            if (rest ~ /[^ \t]/) first = rest
            if (depth <= 0) state = 3
            else next
        } else if (index(code, ";")) { state = 0; next }
        else next
    }
    if (state == 2) {
        if (first == "" && code ~ /[^ \t]/) first = code
        if (code ~ /zune_clear_error[ \t]*\(/) calls_clear = 1
        if (kind[name] ~ /^(status|count-or-error)$/) {
            if (code ~ /return[ \t]+\(?-1\)?[ \t]*;/) literal++
            if (kind[name] == "status" && code ~ /return[ \t]+0[ \t]*;/) literal++
            if (code ~ /\?[ \t]*0[ \t]*:[ \t]*-1/) literal++
        }
        depth += count(code, "{") - count(code, "}")
        if (depth <= 0) state = 3
    }
    if (state == 3) {
        gsub(/^[ \t]+|[ \t]+$/, "", first)
        if (first ~ /^zune_clear_error\(\);/) calls_clear = 1
        print name, (first ~ /^zune_clear_error\(\);/) ? "first" : "notfirst", calls_clear, literal
        state = 0
    }
}' $(ls src/*.c) > "$fixture_dir/defs"

while read -r name clears kind; do
    if ! grep -q "^$name " "$fixture_dir/defs"; then
        echo "FAIL: no definition found for $name"; fail=1; continue
    fi
    while read -r _ position calls literal; do
        if [ "$clears" = yes ] && [ "$position" != first ]; then
            echo "FAIL: $name can fail but does not clear the error first"; fail=1
        fi
        if [ "$clears" = no ] && [ "$calls" = 1 ]; then
            echo "FAIL: $name cannot fail but clears the error"; fail=1
        fi
        if [ "$literal" != 0 ]; then
            echo "FAIL: $name returns a bare 0/-1 status ($literal); use ZUNE_OK/ZUNE_BLOWN"; fail=1
        fi
    done < <(grep "^$name " "$fixture_dir/defs")
done < "$fixture_dir/table"
publics=$(wc -l < "$fixture_dir/table")
echo "source: $publics public functions, $(wc -l < "$fixture_dir/defs") definitions checked"

# ---- 2. Behaviour ----
srcs=$(ls src/*.c | grep -v driverkit_usb.c)
${CC:-cc} -std=c99 -D_GNU_SOURCE -O1 -g -Wall -Wextra -Wno-unused-parameter \
    -fsanitize=address,undefined -fno-sanitize-recover=undefined -fno-omit-frame-pointer -Iinclude -Isrc -DUSE_LIBAV \
    $(pkg-config --cflags libusb-1.0 libavformat libavutil) \
    tests/error-convention.c $srcs -lavcodec $(pkg-config --libs libusb-1.0 libavformat libavutil) \
    -lgcrypt -lm -pthread -o "$fixture_dir/error-convention" 2> "$fixture_dir/build.log" || {
    cat "$fixture_dir/build.log"; exit 1; }
if grep -v '^src/mtpz.c' "$fixture_dir/build.log" | grep -q 'warning:'; then
    grep 'warning:' "$fixture_dir/build.log" | grep -v '^src/mtpz.c'
    echo "FAIL: the behaviour test builds with warnings"; fail=1
fi
"$fixture_dir/error-convention" || fail=1

# ---- 3. Deprecation ----
cat > "$fixture_dir/legacy.c" <<'EOF'
#include "zune.h"
int main(void) {
    ZuneLibrary *lib = 0;
    uint32_t id = 0;
    (void)zune_smuggle_movie(0, "a.wmv", "a", 0, 0, 0, &id);
    (void)zune_smuggle_episode(0, "a.wmv", "a", "s", 1, 1, 0, 0, 0, &id);
    (void)zune_smuggle_clip(0, "a.wmv", "a", 0, 0, &id);
    (void)zune_smuggle_other(0, "a.wmv", "a", 0, 0, 0, &id);
    (void)zune_infiltrate_legacy(0, &lib);
    return 0;
}
EOF
cat > "$fixture_dir/current.c" <<'EOF'
#include "zune.h"
int main(void) {
    ZuneDBLibrary *lib = 0;
    uint32_t id = 0;
    int rc = zune_smuggle_video_named(0, "a.wmv", "a.wmv", "a", 0, 0, 0, 0, 0, 0, 0, &id);
    if (zune_infiltrate(0, &lib) == ZUNE_OK) zune_free_scan(lib);
    return rc == ZUNE_OK ? 0 : 1;
}
EOF
for lang in c c++; do
    if [ "$lang" = c ]; then compiler=${CC:-cc}; std=-std=c99
    else compiler=${CXX:-c++}; std=-std=c++17
        command -v "$compiler" >/dev/null || { echo "skip: no C++ compiler"; continue; }
    fi
    if $compiler $std -x $lang -Iinclude -Wall -Wextra -Werror=deprecated-declarations -fsyntax-only \
        "$fixture_dir/legacy.c" 2> "$fixture_dir/legacy-$lang.log"; then
        echo "FAIL: $lang: calling the deprecated functions compiles without a warning"; fail=1
    fi
    named=$(grep -c 'use zune_smuggle_video_named' "$fixture_dir/legacy-$lang.log" || true)
    scan=$(grep -c 'use zune_infiltrate and zune_free_scan' "$fixture_dir/legacy-$lang.log" || true)
    if [ "$named" -lt 4 ] || [ "$scan" -lt 1 ]; then
        cat "$fixture_dir/legacy-$lang.log"
        echo "FAIL: $lang: deprecation warnings do not name the replacements ($named video, $scan scan)"; fail=1
    fi
    if ! $compiler $std -x $lang -Iinclude -Wall -Wextra -Wpedantic -Werror -fsyntax-only \
        "$fixture_dir/current.c" 2> "$fixture_dir/current-$lang.log"; then
        cat "$fixture_dir/current-$lang.log"
        echo "FAIL: $lang: the replacement functions do not compile cleanly"; fail=1
    fi
done
echo "deprecation: 5 superseded functions warn and name their replacement (C and C++)"

if [ "$fail" -ne 0 ]; then echo "Error convention: FAILED"; exit 1; fi
echo "Error convention: all layers pass"
