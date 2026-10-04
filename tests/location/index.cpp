#include "library/TrackIdentityIndex.h"
#include <cstdio>

int main() {
    int failed = 0;
    auto check = [&](bool ok, const char *label) {
        printf("  %s %s\n", ok ? "PASS" : "FAIL", label);
        if (!ok) ++failed;
    };
    LibTrack track;
    track.id = 1; track.title = QStringLiteral("The Song");
    track.album = QStringLiteral("Studio"); track.artist = QStringLiteral("Performer");
    track.albumartist = QStringLiteral("Album Owner");
    TrackIdentityIndex index;
    check(index.replaceRows({track}) && index.contains("PERFORMER", "studio", "the song")
          && index.contains("album owner", "Studio", "The Song"),
          "case-insensitive triples recognize both artist fields");
    check(!index.contains("Performer", "Live", "The Song")
          && !index.contains("Other Performer", "Studio", "The Song")
          && !index.contains("Performer", "Studio", "Another Song"),
          "title alone and mismatched album or artist never match");
    check(!index.contains("", "Studio", "The Song")
          && !index.contains("Performer", "", "The Song")
          && !index.contains("Performer", "Studio", "  "),
          "missing or blank query fields are not wildcards");
    LibTrack incomplete = track;
    incomplete.album.clear();
    TrackIdentityIndex partial;
    partial.replaceRows({incomplete});
    check(!partial.contains("Performer", "Studio", "The Song"),
          "missing local album does not invent an album match");
    incomplete = track; incomplete.artist.clear(); incomplete.albumartist.clear();
    partial.replaceRows({incomplete});
    check(!partial.contains("Unknown Artist", "Studio", "The Song"),
          "placeholder artist labels do not fill absent local metadata");
    incomplete = track; incomplete.artist.clear();
    partial.replaceRows({incomplete});
    check(partial.contains("Album Owner", "Studio", "The Song"),
          "album artist alone remains a valid complete identity");
    incomplete.artist = QStringLiteral("Unknown Artist");
    partial.replaceRows({incomplete});
    check(!partial.contains("Unknown Artist", "Studio", "The Song")
          && partial.contains("Album Owner", "Studio", "The Song"),
          "unknown artist placeholders cannot create aliases but a real album artist still can");
    incomplete.album = QStringLiteral("Unknown Album");
    partial.replaceRows({incomplete});
    check(!partial.contains("Album Owner", "Unknown Album", "The Song"),
          "unknown album placeholders cannot create confident membership");
    LibTrack collision = track;
    collision.artist = QStringLiteral("a\nb"); collision.album = QStringLiteral("c");
    collision.albumartist.clear(); collision.title = QStringLiteral("d");
    partial.replaceRows({collision});
    check(partial.contains("a\nb", "c", "d") && !partial.contains("a", "b\nc", "d"),
          "embedded separators cannot collide across identity fields");
    LibTrack duplicate = track; duplicate.id = 2; duplicate.filepath = QStringLiteral("/other-format.flac");
    check(!index.replaceRows({track, duplicate}) && !index.replaceRows({duplicate}),
          "adding or removing an equivalent file preserves existing membership");
    duplicate.genre = QStringLiteral("Another genre"); duplicate.year = 2026;
    check(!index.replaceRows({duplicate}), "unrelated metadata does not invalidate membership");
    duplicate.title = QStringLiteral("Renamed Song");
    check(index.replaceRows({duplicate}) && !index.contains("Performer", "Studio", "The Song")
          && index.contains("Performer", "Studio", "Renamed Song"),
          "same-count metadata edits replace old identities");
    check(index.replaceRows({}) && !index.contains("Performer", "Studio", "Renamed Song")
          && !index.replaceRows({}), "removing the last copy removes membership once");
    printf("Track identity index: %s (%d failures)\n", failed ? "FAILED" : "ALL PASS", failed);
    return failed ? 1 : 0;
}
