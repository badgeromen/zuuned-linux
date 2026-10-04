#!/usr/bin/env bash
# Build a traceable x86_64 tester AppImage. The build host defines the ABI
# floor; packaging alone does not make an Arch binary compatible with an LTS.
set -euo pipefail
cd "$(dirname "$0")/.."
export PATH="$PATH:/usr/sbin:/sbin" # ldconfig's read-only cache query on Debian.

[[ $(uname -m) == x86_64 ]] || { echo "This recipe targets x86_64." >&2; exit 1; }
for tool in cmake ninja curl sha256sum readelf ffmpeg ffprobe pkg-config ldconfig; do
    command -v "$tool" >/dev/null || { echo "Missing build tool: $tool" >&2; exit 1; }
done
zuuned_git_root=$(git rev-parse --show-toplevel 2>/dev/null || true)
if [[ $zuuned_git_root == "$PWD" ]]; then
    if [[ -n $(git status --porcelain --untracked-files=normal --ignore-submodules=none) \
          && ${ZUUNED_ALLOW_DIRTY:-0} != 1 ]]; then
        echo "Commit the reviewed candidate before packaging; ZUUNED_ALLOW_DIRTY=1 permits a labeled development artifact." >&2
        exit 1
    fi
    export SOURCE_DATE_EPOCH="${SOURCE_DATE_EPOCH:-$(git log -1 --format=%ct)}"
else
    : "${ZUUNED_SOURCE_REVISION:?Archive builds need the full source revision}"
    : "${ZUUNED_LIBZUNE_REVISION:?Archive builds need the full libzune revision}"
    : "${SOURCE_DATE_EPOCH:?Archive builds need SOURCE_DATE_EPOCH}"
    if [[ ${ZUUNED_ALLOW_DIRTY:-0} != 1 \
          && ( ${ZUUNED_SOURCE_DIRTY:-unknown} != false || ${ZUUNED_LIBZUNE_DIRTY:-unknown} != false ) ]]; then
        echo "Archive builds require explicit clean provenance or ZUUNED_ALLOW_DIRTY=1." >&2
        exit 1
    fi
fi
[[ $SOURCE_DATE_EPOCH =~ ^[0-9]+$ ]] || { echo "Invalid SOURCE_DATE_EPOCH" >&2; exit 1; }

zuuned_build="$PWD/build-appimage"
zuuned_appdir="$zuuned_build/AppDir"
zuuned_tools="${ZUUNED_APPIMAGE_TOOLS:-$PWD/packaging/appimage-tools}"
mkdir -p "$zuuned_tools"
zuuned_tools=$(realpath "$zuuned_tools")

