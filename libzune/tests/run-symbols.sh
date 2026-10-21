#!/usr/bin/env bash
# The shared object exports exactly the zune_* functions declared in zune.h,
# carries SONAME libzune.so.<major>, and ships matching pkg-config metadata.
set -euo pipefail
cd "$(dirname "$0")/.."
make -s all >/dev/null
fixture_dir=$(mktemp -d "${TMPDIR:-/tmp}/libzune-symbols.XXXXXX")
trap 'rm -rf "$fixture_dir"' EXIT
part() { sed -n "s/^#define LIBZUNE_VERSION_$1 \([0-9][0-9]*\)$/\1/p" include/zune.h; }
version="$(part MAJOR).$(part MINOR).$(part PATCH)"

# Declared: every ZUNE_API declaration (name on the declaration's first line).
sed -n 's/^ZUNE_API[^(]*[ *]\(zune_[A-Za-z0-9_]*\)[ \t]*(.*/\1/p' include/zune.h | sort -u > "$fixture_dir/declared"
# Exported: every defined dynamic symbol, minus linker bookkeeping.
nm -D --defined-only "libzune.so.$version" | awk '{print $3}' \
    | grep -vxE '_init|_fini|_edata|_end|__bss_start' | sort -u > "$fixture_dir/exported"
fail=0
if comm -13 "$fixture_dir/declared" "$fixture_dir/exported" | grep .; then
    echo "FAIL: exported but not declared in zune.h (above)"; fail=1
fi
if comm -23 "$fixture_dir/declared" "$fixture_dir/exported" | grep .; then
    echo "FAIL: declared in zune.h but not exported (above)"; fail=1
fi
if nm -D --defined-only "libzune.so.$version" | awk '$2 != "T" {print}' | grep -vE ' (_init|_fini|_edata|_end|__bss_start)$' | grep .; then
    echo "FAIL: exported non-function symbols (above)"; fail=1
fi
[ "$fail" = 0 ] && echo "PASS: $(wc -l < "$fixture_dir/exported") exported functions, all declared zune_* names"

soname=$(readelf -d "libzune.so.$version" | sed -n 's/.*Library soname: \[\(.*\)\]/\1/p')
[ "$soname" = "libzune.so.$(part MAJOR)" ] || { echo "FAIL: SONAME is '$soname'"; fail=1; }
[ "$(readlink libzune.so.$(part MAJOR))" = "libzune.so.$version" ] || { echo "FAIL: libzune.so.$(part MAJOR) link"; fail=1; }
[ "$(readlink libzune.so)" = "libzune.so.$(part MAJOR)" ] || { echo "FAIL: libzune.so link"; fail=1; }
[ "$fail" = 0 ] && echo "PASS: libzune.so.$version, SONAME $soname, development links in place"

# The libraries must follow the objects on the link line: under --as-needed
# (the Debian/Ubuntu default) a library named first is dropped, and the .so
# then fails to load. The Makefile forces --as-needed and -z defs on Linux.
needed=$(readelf -d "libzune.so.$version" | sed -n 's/.*Shared library: \[\(.*\)\]/\1/p')
for lib in libgcrypt libusb-1.0 libavformat libavutil libavcodec; do
    printf '%s\n' "$needed" | grep -q "^$lib\.so" || { echo "FAIL: libzune.so does not record $lib (NEEDED)"; fail=1; }
done
[ "$fail" = 0 ] && echo "PASS: libzune.so records gcrypt, libusb and libav as NEEDED libraries"

[ "$(PKG_CONFIG_PATH=. pkg-config --modversion libzune)" = "$version" ] || { echo "FAIL: pkg-config version"; fail=1; }
private=$(PKG_CONFIG_PATH=. pkg-config --libs --static libzune)
for lib in -lzune -lgcrypt -lusb-1.0 -lavformat -lavutil; do
    case " $private " in *" $lib "*) ;; *) echo "FAIL: pkg-config --static lacks $lib"; fail=1;; esac
done
[ "$fail" = 0 ] && echo "PASS: libzune.pc reports $version with gcrypt, libusb and libav as private libraries"

# `make install PREFIX=...` writes the pc file for that prefix, even after a
# plain `make` built libzune.pc for the default one (dry run: nothing copied).
install_cmds=$(make -s -n install PREFIX=/opt/libzune-pc-check DESTDIR="$fixture_dir/root")
printf '%s\n' "$install_cmds" | grep -q "'prefix=/opt/libzune-pc-check'.*> $fixture_dir/root/opt/libzune-pc-check/lib/pkgconfig/libzune.pc" \
    || { echo "FAIL: make install does not write libzune.pc for its PREFIX"; fail=1; }
[ "$fail" = 0 ] && echo "PASS: make install writes libzune.pc for the install prefix"

${CC:-cc} -std=c99 -D_GNU_SOURCE -Wall -Wextra -Werror -Iinclude tests/symbols.c -ldl -o "$fixture_dir/symbols"
"$fixture_dir/symbols" "./libzune.so.$version" || fail=1
exit $fail
