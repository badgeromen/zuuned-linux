#!/usr/bin/env bash
# Inspect every bundled ELF, including dlopened Qt/QML plugins. Never run the
# candidate or resolve missing dependencies from this machine with ldd.
set -euo pipefail
zuuned_elf_appdir=$(realpath -e -- "${1:?usage: verify-elf-dependencies.sh AppDir}")
zuuned_elf_script_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd -P)
[[ -d $zuuned_elf_appdir/usr/lib ]] || { echo 'Missing bundle library directory.' >&2; exit 1; }
command -v readelf >/dev/null || { echo 'readelf is required for the ELF inventory.' >&2; exit 1; }
declare -A zuuned_host_libraries=()
while IFS= read -r soname; do
    [[ -z $soname || $soname == \#* ]] && continue
    [[ $soname =~ ^[A-Za-z0-9_.+-]+$ ]] || { echo "Invalid host library entry: $soname" >&2; exit 1; }
    zuuned_host_libraries["$soname"]=1
done < "$zuuned_elf_script_dir/appimage-host-libraries.txt"

zuuned_elf_count=0
zuuned_missing_count=0
while IFS= read -r -d '' elf; do
    if ! dynamic=$(LC_ALL=C readelf --dynamic --wide "$elf" 2>/dev/null); then continue; fi
    ((zuuned_elf_count+=1))
    while IFS= read -r soname; do
        [[ -n $soname ]] || continue
        if [[ ! $soname =~ ^[A-Za-z0-9_.+-]+$ ]]; then
            printf 'Invalid dependency name: %s needs %s\n' "${elf#"$zuuned_elf_appdir/"}" "$soname" >&2
            ((zuuned_missing_count+=1))
            continue
        fi
        # AppRun prepends this directory; a sibling merely declaring SONAME is
        # insufficient unless the actual requested filename resolves here.
        library="$zuuned_elf_appdir/usr/lib/$soname"
        if [[ -f $library ]]; then
            target=$(realpath -e -- "$library")
            if [[ $target == "$zuuned_elf_appdir/"* ]] \
                && LC_ALL=C readelf --file-header "$target" >/dev/null 2>&1; then
                continue
            fi
            printf 'Invalid bundled dependency: %s needs %s (not an internal ELF file)\n' \
                "${elf#"$zuuned_elf_appdir/"}" "$soname" >&2
        elif [[ ${zuuned_host_libraries[$soname]:-0} == 1 ]]; then
            continue
        else
            printf 'Missing bundled dependency: %s needs %s\n' \
                "${elf#"$zuuned_elf_appdir/"}" "$soname" >&2
        fi
        ((zuuned_missing_count+=1))
    done < <(sed -n 's/.*(NEEDED).*Shared library: \[\([^]]*\)\].*/\1/p' <<< "$dynamic")
done < <(find "$zuuned_elf_appdir/usr" -type f -print0)

[[ $zuuned_elf_count -gt 0 ]] || { echo 'Bundle contains no readable ELF files.' >&2; exit 1; }
[[ $zuuned_missing_count == 0 ]] || {
    printf 'ELF inventory failed: %s unresolved/invalid dependency edges across %s ELF files.\n' \
        "$zuuned_missing_count" "$zuuned_elf_count" >&2
    exit 1
}
printf 'ELF dependency inventory passed: %s ELF files; only explicit host-platform SONAMEs may be external.\n' "$zuuned_elf_count"
