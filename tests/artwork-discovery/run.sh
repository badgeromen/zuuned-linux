#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/../.."
fixture_dir=$(mktemp -d /tmp/zuuned-music-identity.XXXXXX)
trap 'rm -rf "$fixture_dir"' EXIT
read -r -a qt_flags <<< "$(pkg-config --cflags --libs Qt6Core Qt6Gui Qt6Network Qt6Concurrent)"
qt_libexec=$(pkg-config --variable=libexecdir Qt6Core)
"$qt_libexec/moc" src/library/ArtistImageService.h -o "$fixture_dir/moc_artist.cpp"
"$qt_libexec/moc" src/library/OnlineAlbumArtService.h -o "$fixture_dir/moc_online_album.cpp"
"$qt_libexec/moc" src/library/AlbumArtService.h -o "$fixture_dir/moc_album.cpp"
${CXX:-c++} -std=c++20 -O1 -g -Wall -Wextra -fPIC -Isrc -Isrc/library -Itranscode \
    tests/artwork-discovery/backend.cpp src/library/MusicIdentityClient.cpp \
    src/library/AlbumArtService.cpp src/library/OnlineAlbumArtService.cpp "$fixture_dir/moc_album.cpp" "$fixture_dir/moc_online_album.cpp" \
    src/library/ArtistImageService.cpp "$fixture_dir/moc_artist.cpp" \
    "${qt_flags[@]}" -o "$fixture_dir/music-identity"
XDG_CACHE_HOME="$fixture_dir/cache" XDG_CONFIG_HOME="$fixture_dir/config" \
    "$fixture_dir/music-identity"