# Cache hits are verified too. A moving upstream continuous URL can never
# silently change the build tool; archive these pinned inputs with the release.
while read -r filename digest url; do
    [[ -z "${filename:-}" || $filename == \#* ]] && continue
    [[ $filename =~ ^[a-zA-Z0-9._-]+$ && $digest =~ ^[a-f0-9]{64}$ && $url == https://* ]] \
        || { echo "Invalid appimage-tools.lock entry" >&2; exit 1; }
    if [[ ! -f "$zuuned_tools/$filename" ]]; then
        curl --fail --location --retry 2 --output "$zuuned_tools/$filename.download" "$url"
        printf '%s  %s\n' "$digest" "$zuuned_tools/$filename.download" | sha256sum --check --status \
            || { echo "Tool checksum changed: $filename. Review a deliberate lock update; do not bypass this check." >&2; exit 1; }
        mv "$zuuned_tools/$filename.download" "$zuuned_tools/$filename"
    fi
    printf '%s  %s\n' "$digest" "$zuuned_tools/$filename" | sha256sum --check --status \
        || { echo "Cached tool checksum mismatch: $filename" >&2; exit 1; }
    chmod +x "$zuuned_tools/$filename"
done < packaging/appimage-tools.lock

cmake -B "$zuuned_build" -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=/usr \
    "-DZUUNED_SOURCE_REVISION=${ZUUNED_SOURCE_REVISION:-}" \
    "-DZUUNED_LIBZUNE_REVISION=${ZUUNED_LIBZUNE_REVISION:-}" \
    "-DZUUNED_SOURCE_DIRTY=${ZUUNED_SOURCE_DIRTY:-}" \
    "-DZUUNED_LIBZUNE_DIRTY=${ZUUNED_LIBZUNE_DIRTY:-}"
cmake --build "$zuuned_build" --parallel "${ZUUNED_BUILD_JOBS:-4}"
rm -rf "$zuuned_appdir" # Only the fixed generated AppDir, never user data.
DESTDIR="$zuuned_appdir" cmake --install "$zuuned_build"

export APPIMAGE_EXTRACT_AND_RUN=1
export NO_STRIP=1 # Old linuxdeploy strip cannot read current Arch RELR objects.
export QMAKE="${QMAKE:-$(command -v qmake6)}"
zuuned_qt_plugins=$("$QMAKE" -query QT_INSTALL_PLUGINS)
zuuned_portal_plugin="$zuuned_qt_plugins/platformthemes/libqxdgdesktopportal.so"
[[ -s "$zuuned_portal_plugin" ]] || {
    echo "Missing Qt desktop-portal plugin: $zuuned_portal_plugin (Debian: qt6-xdgdesktopportal-platformtheme)." >&2
    exit 1
}
export QML_SOURCES_PATHS="$PWD/qml"
export PATH="$zuuned_tools:$PATH"

# libzune photo preparation and its thumbnail fallback need the CLI too.
# linuxdeploy's blacklist assumes more host packages than our desktop contract.
# Force these dependencies into the bundle; the recursive ELF gate below catches
# any new omissions, including dependencies of plugins loaded only at runtime.
zuuned_extra_libraries=()
zuuned_linker_cache=$(ldconfig -p)
for soname in libusb-1.0.so.0 libasound.so.2 libcom_err.so.2 libgcc_s.so.1 \
    libgmp.so.10 libgpg-error.so.0 libjack.so.0 libpipewire-0.3.so.0 \
    libstdc++.so.6 libz.so.1 libfribidi.so.0 libharfbuzz.so.0; do
    library=$(awk -v name="$soname" '$1 == name && /x86-64/ { print $NF; exit }' <<< "$zuuned_linker_cache")
    [[ -f $library ]] || { echo "Cannot locate required bundle library: $soname" >&2; exit 1; }
    zuuned_extra_libraries+=(--library "$library")
done
"$zuuned_tools/linuxdeploy-x86_64.AppImage" \
    --appdir "$zuuned_appdir" \
    "${zuuned_extra_libraries[@]}" \
    --executable "$(command -v ffmpeg)" --executable "$(command -v ffprobe)" \
    --desktop-file "$zuuned_appdir/usr/share/applications/zuuned.desktop" \
    --icon-file packaging/icons/hicolor/256x256/apps/zuuned.png --plugin qt

# Platform themes are selected at runtime and are not inferred from QML imports.
# Keep the host's file chooser via the portal, using our matching Qt plugin.
install -Dm755 "$zuuned_portal_plugin" \
    "$zuuned_appdir/usr/plugins/platformthemes/libqxdgdesktopportal.so"
"$zuuned_tools/linuxdeploy-x86_64.AppImage" --appdir "$zuuned_appdir" \
    --deploy-deps-only "$zuuned_appdir/usr/plugins/platformthemes/libqxdgdesktopportal.so"

# linuxdeploy's default AppRun may be a symlink to zuuned. Replace the link,
# never write through it (which would overwrite the application executable).
install -m 755 packaging/AppRun "$zuuned_appdir/AppRun.new"
mv -f "$zuuned_appdir/AppRun.new" "$zuuned_appdir/AppRun"
bash packaging/verify-appdir.sh "$zuuned_appdir"

zuuned_version=$(sed -n 's/^  "version": "\([^"]*\)",$/\1/p' "$zuuned_appdir/usr/share/zuuned/zuuned-build.json")
zuuned_commit=$(sed -n 's/^  "application": "\([^"]*\)",$/\1/p' "$zuuned_appdir/usr/share/zuuned/zuuned-build.json")
zuuned_commit=${zuuned_commit:0:12}
zuuned_dirty=""
if grep -Eq '"(applicationDirty|libzuneDirty)": true' "$zuuned_appdir/usr/share/zuuned/zuuned-build.json"; then zuuned_dirty="-dirty"; fi
if grep -Eq '"(applicationDirty|libzuneDirty)": null' "$zuuned_appdir/usr/share/zuuned/zuuned-build.json"; then zuuned_dirty="-state-unknown"; fi
zuuned_stamp=$(date -u -d "@$SOURCE_DATE_EPOCH" +%Y%m%dT%H%M%SZ)
zuuned_name="Zuuned-${zuuned_version}-g${zuuned_commit}${zuuned_dirty}-${zuuned_stamp}-x86_64.AppImage"
zuuned_dist="$zuuned_build/dist"
mkdir -p "$zuuned_dist"
export LDAI_OUTPUT="$zuuned_dist/$zuuned_name"
export LDAI_RUNTIME_FILE="$zuuned_tools/runtime-x86_64"
"$zuuned_tools/linuxdeploy-x86_64.AppImage" --appdir "$zuuned_appdir" --output appimage
bash packaging/verify-appdir.sh "$zuuned_appdir"
[[ -s "$LDAI_OUTPUT" ]] || { echo "AppImage output was not created: $LDAI_OUTPUT" >&2; exit 1; }

cp "$zuuned_appdir/usr/share/zuuned/zuuned-build.json" "$LDAI_OUTPUT.build.json"
cp packaging/appimage-tools.lock "$LDAI_OUTPUT.tools.lock"
(
    cd "$zuuned_dist"
    sha256sum "$zuuned_name" > "$zuuned_name.sha256"
)
{
    echo "Candidate: $zuuned_name"
    echo "Build time: $(date -u +%Y-%m-%dT%H:%M:%SZ)"
    echo "SOURCE_DATE_EPOCH: $SOURCE_DATE_EPOCH"
    echo "Host: $(uname -srmo)"
    if [[ -r /etc/os-release ]]; then cat /etc/os-release; fi
    getconf GNU_LIBC_VERSION
    "$QMAKE" -query QT_VERSION
    pkg-config --modversion mpv libavcodec libavformat libusb-1.0 sqlite3
    echo "Required GLIBC symbol versions (all bundled ELF files):"
    while IFS= read -r -d '' candidate; do
        readelf --version-info "$candidate" 2>/dev/null || true
    done < <(find "$zuuned_appdir/usr" -type f -print0) \
        | grep -oE 'GLIBC_[0-9]+(\.[0-9]+)+' | sort -Vu
    echo "Inventory passed; clean-runtime, fresh-install and host-device gates are recorded separately."
} > "$LDAI_OUTPUT.environment.txt"
printf 'Candidate: %s\nChecksum and build/environment records are alongside it.\n' "$LDAI_OUTPUT"
