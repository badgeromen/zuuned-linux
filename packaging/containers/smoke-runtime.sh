#!/usr/bin/env bash
# Run the actual candidate in a disposable minimal desktop. No rebuilt app or
# Qt test fixture participates. This gate is deliberately not a USB/audio/GPU
# certification. File/folder/save choices must use the real desktop portal.
set -euo pipefail
umask 077

fail() { printf 'FAIL: %s\n' "$*" >&2; exit 1; }
assert_isolated() {
    [[ ! -e /dev/bus/usb ]] || fail 'Refusing a runtime test with USB exposed.'
    local interface
    for interface in /sys/class/net/*; do
        [[ ${interface##*/} == lo ]] || fail 'Run this container with --network none.'
    done
    # libudev can see host hardware through read-only sysfs even without USB
    # device nodes. Keep host hotplug from changing the fixture's panel layout.
    local usb_device vendor product
    for usb_device in /sys/bus/usb/devices/*; do
        [[ -f $usb_device/idVendor && -f $usb_device/idProduct ]] || continue
        read -r vendor < "$usb_device/idVendor"
        read -r product < "$usb_device/idProduct"
        if [[ $vendor == 045e && ( $product == 0710 || $product == 063e ) ]]; then
            fail 'Mask USB enumeration with --mount type=tmpfs,dst=/sys/bus/usb/devices; host hotplug must not affect this desktop fixture.'
        fi
    done
}
assert_isolated

if [[ ${1:-} != --desktop ]]; then
    [[ $# == 2 ]] || fail 'usage: zuuned-runtime-smoke candidate.AppImage output-directory'
    candidate=$(realpath -e -- "$1")
    [[ -f $candidate && -x $candidate ]] || fail 'Candidate must be an executable AppImage.'
    mkdir -p -- "$2"
    output=$(realpath -e -- "$2")
    # Keep evidence from prior runs intact, including failed ones.
    evidence=$(mktemp -d "$output/runtime-XXXXXXXX")
    printf '%s\n' "$evidence" > "$output/latest-runtime.txt"
    {
        date -u +%Y-%m-%dT%H:%M:%SZ
        sha256sum -- "$candidate"
        sha256sum -- "$(realpath -e -- "$0")"
        cat /etc/os-release
        getconf GNU_LIBC_VERSION
        printf '\nInstalled packages (no system Qt/mpv/FFmpeg permitted):\n'
        dpkg-query -W -f='${binary:Package}\t${Version}\n'
    } > "$evidence/environment.txt"
    if dpkg-query -W -f='${binary:Package}\n' | grep -Eq '^(ffmpeg|mpv|libmpv|libavcodec|libavformat|libqt[56]|qml[56]-)'; then
        fail 'System Qt/mpv/FFmpeg packages would mask missing candidate dependencies.'
    fi
    printf 'Candidate evidence: %s\n' "$evidence"
    set +e
    xvfb-run -a -s '-screen 0 1440x1000x24 +extension GLX -nolisten tcp' \
        dbus-run-session -- bash "$(realpath -e -- "$0")" --desktop "$candidate" "$evidence" \
        > "$evidence/runner.log" 2>&1
    result=$?
    set -e
    cat "$evidence/runner.log"
    [[ $result == 0 ]] || fail "Runtime gate exited $result; retained evidence: $evidence"
    exit 0
fi

[[ $# == 3 ]] || fail 'Internal desktop invocation is incomplete.'
candidate=$2
evidence=$3
work=$(mktemp -d /tmp/zuuned-runtime-XXXXXXXX)
app_pid=
wm_pid=
portal_pid=
gtk_portal_pid=
portal_monitor_pid=
app_log=
window_id=
launch_name=
native_before=
native_log_files() {
    [[ -d $1 ]] || return 0
    find "$1" -type f -name 'zuuned-*.log' -print | LC_ALL=C sort
}
preserve_native_logs() {
    local state_root=$1 label=$2 source destination
    destination="$evidence/local-logs/$label"
    mkdir -p "$destination"
    : > "$destination/inventory.txt"
    while IFS= read -r source; do
        # The filename includes its session/PID/random identity. Copy only the
        # native logger's files, never the rest of a supplied user's profile.
        cp -p -- "$source" "$destination/"
        # Hash the retained snapshot, not a source still receiving new lines.
        stat -c '%a %s %n' -- "$destination/${source##*/}" >> "$destination/inventory.txt"
        sha256sum -- "$destination/${source##*/}" >> "$destination/inventory.txt"
    done < <(native_log_files "$state_root")
}
cleanup() {
    local result=$?
    trap - EXIT
    # These PIDs belong exclusively to this invocation. No app on the host is
    # visible to the container, and USB absence is checked before every launch.
    if [[ -n $app_pid ]] && kill -0 "$app_pid" 2>/dev/null; then
        tail -n 40 "$app_log" > "$evidence/last-app-lines.txt" 2>/dev/null || true
        kill -TERM "$app_pid" 2>/dev/null || true
        for ((i=0; i<30; i++)); do
            kill -0 "$app_pid" 2>/dev/null || break
            sleep 0.1
        done
        kill -KILL "$app_pid" 2>/dev/null || true
        wait "$app_pid" 2>/dev/null || true
    fi
    for owned_pid in "$portal_pid" "$gtk_portal_pid" "$portal_monitor_pid"; do
        if [[ -n $owned_pid ]]; then kill -TERM "$owned_pid" 2>/dev/null || true; wait "$owned_pid" 2>/dev/null || true; fi
    done
    if [[ -n $wm_pid ]]; then kill -TERM "$wm_pid" 2>/dev/null || true; wait "$wm_pid" 2>/dev/null || true; fi
    # Retain the app's own diagnostics even when startup or a later gate fails.
    # This runs after its process exits so the capture worker has drained.
    preserve_native_logs "$work/state" final-fixture || true
    preserve_native_logs "$work/upgrade-state" final-upgrade || true
    rm -rf -- "$work" # Only this invocation's mktemp tree.
    exit "$result"
}
trap cleanup EXIT

