#!/usr/bin/env bash
# Build tiny real ELF dependency chains; no candidate/fixture executable runs.
set -euo pipefail
zuuned_elf_repo=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd -P)
zuuned_elf_fixture=$(mktemp -d /tmp/zuuned-elf-inventory-XXXXXXXX)
trap 'rm -rf -- "$zuuned_elf_fixture"' EXIT
zuuned_elf_bundle="$zuuned_elf_fixture/AppDir"
zuuned_elf_lib="$zuuned_elf_bundle/usr/lib"
zuuned_elf_plugin="$zuuned_elf_bundle/usr/plugins/platforms"
mkdir -p "$zuuned_elf_lib" "$zuuned_elf_plugin" "$zuuned_elf_fixture/outside"
cat > "$zuuned_elf_fixture/leaf.c" <<'C'
int fixture_leaf(void) { return 1; }
C
cat > "$zuuned_elf_fixture/middle.c" <<'C'
extern int fixture_leaf(void);
int fixture_middle(void) { return fixture_leaf(); }
C
cat > "$zuuned_elf_fixture/plugin.c" <<'C'
extern int fixture_middle(void);
int fixture_plugin(void) { return fixture_middle(); }
C
cc -shared -fPIC -Wl,-soname,libfixtureleaf.so.1 "$zuuned_elf_fixture/leaf.c" -o "$zuuned_elf_lib/libfixtureleaf.so.1"
cc -shared -fPIC -Wl,-soname,libfixturemiddle.so.1 "$zuuned_elf_fixture/middle.c" \
    -L"$zuuned_elf_lib" -Wl,--no-as-needed -l:libfixtureleaf.so.1 -o "$zuuned_elf_lib/libfixturemiddle.so.1"
cc -shared -fPIC "$zuuned_elf_fixture/plugin.c" -L"$zuuned_elf_lib" \
    -Wl,--no-as-needed -l:libfixturemiddle.so.1 -o "$zuuned_elf_plugin/libfixtureplugin.so"

verify() { bash "$zuuned_elf_repo/packaging/verify-elf-dependencies.sh" "$zuuned_elf_bundle"; }
verify > "$zuuned_elf_fixture/result.log" 2>&1
echo 'PASS complete real ELF/plugin dependency chain is accepted'
mv "$zuuned_elf_lib/libfixtureleaf.so.1" "$zuuned_elf_fixture/outside/"
if LD_LIBRARY_PATH="$zuuned_elf_fixture/outside" verify > "$zuuned_elf_fixture/result.log" 2>&1; then
    echo 'FAIL inventory silently used the host lookup path' >&2; exit 1
fi
grep -q 'libfixturemiddle.so.1 needs libfixtureleaf.so.1' "$zuuned_elf_fixture/result.log"
echo 'PASS missing transitive dependency is rejected even when available outside the bundle'

ln -s "$zuuned_elf_fixture/outside/libfixtureleaf.so.1" "$zuuned_elf_lib/libfixtureleaf.so.1"
if verify > "$zuuned_elf_fixture/result.log" 2>&1; then
    echo 'FAIL external symlink satisfied bundle inventory' >&2; exit 1
fi
grep -q 'not an internal ELF file' "$zuuned_elf_fixture/result.log"
echo 'PASS dependency symlink escaping the AppDir is rejected'
rm "$zuuned_elf_lib/libfixtureleaf.so.1"
printf 'not an ELF library\n' > "$zuuned_elf_lib/libfixtureleaf.so.1"
if verify > "$zuuned_elf_fixture/result.log" 2>&1; then
    echo 'FAIL non-ELF placeholder satisfied bundle inventory' >&2; exit 1
fi
echo 'PASS named placeholder cannot satisfy an ELF dependency'
cp "$zuuned_elf_fixture/outside/libfixtureleaf.so.1" "$zuuned_elf_lib/libfixtureleaf.so.1"

# Simulate a dependency skipped by linuxdeploy's broad blacklist. The crypto
# library must not inherit the desktop/platform exceptions in our contract.
cc -shared -fPIC -Wl,-soname,libgpg-error.so.0 "$zuuned_elf_fixture/leaf.c" -o "$zuuned_elf_fixture/outside/libgpg-error.so.0"
cc -shared -fPIC "$zuuned_elf_fixture/middle.c" -L"$zuuned_elf_fixture/outside" \
    -Wl,--no-as-needed -l:libgpg-error.so.0 -o "$zuuned_elf_plugin/libcryptoplugin.so"
if verify > "$zuuned_elf_fixture/result.log" 2>&1; then
    echo 'FAIL a crypto library was treated as an optional host dependency' >&2; exit 1
fi
grep -q 'usr/plugins/platforms/libcryptoplugin.so needs libgpg-error.so.0' "$zuuned_elf_fixture/result.log"
echo 'PASS dlopened plugin crypto dependency is required inside the bundle'
echo '5 real ELF inventory checks passed'
