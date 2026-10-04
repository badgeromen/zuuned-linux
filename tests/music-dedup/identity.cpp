#include "library/MusicIdentity.h"
#include "library/TrackIdentityIndex.h"
#include <cstdio>

int main() {
    using namespace MusicIdentity;
    int checks = 0, failed = 0;
    auto check = [&](bool ok, const char *label) {
        ++checks; if (!ok) ++failed;
        printf("%s %s\n",ok ? "PASS" : "FAIL",label);
    };
    const Track first{"Owner","Double","Intro",1,1}, second{"Owner","Double","Intro",2,1};
    Index index;
    index.add(first,101); index.add(second,102);
    check(index.size() == 2 && index.resolve(first).itemId == 101 && index.resolve(second).itemId == 102,
          "separate discs resolve to separate objects");
    check(index.resolve(first).kind == MatchKind::Exact,"known matching disc and track is exact");
    check(index.resolve({"OWNER"," Double ","intro",1,1}).itemId == 101,"normalization shared across callers");
    check(index.resolve({"Owner","Double","Intro",3,1}).kind == MatchKind::Missing,"known conflicting discs never match");
    check(index.resolve({"Owner","Double","Intro",1,2}).kind == MatchKind::Missing,"known conflicting tracks never match");
    check(index.resolve({"Owner","Double","Intro",0,1}).kind == MatchKind::Ambiguous,"unknown disc is not disc one");
    check(index.resolve({"Owner","Double","Intro",1,0}).kind == MatchKind::UniqueLegacy,"known disc can distinguish unknown track");
    index.add(first,103);
    check(index.resolve(first).kind == MatchKind::Ambiguous,"duplicate exact objects never overwrite candidates");
    check(index.resolve({"Wrong","Double","Intro",2,1},{},true).kind == MatchKind::Missing,
          "artist fallback cannot contradict a known artist");
    Index legacy;
    const Track missing{"Owner","Double","Intro",0,0};
    legacy.add(missing,200);
    check(legacy.resolve(first).kind == MatchKind::UniqueLegacy,"one legacy candidate can match one source identity");
    check(legacy.resolve(first,{first,second}).kind == MatchKind::Ambiguous,
          "source disc siblings make missing device disc ambiguous");
    check(legacy.resolve(first,{first,{"Owner","Double","Intro",1,2}}).kind == MatchKind::Ambiguous,
          "source repeated titles on same disc need track evidence");
    check(legacy.resolve(first,{first,first}).matched(),"identical source copies are not competing identities");
    check(legacy.resolve(first,{first,{"Other","Double","Intro",2,1}}).matched(),"different artists are not source siblings");
    check(legacy.resolve({"Owner","", "Intro",1,1}).kind == MatchKind::Missing,"missing album is not wildcard");
    check(legacy.resolve({"Unknown Artist","Double","Intro",1,1}).kind == MatchKind::Missing,"placeholder artist is not identity");
    QVariantMap row{{"artist","Performer"},{"albumartist","Owner"},{"album","Double"},{"title","Intro"},
        {"discNumber",2},{"trackNumber",2297},{"trackNumberReliable",false}};
    auto parsed = fromMap(row);
    check(parsed.artist == "owner" && parsed.discNumber == 2 && parsed.trackNumber == 0,
          "map prefers album owner and ignores unreliable classic track slot");
    row.insert("discNumberReliable",false);
    check(fromMap(row).discNumber == 0,"unreliable disc stays unknown");
    row.insert("albumartist","");
    check(fromMap(row).artist == "performer","missing album owner falls back to track artist");
    Index separators;
    Index absentArtist;
    absentArtist.add({"", "Double", "Intro", 1, 1}, 300);
    check(absentArtist.resolve({"", "Double", "Intro", 1, 1}, {}, true).kind == MatchKind::UniqueLegacy,
          "missing artist is never labeled exact even with matching numbers");
    absentArtist.add(first, 301);
    check(absentArtist.resolve({"", "Double", "Intro", 1, 1}, {}, true).kind == MatchKind::Ambiguous,
          "absent-artist fallback considers known and unknown artist candidates together");
    separators.add({"a\tb","c","d",0,0},1);
    check(!separators.resolve({"a","b\tc","d",0,0}).matched(),"structured keys cannot collide at separators");
    LibTrack local;
    local.id=1; local.artist="Performer"; local.albumartist="Owner"; local.album="Double";local.title="Intro";
    local.discNumber=1;local.trackNumber=1;
    TrackIdentityIndex membership;
    membership.replaceRows({local});
    check(membership.contains("Owner","Double","Intro",1,1),"local badge accepts known identity");
    auto other=local;other.id=2;other.discNumber=2;
    check(membership.replaceRows({local,other}) && !membership.contains("Owner","Double","Intro"),
          "local multidisc candidates invalidate title-only badge");
    check(membership.peers(toMap(first)).size()==2,"source peers lookup retains multidisc distinctions");
    auto twin=local;twin.id=3;
    check(!membership.replaceRows({local,other,twin}),"equivalent copies do not cause badge revision churn");
    other.trackNumber=2;
    check(membership.replaceRows({local,other}),"disc and track edits invalidate identity revision");
    printf("Music identity: %d checks, %d failures\n",checks,failed);
    return failed ? 1 : 0;
}