export XDG_CONFIG_HOME="$work/config" XDG_DATA_HOME="$work/data"
export XDG_CACHE_HOME="$work/cache" XDG_STATE_HOME="$work/state" XDG_RUNTIME_DIR="$work/runtime"
mkdir -p "$XDG_CONFIG_HOME" "$XDG_DATA_HOME" "$XDG_CACHE_HOME" "$XDG_STATE_HOME" "$XDG_RUNTIME_DIR"
export QT_QPA_PLATFORM=xcb QSG_RHI_BACKEND=opengl
export XDG_CURRENT_DESKTOP=ZUUNED-Smoke XDG_SESSION_TYPE=x11 GDK_BACKEND=x11
export LIBGL_ALWAYS_SOFTWARE=1 ZUUNED_MPV_AO=null
unset QT_QPA_PLATFORMTHEME GTK_THEME GTK_USE_PORTAL
unset QT_PLUGIN_PATH QT_QPA_PLATFORM_PLUGIN_PATH QML_IMPORT_PATH QML2_IMPORT_PATH LD_LIBRARY_PATH
openbox > "$evidence/window-manager.log" 2>&1 &
wm_pid=$!
glxinfo -B > "$evidence/software-opengl.txt" 2>&1

# Extract before starting the candidate so a loader failure still leaves a
# complete dependency report. Extraction uses the shipped AppImage runtime.
mkdir "$work/extract"
(cd "$work/extract" && "$candidate" --appimage-extract) > "$evidence/extraction.log" 2>&1
appdir="$work/extract/squashfs-root"
[[ -x $appdir/AppRun && -x $appdir/usr/bin/ffmpeg && -x $appdir/usr/bin/ffprobe ]] || fail 'Extracted candidate is incomplete.'
cp "$appdir/usr/share/zuuned/zuuned-build.json" "$evidence/zuuned-build.json"

# The candidate's tools run against the candidate's libraries. Keep these paths
# out of the desktop's environment so host tools cannot accidentally use them.
bundled_ffmpeg() { env LD_LIBRARY_PATH="$appdir/usr/lib" "$appdir/usr/bin/ffmpeg" -hide_banner -nostdin -y "$@"; }
bundled_ffprobe() { env LD_LIBRARY_PATH="$appdir/usr/lib" "$appdir/usr/bin/ffprobe" -hide_banner "$@"; }
missing_library=0
for executable in zuuned ffmpeg ffprobe; do
    if ! env LD_LIBRARY_PATH="$appdir/usr/lib" ldd "$appdir/usr/bin/$executable" > "$evidence/ldd-$executable.txt" 2>&1 \
        || grep -q 'not found' "$evidence/ldd-$executable.txt"; then missing_library=1; fi
done
[[ $missing_library == 0 ]] || fail 'Missing runtime libraries; see all three ldd reports.'
# Give identification its own completely empty profile. Checking both entry
# points separately prevents one side's profile writes from masking the other.
version_profile="$work/version-profile"
mkdir -p "$version_profile"/{home,config,data,cache,state,runtime}
version_env=(HOME="$version_profile/home" XDG_CONFIG_HOME="$version_profile/config"
    XDG_DATA_HOME="$version_profile/data" XDG_CACHE_HOME="$version_profile/cache"
    XDG_STATE_HOME="$version_profile/state" XDG_RUNTIME_DIR="$version_profile/runtime")
check_version_profile() {
    find "$version_profile" -mindepth 2 -printf '%P\n' | LC_ALL=C sort > "$evidence/$1-profile-writes.txt"
    [[ ! -s $evidence/$1-profile-writes.txt ]] || fail "$1 --version wrote profile/log files."
}
timeout 45 env "${version_env[@]}" APPIMAGE_EXTRACT_AND_RUN=1 "$candidate" --version > "$evidence/appimage-version.txt" 2>&1
grep -q '^Zuuned ' "$evidence/appimage-version.txt" || fail 'AppImage version check returned no build identity.'
check_version_profile appimage-version
timeout 45 env "${version_env[@]}" "$appdir/AppRun" --version > "$evidence/extracted-version.txt" 2>&1
check_version_profile extracted-version
cmp "$evidence/appimage-version.txt" "$evidence/extracted-version.txt" || fail 'AppImage and extracted build identity differ.'

media="$work/media"
mkdir "$media"
{
    bundled_ffmpeg -v error -f lavfi -i 'sine=frequency=440:duration=2' -c:a flac \
        -metadata title='Runtime proof' -metadata artist='Zuuned Fixture' \
        -metadata album_artist='Zuuned Fixture' -metadata album='Print proof' \
        -metadata track=1 -metadata disc=2 "$media/fixture.flac"
    bundled_ffmpeg -v error -f lavfi -i 'testsrc2=size=400x400:rate=1' \
        -frames:v 1 -threads 1 "$media/cover.jpg"
    bundled_ffmpeg -v error -f lavfi -i 'testsrc2=size=160x120:rate=12:duration=1' \
        -c:v wmv2 "$work/fixture.wmv"
    bundled_ffmpeg -v error -i "$media/fixture.flac" -f null -
    bundled_ffmpeg -v error -i "$work/fixture.wmv" -f null -
    bundled_ffprobe -v error -show_format -show_streams "$media/fixture.flac"
    bundled_ffprobe -v error -show_format -show_streams "$work/fixture.wmv"
} > "$evidence/bundled-media-tools.log" 2>&1
cp "$media/cover.jpg" "$evidence/generated-cover.jpg"
playlist="$work/portal-playlist.m3u"
printf '#EXTM3U\n#EXTINF:2,Zuuned Fixture - Runtime proof\n%s\n' "$media/fixture.flac" > "$playlist"

