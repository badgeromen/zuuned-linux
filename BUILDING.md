# Building ZuunedLinux

This guide produces either the native development executable or the complete
x86_64 tester bundle. Run all commands from the repository root unless a step
says otherwise.

## Get the source

ZuunedLinux pins `libzune` as a Git submodule. A checkout without that submodule
cannot build the device stack.

```bash
git clone --recurse-submodules https://github.com/badgeromen/zuuned-linux.git
cd zuunedlinux
git submodule status libzune
```

For an existing checkout:

```bash
git submodule update --init --recursive
```

Do not update the libzune pin merely to make a release build. The application
commit records the protocol revision it was tested with.

## Install native build dependencies

Qt 6.8 or newer is required. The application also links libusb, libgcrypt,
SQLite, mpv, FFmpeg and LAME. The complete Arch and Debian package commands are
kept in the [main README](README.md#build).

## Build the native application

```bash
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel 4
./build/zuuned
```

CMake builds the pinned `libzune.a` automatically. If the linker reports an
unrecognized file format after moving the checkout between macOS and Linux,
remove the stale submodule objects and rebuild:

```bash
make -C libzune clean
cmake --build build --parallel 4
```

The native outputs are:

- `build/zuuned` — the Qt application
- `build/librarytool` — local-library/scanner diagnostics
- `build/playertool` — playback diagnostics
- `build/transcodetool` — media conversion diagnostics

This build uses the current machine's libraries. It is suitable for development
and hardware checks on that machine, but it is not the distributable AppImage.

## Build the tester AppImage

The release workflow builds inside pinned Debian 13 images. This gives the
package a known Qt 6.8.2 and glibc baseline while keeping build dependencies off
the development desktop. Docker is used only to compile and inspect the bundle:
the containers receive no USB device, personal library, audio device or desktop
session. Zune hardware testing still runs on a physical host.

Install Docker first. If the current account can access Docker directly, omit
`sudo` from the following commands. The checked-in images use tester UID/GID
`1000:1000`; if the checkout owner differs, follow the UID instructions in the
detailed container README rather than changing only `--user` below.

Build the two images whenever either Dockerfile or the runtime smoke script
changes:

```bash
sudo docker build --platform linux/amd64 \
  -f packaging/containers/Dockerfile.debian13 \
  -t zuuned-tester-debian13 packaging/containers

sudo docker build --platform linux/amd64 \
  -f packaging/containers/Dockerfile.runtime-debian13 \
  -t zuuned-runtime-debian13 packaging/containers
```

The package must come from committed, clean application and submodule states.
This prevents an AppImage whose filename identifies one revision while its
contents came from another.

```bash
test -z "$(git status --porcelain --untracked-files=normal --ignore-submodules=none)"
test -z "$(git -C libzune status --porcelain --untracked-files=normal)"

repo=$(pwd)
release_root=$(mktemp -d /tmp/zuuned-release-XXXXXXXX)
snapshot="$release_root/source"

node packaging/containers/snapshot.mjs "$snapshot"
grep -qx 'export ZUUNED_SOURCE_DIRTY=false' "$snapshot/snapshot.env"
grep -qx 'export ZUUNED_LIBZUNE_DIRTY=false' "$snapshot/snapshot.env"

mkdir -p packaging/appimage-tools
tools=$(realpath packaging/appimage-tools)

sudo docker run --rm --platform linux/amd64 --user 1000:1000 \
  --env LANG=C.UTF-8 --env ZUUNED_APPIMAGE_TOOLS=/tools \
  --mount "type=bind,src=$snapshot,dst=/work" \
  --mount "type=bind,src=$tools,dst=/tools" \
  --workdir /work zuuned-tester-debian13 \
  bash -lc 'source /work/snapshot.env; bash packaging/build-appimage.sh'

mkdir -p "$repo/build-appimage/dist"
cp -a "$snapshot/build-appimage/dist/." "$repo/build-appimage/dist/"
find "$repo/build-appimage/dist" -maxdepth 1 -type f -printf '%f\n' | sort
```

The first package run may download the AppImage deployment tools. Their hashes
are pinned in `packaging/appimage-tools.lock`; the builder verifies cached and
downloaded copies before executing them.

The output set under `build-appimage/dist/` contains:

- `Zuuned-<version>-g<revision>-<UTC>-x86_64.AppImage`
- the matching `.AppImage.sha256`
- `.AppImage.build.json` with the exact app and libzune revisions
- `.AppImage.environment.txt` with the build environment and glibc requirements
- `.AppImage.tools.lock` with the deployment-tool hashes

Verify the exact candidate from inside its output directory, because the
checksum file contains the AppImage's basename:

```bash
cd build-appimage/dist
candidate=$(find . -maxdepth 1 -type f -name 'Zuuned-*.AppImage' \
  -printf '%T@ %f\n' | sort -nr | head -1 | cut -d' ' -f2-)
sha256sum --check "$candidate.sha256"
chmod +x "$candidate"
"./$candidate" --version
cd ../..
```

## Run the isolated package gate

This launches the actual AppImage in Xvfb with software OpenGL, disposable XDG
profiles, no network and an empty USB view. It checks packaged libraries,
onboarding, local music import, artwork rendering, desktop file pickers,
playlist import, diagnostics export and normal shutdown. It cannot certify real
GPU/audio/USB behavior.

```bash
candidate=$(realpath "build-appimage/dist/$candidate")
runtime_evidence="$release_root/runtime"
mkdir -p "$runtime_evidence"

sudo docker run --rm --platform linux/amd64 --user 1000:1000 \
  --network none --shm-size 512m \
  --mount type=tmpfs,dst=/sys/bus/usb/devices \
  --mount "type=bind,src=$candidate,dst=/candidate.AppImage,readonly" \
  --mount "type=bind,src=$runtime_evidence,dst=/evidence" \
  zuuned-runtime-debian13 /candidate.AppImage /evidence

cat "$runtime_evidence/latest-runtime.txt"
```

The detailed evidence contract and failure interpretation live in
[`packaging/containers/README.md`](packaging/containers/README.md). A passing
container gate does not replace playback, transfer and USB recovery tests on a
physical machine.

## Run the AppImage

```bash
chmod +x Zuuned-*.AppImage
./Zuuned-*.AppImage
```

If FUSE is unavailable:

```bash
APPIMAGE_EXTRACT_AND_RUN=1 ./Zuuned-*.AppImage
```

If a file manager says “archive not supported,” it has handed the executable to
an archive application instead of running it. Use the commands above or enable
the file's executable permission in its Properties dialog. Code inside an
AppImage cannot change a desktop's file association before the package starts.

Install the two USB rules only on the physical test system; the application can
also install these through its USB permission card:

```bash
sudo install -m 644 packaging/68-zuuned.rules /etc/udev/rules.d/68-zuuned.rules
sudo install -m 644 packaging/68-zuuned.rules /etc/udev/rules.d/72-zuuned.rules
sudo udevadm control --reload-rules
```

Replug the Zune after installing the rules. See
[`packaging/TESTER_INSTALL.md`](packaging/TESTER_INSTALL.md) for tester-facing
launch, diagnostics and reporting instructions.