dbus_pid() {
    timeout 5 gdbus call --session --dest org.freedesktop.DBus \
        --object-path /org/freedesktop/DBus --method org.freedesktop.DBus.GetConnectionUnixProcessID "$1" \
        | sed -n 's/^(uint32 \([0-9][0-9]*\),)$/\1/p'
}
portal_executable() {
    local path
    while IFS= read -r path; do
        if [[ ${path##*/} == "$1" && -f $path && -x $path ]]; then printf '%s\n' "$path"; return; fi
    done < <(dpkg-query -L "$1")
    fail "Missing desktop portal executable: $1"
}
start_portals() {
    # This is this disposable desktop's backend preference, never an app theme
    # override or a modification to the user's desktop configuration.
    local desktop="$work/portal-desktop"
    mkdir -p "$desktop/config/xdg-desktop-portal" "$desktop/data" "$desktop/cache" "$desktop/state"
    cat > "$desktop/config/xdg-desktop-portal/portals.conf" <<'PORTALS'
[preferred]
default=gtk
org.freedesktop.impl.portal.FileChooser=gtk
PORTALS
    cp "$desktop/config/xdg-desktop-portal/portals.conf" "$evidence/desktop-portals.conf"
    local portal_env=(XDG_CONFIG_HOME="$desktop/config" XDG_DATA_HOME="$desktop/data"
        XDG_CACHE_HOME="$desktop/cache" XDG_STATE_HOME="$desktop/state" GTK_USE_PORTAL=0)
    env "${portal_env[@]}" dbus-update-activation-environment DISPLAY XAUTHORITY XDG_CURRENT_DESKTOP \
        XDG_SESSION_TYPE XDG_RUNTIME_DIR XDG_CONFIG_HOME XDG_DATA_HOME XDG_CACHE_HOME XDG_STATE_HOME GDK_BACKEND GTK_USE_PORTAL
    stdbuf -oL -eL dbus-monitor --session \
        "interface='org.freedesktop.portal.FileChooser'" \
        "interface='org.freedesktop.impl.portal.FileChooser'" \
        "interface='org.freedesktop.portal.Request',member='Response'" \
        > "$evidence/portal-bus.log" 2> "$evidence/portal-monitor.log" &
    portal_monitor_pid=$!
    env "${portal_env[@]}" "$(portal_executable xdg-desktop-portal-gtk)" --verbose \
        > "$evidence/portal-gtk.log" 2>&1 &
    gtk_portal_pid=$!
    for ((attempt=0; attempt<100; attempt++)); do
        kill -0 "$gtk_portal_pid" 2>/dev/null || fail 'GTK desktop portal exited at startup.'
        [[ $(dbus_pid org.freedesktop.impl.portal.desktop.gtk 2>/dev/null || true) == "$gtk_portal_pid" ]] && break
        sleep 0.2
    done
    [[ $(dbus_pid org.freedesktop.impl.portal.desktop.gtk) == "$gtk_portal_pid" ]] || fail 'GTK desktop portal did not own its D-Bus name.'
    env "${portal_env[@]}" "$(portal_executable xdg-desktop-portal)" --verbose \
        > "$evidence/portal-desktop.log" 2>&1 &
    portal_pid=$!
    for ((attempt=0; attempt<100; attempt++)); do
        kill -0 "$portal_pid" 2>/dev/null || fail 'Desktop portal exited at startup.'
        [[ $(dbus_pid org.freedesktop.portal.Desktop 2>/dev/null || true) == "$portal_pid" ]] && break
        sleep 0.2
    done
    [[ $(dbus_pid org.freedesktop.portal.Desktop) == "$portal_pid" ]] || fail 'Desktop portal did not own its D-Bus name.'
    timeout 10 gdbus call --session --dest org.freedesktop.portal.Desktop \
        --object-path /org/freedesktop/portal/desktop --method org.freedesktop.DBus.Properties.Get \
        org.freedesktop.portal.FileChooser version > "$evidence/portal-filechooser-version.txt"
    kill -0 "$portal_monitor_pid" || fail 'D-Bus evidence monitor is not running.'
}
portal_mark() {
    portal_tag=$1 portal_method=$2 portal_title=$3 portal_directory=${4:-false}
    portal_window= portal_handle=
    portal_offset=$(wc -l < "$evidence/portal-bus.log")
}
portal_records() {
    tail -n "+$((portal_offset + 1))" "$evidence/portal-bus.log" > "$evidence/portal-$portal_tag-bus.log"
    awk -v interface="$1" -v member="$portal_method" '
        function emit() {
            if (record ~ /^method call / && index(record, "interface=" interface "; member=" member "\n")) print record "\n"
        }
        /^(signal|method call|method return|error) / { emit(); record=$0; next }
        { record=record "\n" $0 }
        END { emit() }' \
        "$evidence/portal-$portal_tag-bus.log"
}
find_portal_window() {
    local id
    while IFS= read -r id; do
        if [[ $(xdotool getwindowname "$id" 2>/dev/null) == "$portal_title" ]]; then printf '%s\n' "$id"; return; fi
    done < <(xdotool search --onlyvisible --pid "$gtk_portal_pid" 2>/dev/null || true)
}
await_portal_chooser() {
    local request="$evidence/portal-$portal_tag-request.txt"
    local backend="$evidence/portal-$portal_tag-backend.txt" sender directory
    for ((attempt=0; attempt<100; attempt++)); do
        still_running
        kill -0 "$portal_pid" && kill -0 "$gtk_portal_pid" && kill -0 "$portal_monitor_pid" || fail 'Desktop portal or monitor exited.'
        portal_records org.freedesktop.portal.FileChooser > "$request"
        portal_records org.freedesktop.impl.portal.FileChooser > "$backend"
        portal_window=$(find_portal_window)
        [[ -s $request && -s $backend && -n $portal_window ]] && break
        sleep 0.2
    done
    capture "portal-$portal_tag-chooser"
    [[ -s $request && -s $backend && -n $portal_window ]] || fail "$portal_tag did not open a real GTK desktop portal chooser."
    [[ $(grep -c '^method call ' "$request") == 1 && $(grep -c '^method call ' "$backend") == 1 ]] || fail 'Expected exactly one correlated portal request.'
    sender=$(sed -n '1s/.* sender=\([^ ]*\) .*/\1/p' "$request")
    [[ $(dbus_pid "$sender") == "$app_pid" ]] || fail 'FileChooser request did not come from the candidate process.'
    sender=$(sed -n '1s/.* sender=\([^ ]*\) .*/\1/p' "$backend")
    [[ $(dbus_pid "$sender") == "$portal_pid" ]] || fail 'Backend request did not come from the real desktop portal.'
    [[ $(xdotool getwindowpid "$portal_window") == "$gtk_portal_pid" ]] || fail 'Chooser window is not owned by the GTK desktop backend.'
    portal_handle=$(sed -n 's/^[[:space:]]*object path "\([^"]*\)"/\1/p' "$backend" | head -n 1)
    [[ $portal_handle =~ ^/org/freedesktop/portal/desktop/request/[a-zA-Z0-9_]+/[a-zA-Z0-9_]+$ ]] || fail 'No correlated portal request handle.'
    if [[ $portal_method == OpenFile ]]; then
        directory=$(awk '/string "directory"/ { found=1; next } found && /variant.*boolean/ { print $NF; exit }' "$request")
        if [[ $portal_directory == true ]]; then
            [[ $directory == true ]] || fail 'Folder chooser omitted the portal directory=true option.'
        else
            [[ -z $directory || $directory == false ]] || fail 'File chooser unexpectedly requested a directory.'
        fi
    fi
    printf '%s: app PID=%s; portal PID=%s; GTK window PID=%s; %s; handle=%s\n' \
        "$portal_tag" "$app_pid" "$portal_pid" "$gtk_portal_pid" "$portal_method" "$portal_handle" >> "$evidence/portal-picker-checks.txt"
}
portal_response() {
    tail -n "+$((portal_offset + 1))" "$evidence/portal-bus.log" > "$evidence/portal-$portal_tag-bus.log"
    awk -v handle="$portal_handle" '
        function emit() {
            if (record ~ /^signal / && index(record, "path=" handle ";") && index(record, "interface=org.freedesktop.portal.Request; member=Response\n")) print record "\n"
        }
        /^(signal|method call|method return|error) / { emit(); record=$0; next }
        { record=record "\n" $0 }
        END { emit() }' \
        "$evidence/portal-$portal_tag-bus.log" > "$evidence/portal-$portal_tag-response.txt"
    [[ -s $evidence/portal-$portal_tag-response.txt ]]
}
select_portal_path() {
    local selected=$1 status
    xdotool windowactivate --sync "$portal_window"
    xdotool key --clearmodifiers ctrl+l ctrl+a
    xdotool type --clearmodifiers --delay 1 -- "$selected"
    xdotool key --clearmodifiers Return
    sleep 1
    # GTK may first navigate to a typed folder. A second normal acceptance is
    # sent only while this same backend-owned chooser remains visible.
    if ! portal_response && [[ -n $(find_portal_window) ]]; then xdotool key --clearmodifiers Return; fi
    for ((attempt=0; attempt<100; attempt++)); do
        still_running
        portal_response && break
        sleep 0.2
    done
    capture "portal-$portal_tag-result"
    portal_response || fail "$portal_tag has no correlated desktop portal response."
    status=$(sed -n '2s/^[[:space:]]*uint32 \([0-9][0-9]*\)[[:space:]]*$/\1/p' "$evidence/portal-$portal_tag-response.txt")
    [[ $status == 0 ]] || fail "$portal_tag portal response did not report success."
    grep -Fq "string \"file://$selected\"" "$evidence/portal-$portal_tag-response.txt" || fail 'Portal response did not return the exact selected fixture URI.'
    printf '%s: PASS; successful response selected file://%s\n' "$portal_tag" "$selected" >> "$evidence/portal-picker-checks.txt"
}

check_log() {
    local inspected_log="$app_log"
    if [[ $app_log == "$evidence/upgraded-profile.log" ]]; then
        # The copied DB deliberately retains original media/cache paths while
        # those files are absent from the container. Keep their image warnings
        # as evidence; they do not diagnose a migration or bundled-QML failure.
        inspected_log="$evidence/upgraded-profile-without-absent-images.log"
        # Home redaction turns file:///home/user/... into file://~/.... Both
        # spellings still refer to intentionally unmounted local artwork.
        sed -E '/\[qml-warn\].*QML QQuickImage: Cannot open: file:\/\/(\/|~\/)/d' "$app_log" > "$inspected_log"
    fi
    if grep -Ein '\[qml-warn\]|QQmlApplicationEngine failed|ReferenceError:|TypeError:|module .* is not installed|error while loading shared libraries|Could not load the Qt platform plugin|Failed to create.*(context|RHI)|QML.*(Error|unavailable)' "$inspected_log"; then
        fail "QML/runtime error in $app_log"
    fi
}
still_running() { kill -0 "$app_pid" 2>/dev/null || { cat "$app_log"; fail 'Candidate exited before the runtime check finished.'; }; }
check_native_logs() {
    local phase=$1 file count=0
    local inventory="$evidence/$launch_name-native-$phase.txt"
    native_log_files "$XDG_STATE_HOME" > "$inventory"
    [[ -s $inventory ]] || fail 'Candidate did not create an automatic local session log.'
    while IFS= read -r file; do
        ((count+=1))
        [[ $(stat -c '%a' -- "$file") == 600 ]] || fail "Session log permissions are not 0600: $file"
        [[ $(stat -c '%a' -- "${file%/*}") == 700 ]] || fail "Session log directory permissions are not 0700: $file"
        [[ $(stat -c '%s' -- "$file") -le 2097152 ]] || fail 'A native log exceeds the 2 MiB rotation limit.'
    done < "$inventory"
    [[ $count -le 8 ]] || fail 'Native log retention exceeds eight files.'
    local new_session="$evidence/$launch_name-native-new-session-$phase.txt"
    LC_ALL=C comm -13 "$native_before" "$inventory" > "$new_session"
    [[ -s $new_session ]] || fail 'Relaunch reused old logs without creating a new session file.'
    local has_header=0 has_native_startup=0 has_shutdown=0
    while IFS= read -r file; do
        [[ -s $file ]] || fail 'Native session log is empty.'
        if grep -Fq '[diagnostics] session started' "$file"; then has_header=1; fi
        # This existing startup line is emitted with fprintf(stderr), proving
        # the native capture path rather than only a Qt message handler.
        if grep -Eq '\[zuuned\].*Qt runtime ' "$file"; then has_native_startup=1; fi
        if grep -Fq '[diagnostics] session ended' "$file"; then has_shutdown=1; fi
    done < "$new_session"
    [[ $has_header == 1 && $has_native_startup == 1 ]] || fail 'Local log lacks its session header or native startup/build line.'
    if [[ $phase == stopped ]]; then
        [[ $has_shutdown == 1 ]] || fail 'Normal shutdown did not drain and close the native session log.'
    fi
    preserve_native_logs "$XDG_STATE_HOME" "$launch_name-$phase"
}
start_app() {
    assert_isolated
    launch_name=$1
    app_log="$evidence/$1.log"
    native_before="$evidence/$1-native-before.txt"
    native_log_files "$XDG_STATE_HOME" > "$native_before"
    shift
    "$appdir/AppRun" "$@" > "$app_log" 2>&1 &
    app_pid=$!
    window_id=
    for ((attempt=0; attempt<150; attempt++)); do
        still_running
        window_id=$(xdotool search --onlyvisible --pid "$app_pid" --name '^Zuuned$' 2>/dev/null | head -n 1 || true)
        [[ -n $window_id ]] && break
        sleep 0.2
    done
    [[ -n $window_id ]] || fail 'No visible candidate window within 30 seconds.'
    xdotool windowsize --sync "$window_id" 1400 920
    xdotool windowmove --sync "$window_id" 10 25
    xdotool windowactivate --sync "$window_id"
    sleep 6 # Includes the production splash and its fade.
    still_running
    check_log
    check_native_logs running
}
capture() {
    still_running
    bundled_ffmpeg -v error -f x11grab -video_size 1440x1000 -i "$DISPLAY" \
        -frames:v 1 -threads 1 "$evidence/$1.png" >> "$evidence/captures.log" 2>&1
    [[ -s $evidence/$1.png ]] || fail "Empty capture: $1"
    bundled_ffprobe -v error -select_streams v:0 -show_entries stream=width,height \
        -of csv=p=0 "$evidence/$1.png" | grep -qx '1440,1000' || fail 'Capture dimensions are wrong.'
    check_log
}
stop_app() {
    assert_isolated
    check_log
    [[ ! -s $XDG_DATA_HOME/Zuuned/Zuuned/inflight-send ]] || fail 'Unexpected transfer marker in no-USB fixture.'
    # The temporary WM sends WM_DELETE_WINDOW; this is a normal application
    # close, giving QSettings/SQLite their regular shutdown path.
    xdotool windowactivate --sync "$window_id" key --clearmodifiers alt+F4
    for ((attempt=0; attempt<100; attempt++)); do
        kill -0 "$app_pid" 2>/dev/null || break
        sleep 0.1
    done
    if kill -0 "$app_pid" 2>/dev/null; then fail 'Candidate did not close normally within 10 seconds.'; fi
    local status=0
    wait "$app_pid" || status=$?
    app_pid=
    [[ $status == 0 ]] || fail "Candidate exited with status $status."
    check_log
    check_native_logs stopped
}

start_portals
start_app fresh-onboarding --page settings --settings-tab Appearance
capture 01-fresh-onboarding
# Onboarding's 600px centered column places Skip 60px from its right edge,
# centered 52px above the bottom. Use the actual native window geometry.
geometry=$(xdotool getwindowgeometry --shell "$window_id")
width=$(sed -n 's/^WIDTH=//p' <<< "$geometry")
height=$(sed -n 's/^HEIGHT=//p' <<< "$geometry")
[[ $width =~ ^[0-9]+$ && $height =~ ^[0-9]+$ ]] || fail 'No native window geometry.'
xdotool mousemove --window "$window_id" "$((width / 2 + 220))" "$((height - 52))" click 1
settings="$XDG_CONFIG_HOME/Zuuned/Zuuned.conf"
for ((attempt=0; attempt<60; attempt++)); do
    grep -q '^onboardingComplete=true$' "$settings" 2>/dev/null && break
    still_running
    sleep 0.2
done
grep -q '^onboardingComplete=true$' "$settings" || fail 'The actual onboarding Skip action did not complete.'
sleep 2
capture 02-empty-appearance

db="$XDG_DATA_HOME/Zuuned/Zuuned/library.db"
[[ -s $db ]] || fail 'Fresh candidate did not create its library.'
[[ $(sqlite3 "$db" 'SELECT count(*) FROM tracks;') == 0 ]] || fail 'Fresh profile was not empty.'
# Add the generated music through the production Library browse control and
# the desktop's actual directory chooser. No watch-folder database seeding.
[[ $(sqlite3 "$db" 'SELECT count(*) FROM watch_folders;') == 0 ]] || fail 'Fresh profile has unexpected watch folders.'
# An empty library shows onboarding on every launch. Stay in the session where
# Skip was actually clicked and use the visible Library tab before adding media.
xdotool mousemove --window "$window_id" 450 90 click 1
sleep 1
capture portal-folder-library
portal_mark folder OpenFile 'Choose a music folder' true
# Current Library layout: browse is at y=560 in the client window.
xdotool mousemove --window "$window_id" 1260 560 click 1
await_portal_chooser
select_portal_path "$media"
for ((attempt=0; attempt<100; attempt++)); do
    [[ $(sqlite3 "$db" 'SELECT count(*) FROM tracks;') == 1 ]] && break
    still_running
    sleep 0.2
done
[[ $(sqlite3 "$db" "SELECT count(*) FROM watch_folders WHERE path='$media' AND type='music';") == 1 ]] || fail 'Desktop folder selection did not add the actual music watch folder.'
sqlite3 -header -csv "$db" 'SELECT title,artist,albumartist,album,tracknumber,discnumber FROM tracks;' > "$evidence/imported-fixture.csv"
[[ $(sqlite3 "$db" "SELECT count(*) FROM tracks WHERE title='Runtime proof' AND artist='Zuuned Fixture' AND album='Print proof' AND tracknumber=1 AND discnumber=2;") == 1 ]] \
    || fail 'The bundled app did not import the real-media fixture metadata after desktop folder selection.'
stop_app
start_app original-appearance --page settings --settings-tab Appearance
sleep 3
capture 03-original-appearance
stop_app

# Set the production QSettings key while stopped. This tests persisted
# preference loading and actual treated rendering, not the control's click.
sed -i '/^artworkStyle=/d; /^\[General\]/a artworkStyle=halftone' "$settings"
grep -q '^artworkStyle=halftone$' "$settings" || fail 'Fixture could not seed the production artwork setting.'
start_app halftone-appearance --page settings --settings-tab Appearance
print_cache="$XDG_CACHE_HOME/Zuuned/Zuuned/printed-art"
for ((attempt=0; attempt<100; attempt++)); do
    if [[ -d $print_cache ]] && find "$print_cache" -name 'print-*.png' -type f | grep -q .; then break; fi
    still_running
    sleep 0.2
done
[[ -d $print_cache ]] || fail 'Halftone did not create its actual native print cache.'
find "$print_cache" -name 'print-*.png' -type f -exec sha256sum {} + > "$evidence/printed-art.sha256"
[[ -s $evidence/printed-art.sha256 ]] || fail 'Halftone produced no cached artwork.'
capture 04-halftone-appearance
stop_app
grep -q '^artworkStyle=halftone$' "$settings" || fail 'Persisted halftone setting was lost on shutdown.'

# OpenFile with directory=false is a separate production path: a local .m3u
# picker followed by the native playlist importer, without any Zune connection.
start_app playlist-file-picker --page settings --settings-tab Mixtapes
xdotool mousemove --window "$window_id" 950 750 click --repeat 24 --delay 30 5
sleep 1
capture portal-playlist-settings
portal_mark playlist OpenFile 'Import a playlist (.m3u)'
xdotool mousemove --window "$window_id" 515 824 click 1
await_portal_chooser
select_portal_path "$playlist"
for ((attempt=0; attempt<100; attempt++)); do
    [[ $(sqlite3 "$db" "SELECT count(*) FROM playlists WHERE name='portal-playlist';") == 1 ]] && break
    still_running
    sleep 0.2
done
[[ $(sqlite3 "$db" "SELECT count(*) FROM playlists p JOIN playlist_tracks pt ON pt.playlist_id=p.id JOIN tracks t ON t.id=pt.track_id WHERE p.name='portal-playlist' AND t.filepath='$media/fixture.flac';") == 1 ]] \
    || fail 'Desktop file selection did not import the fixture playlist and its track.'
stop_app

# Exercise the production Health button and actual SaveFile dialog. This uses
# the same fixed window geometry as the onboarding gate, not a QML test API.
start_app diagnostics-health --page settings --settings-tab Health
capture 05-diagnostics-health
report="$evidence/zuuned-debug-report.txt"
[[ ! -e $report ]] || fail 'The report fixture destination unexpectedly exists.'
# DiagnosticsSettings is the first Health section; at 1400x920 its primary
# button is inside this rectangle. Keep the capture when Qt layout changes.
portal_mark report SaveFile 'Save a ZUUNED debug report'
xdotool mousemove --window "$window_id" 405 252 click 1
await_portal_chooser
select_portal_path "$report"
for ((attempt=0; attempt<100; attempt++)); do
    [[ -s $report ]] && break
    still_running
    sleep 0.2
done
capture 07-debug-report-saved
[[ -s $report ]] || fail 'The actual Health export/SaveFile action did not create its report.'
[[ $(stat -c '%a' -- "$report") == 600 ]] || fail 'Exported report permissions are not 0600.'
[[ $(stat -c '%s' -- "$report") -le 6291456 ]] || fail 'Exported report exceeds the 6 MiB smoke bound.'
for heading in 'ZUUNED DEBUG REPORT' 'SYSTEM AND APPLICATION' 'RECENT SESSION LOGS'; do
    grep -Fxq "$heading" "$report" || fail "Export lacks $heading."
done
grep -Fq '[diagnostics] session started' "$report" || fail 'Export has no captured native session.'
grep -Eq '\[zuuned\].*Qt runtime ' "$report" || fail 'Export has no native startup/build line.'
grep -Eq '"logging_available"[[:space:]]*:[[:space:]]*true' "$report" || fail 'Export does not report available local logging.'
for field in application libzune; do
    revision=$(sed -n "s/^[[:space:]]*\"$field\":[[:space:]]*\"\([a-f0-9]\{40\}\)\".*/\1/p" "$evidence/zuuned-build.json")
    [[ $revision =~ ^[a-f0-9]{40}$ ]] || fail "Candidate metadata has no full $field revision."
    grep -Fq "$revision" "$report" || fail "Export does not identify the candidate's $field revision."
done
sha256sum -- "$report" > "$evidence/zuuned-debug-report.sha256"
stop_app
printf 'PASS: actual Health export button and desktop portal SaveFile dialog wrote a private bounded report with candidate revisions and native session logs.\n' > "$evidence/diagnostics-export.txt"

# Optional packaged photo-organizer rehearsal. All mutations below come from
# actual UI input; SQL only observes the disposable application's database.
if [[ ${ZUUNED_PHOTO_GATE:-0} == 1 ]]; then
    photo_media="$work/photo-media"
    mkdir "$photo_media"
    cp "$media/cover.jpg" "$photo_media/runtime-photo.jpg"
    start_app photo-folder-settings --page settings --settings-tab Library
    capture photo-01-library-before-import
    # This restarted Library page has one watch row and starts at the top.
    # Its final type segment selects Photos (confirmed in photo-01 capture).
    xdotool mousemove --window "$window_id" 1175 285 click 1
    portal_mark photos-folder OpenFile 'Choose a photos folder' true
    xdotool mousemove --window "$window_id" 1260 285 click 1
    await_portal_chooser
    select_portal_path "$photo_media"
    for ((attempt=0; attempt<100; attempt++)); do
        [[ $(sqlite3 "$db" "SELECT count(*) FROM photos WHERE filepath='$photo_media/runtime-photo.jpg';") == 1 ]] && break
        still_running
        sleep 0.2
    done
    [[ $(sqlite3 "$db" "SELECT count(*) FROM watch_folders WHERE path='$photo_media' AND type='photos';") == 1 ]] || fail 'Photos chooser did not add the production photo watch folder.'
    [[ $(sqlite3 "$db" "SELECT count(*) FROM photos WHERE filepath='$photo_media/runtime-photo.jpg';") == 1 ]] || fail 'Production scanner did not index the generated photo.'
    stop_app
    start_app photo-album-builder --page photos
    capture photo-02-folders
    xdotool mousemove --window "$window_id" 410 90 click 1
    sleep 1
    capture photo-03-albums
    xdotool mousemove --window "$window_id" 505 90 click 1
    sleep 1
    capture photo-04-parent-draft
    xdotool mousemove --window "$window_id" 180 147 click 1
    xdotool key --clearmodifiers ctrl+a
    xdotool type --clearmodifiers 'Runtime parent'
    [[ $(sqlite3 "$db" "SELECT count(*) FROM photo_albums WHERE name='Runtime parent';") == 0 ]] || fail 'Unsaved parent draft unexpectedly persisted.'
    xdotool mousemove --window "$window_id" 258 850 click 1
    sleep 1
    capture photo-05-parent-saved
    [[ $(sqlite3 "$db" "SELECT count(*) FROM photo_albums WHERE name='Runtime parent' AND parent_id IS NULL;") == 1 ]] || fail 'Photo builder did not save the parent album.'
    xdotool mousemove --window "$window_id" 420 215 click 1
    sleep 1
    capture photo-06-parent-open
    xdotool mousemove --window "$window_id" 510 90 click 1
    sleep 1
    capture photo-07-child-draft
    xdotool mousemove --window "$window_id" 180 147 click 1
    xdotool key --clearmodifiers ctrl+a
    xdotool type --clearmodifiers 'Runtime child'
    [[ $(sqlite3 "$db" "SELECT count(*) FROM photo_albums WHERE name='Runtime child';") == 0 ]] || fail 'Unsaved child draft unexpectedly persisted.'
    xdotool mousemove --window "$window_id" 258 850 click 1
    sleep 1
    capture photo-08-child-saved
    [[ $(sqlite3 "$db" "SELECT count(*) FROM photo_albums child JOIN photo_albums parent ON child.parent_id=parent.id WHERE child.name='Runtime child' AND parent.name='Runtime parent';") == 1 ]] || fail 'Photo builder did not save the nested album under its parent.'
    stop_app
    start_app photo-albums-reopened --page photos
    xdotool mousemove --window "$window_id" 410 90 click 1
    sleep 1
    xdotool mousemove --window "$window_id" 420 215 click 1
    sleep 1
    capture photo-09-reopened-parent
    [[ $(sqlite3 "$db" "SELECT count(*) FROM photo_albums child JOIN photo_albums parent ON child.parent_id=parent.id WHERE child.name='Runtime child' AND parent.name='Runtime parent';") == 1 ]] || fail 'Nested photo albums did not persist across restart.'
    sqlite3 -header -csv "$db" 'SELECT id,name,parent_id FROM photo_albums ORDER BY id;' > "$evidence/photo-albums.csv"
    stop_app
    printf 'PASS: packaged app imported a generated photo through its desktop chooser; actual builder UI staged and saved an empty parent and nested child; records survived restart. Screenshots retain reopened UI. Album membership, bulk selection/dragging and USB transfers are separate gates.\n' > "$evidence/photo-builder-PASS.txt"
fi

cp "$settings" "$evidence/fixture-settings.conf"
sqlite3 "$db" 'PRAGMA integrity_check;' > "$evidence/library-integrity.txt"
grep -qx ok "$evidence/library-integrity.txt" || fail 'Disposable library integrity check failed.'
sha256sum "$evidence"/*.png > "$evidence/captures.sha256"

# Optional copied real profile. Mount only a SQLite-consistent backup and its
# settings read-only; never mount the user's live profile or watched media.
if [[ -n ${ZUUNED_UPGRADE_INPUT:-} ]]; then
    upgrade_input=$(realpath -e -- "$ZUUNED_UPGRADE_INPUT")
    [[ -s $upgrade_input/library.db && -f $upgrade_input/Zuuned.conf ]] || fail 'Upgrade input requires library.db and Zuuned.conf.'
    [[ $(sqlite3 -readonly "$upgrade_input/library.db" 'PRAGMA user_version;') == 5 ]] || fail 'This upgrade rehearsal expects a v5 library.'
    export XDG_CONFIG_HOME="$work/upgrade-config" XDG_DATA_HOME="$work/upgrade-data" XDG_CACHE_HOME="$work/upgrade-cache" XDG_STATE_HOME="$work/upgrade-state"
    mkdir -p "$XDG_CONFIG_HOME/Zuuned" "$XDG_DATA_HOME/Zuuned/Zuuned" "$XDG_CACHE_HOME" "$XDG_STATE_HOME"
    cp "$upgrade_input/library.db" "$XDG_DATA_HOME/Zuuned/Zuuned/library.db"
    cp "$upgrade_input/Zuuned.conf" "$XDG_CONFIG_HOME/Zuuned/Zuuned.conf"
    start_app upgraded-profile --page settings --settings-tab Health
    sleep 4
    capture 08-upgraded-profile
    stop_app
    upgrade_db="$XDG_DATA_HOME/Zuuned/Zuuned/library.db"
    [[ $(sqlite3 "$upgrade_db" 'PRAGMA user_version;') == 9 ]] || fail 'Copied real profile did not migrate to v9.'
    [[ $(sqlite3 "$upgrade_db" 'PRAGMA integrity_check;') == ok ]] || fail 'Upgraded library integrity check failed.'
    [[ -s $upgrade_db.v5-backup ]] || fail 'Actual upgrade did not create its v5 rollback backup.'
    cmp <(sqlite3 -readonly "$upgrade_input/library.db" .dump) <(sqlite3 -readonly "$upgrade_db.v5-backup" .dump) \
        || fail 'Rollback backup does not preserve the full copied input database.'
    while IFS= read -r table; do
        [[ $table =~ ^[a-z_]+$ ]] || fail 'Unexpected profile table name.'
        # Offline provider retries may change bookkeeping. Compare every old
        # column containing identity, art, order, playback or library content.
        columns=$(sqlite3 -readonly "$upgrade_input/library.db" "SELECT group_concat('\"'||name||'\"',',') FROM pragma_table_info('$table') WHERE NOT ('$table'='artists' AND name IN ('fetch_attempts','last_fetch_at')) AND NOT ('$table'='videos' AND name IN ('lookup_attempts','last_lookup_at'));")
        changes=$(sqlite3 "$upgrade_db" "ATTACH '$upgrade_input/library.db' AS previous; SELECT (SELECT count(*) FROM (SELECT $columns FROM main.\"$table\" EXCEPT SELECT $columns FROM previous.\"$table\")) + (SELECT count(*) FROM (SELECT $columns FROM previous.\"$table\" EXCEPT SELECT $columns FROM main.\"$table\"));")
        [[ $changes == 0 ]] || fail "Copied profile changed preserved data in $table."
        printf '%s: %s rows preserved\n' "$table" "$(sqlite3 "$upgrade_db" "SELECT count(*) FROM \"$table\";")" >> "$evidence/upgrade-preservation.txt"
    done < <(sqlite3 -readonly "$upgrade_input/library.db" "SELECT name FROM sqlite_schema WHERE type='table' AND name NOT LIKE 'sqlite_%' ORDER BY name;")
    settings_entries() {
        awk '/^\[/ { section=$0; next } /^[[:space:]]*[#;]/ { next } /^[^=]+=/ { print section "\t" $0 }' "$1"
    }
    settings_entries "$XDG_CONFIG_HOME/Zuuned/Zuuned.conf" > "$work/upgraded-settings-entries"
    while IFS= read -r setting; do
        grep -Fxq -- "$setting" "$work/upgraded-settings-entries" || fail 'An existing copied setting changed value or group during upgrade.'
    done < <(settings_entries "$upgrade_input/Zuuned.conf")
    printf 'PASS: copied v5 profile migrated to v8; full rollback backup, old library data and settings preserved.\n' >> "$evidence/upgrade-preservation.txt"
    sha256sum "$evidence"/*.png > "$evidence/captures.sha256"
fi
[[ $(grep -Ec '^(folder|playlist|report): PASS; successful response' "$evidence/portal-picker-checks.txt") == 3 ]] || fail 'Not all three desktop picker paths passed.'
printf 'PASS: real desktop portal directory OpenFile, ordinary OpenFile and SaveFile, with app/backend PIDs and exact selected URIs verified.\n' >> "$evidence/portal-picker-checks.txt"
cat > "$evidence/PASS.txt" <<'RESULT'
PASS: actual AppImage identification and extraction; no system Qt/mpv/FFmpeg;
both --version entry points leave their empty HOME/XDG profile untouched;
bundled media-tool encode/decode; actual fresh onboarding and Skip; empty and
fixture-backed Appearance; desktop folder chooser and native FLAC import with disc metadata; persisted
halftone launch and native print-cache output; normal shutdown; SQLite integrity.
Every GUI launch creates a private native session log with its startup/build
line, retained after shutdown; observed files respect the size/count bounds.
Real desktop portal folder/open/save requests and GTK-owned chooser windows;
playlist file selection/import; actual Health export and desktop SaveFile produce a private bounded
debug report containing the candidate revisions and captured native startup.
All candidate launches used software OpenGL on Xvfb, null audio, isolated XDG
profiles and no USB/network. Screenshots are retained for visual review.
Not certified here: physical audio/GPU, host desktop integration, USB/hotplug,
device transfers, live providers or other desktops' portal backends. A copied-profile
upgrade is certified only when upgrade-preservation.txt also reports PASS.
RESULT
cat "$evidence/PASS.txt"
