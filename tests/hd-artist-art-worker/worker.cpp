#include "DeviceWorker.h"
#include "artwork/HdArtistArtStore.h"
#include <QCoreApplication>
#include <QImage>
#include <QDir>
#include <QTemporaryDir>
#include <cstdio>
#include <cstring>
extern "C" {
#include "zune.h"
}
// No DeviceService or live handle exists. Every reachable device call is wrapped.
struct HdArtworkWorkerTestAccess {
    static void attach(DeviceWorker &w) { w.m_dev = reinterpret_cast<ZuneDevice *>(quintptr(1)); w.setSpokenGeneration(7); }
};
namespace {
DeviceWorker *worker;
ZuneDeviceFamily family;
bool aborted, bindingWrite, bindingUncertain, clean, readyWanted, changeGeneration;
bool failLink, failAlbum, failCheckedInventory;
ZuneHDArtStatus serveStatus, bindStatus;
int served, ensured, ordinaryArtists, albums, links, severed, clears, reads;
QList<quint32> linkArtists;
QVariantList reports, progresses;
int okCount, failCount;
QString emittedToken;
void reset(DeviceWorker &w) {
    worker=&w; HdArtworkWorkerTestAccess::attach(w); family=ZUNE_FAMILY_PAVO;
    aborted=bindingWrite=bindingUncertain=changeGeneration=failLink=failAlbum=failCheckedInventory=false;
    clean=readyWanted=true; serveStatus=ZUNE_HD_ART_PARTIAL; bindStatus=ZUNE_HD_ART_OK;
    served=ensured=ordinaryArtists=albums=links=severed=clears=reads=0;
    okCount=failCount=-1; reports.clear(); progresses.clear(); linkArtists.clear(); emittedToken.clear();
}
}
extern "C" {
ZuneDeviceFamily __wrap_zune_get_family(ZuneDeviceHandle) { return family; }
const char *__wrap_zune_get_serial(ZuneDeviceHandle) { return "fixture"; }
int __wrap_zune_is_aborted(ZuneDeviceHandle) { return aborted; }
void __wrap_zune_clear_abort(ZuneDeviceHandle) { ++clears; aborted=false; }
void __wrap_zune_sever(ZuneDeviceHandle) { ++severed; }
uint16_t __wrap_zune_autopsy(ZuneDeviceHandle) { return clean ? 0x2001 : 0x02ff; }
uint32_t __wrap_zune_forge_artist(ZuneDeviceHandle,const char*) { ++ordinaryArtists; return 222; }
ZuneAlbumObject *__wrap_zune_get_albums(ZuneDeviceHandle,int *count) { ++reads; *count=0; return nullptr; }
int __wrap_zune_get_albums_checked(ZuneDeviceHandle,ZuneAlbumObject **out,int *count) { ++reads; *out=nullptr; *count=0; return failCheckedInventory ? -1 : 0; }
void __wrap_zune_free_albums(ZuneAlbumObject *,int) {}
uint32_t __wrap_zune_forge_album(ZuneDeviceHandle,const char*,const char*,const char*,uint32_t*,int) { ++albums; return failAlbum ? 0 : 333; }
int __wrap_zune_rewire_album(ZuneDeviceHandle,uint32_t,const char*,const char*,uint32_t*,int) { return 0; }
int __wrap_zune_link_artist(ZuneDeviceHandle,uint32_t,uint32_t artist) { ++links; linkArtists.append(artist); return failLink ? -1 : 0; }
int __wrap_zune_brand(ZuneDeviceHandle,uint32_t,const uint8_t*,size_t) { return 0; }
ZuneHDArtStatus __wrap_zune_hd_art_ensure_artist(ZuneDeviceHandle,const char*,const char*,ZuneHDArtistResult *r) {
    ++ensured; *r={}; r->handle=bindStatus==ZUNE_HD_ART_OK||bindingWrite ? 777 : 0;
    r->write_attempted=bindingWrite; r->transport_uncertain=bindingUncertain;
    if (changeGeneration) worker->setSpokenGeneration(8);
    return bindStatus;
}
ZuneHDArtStatus __wrap_zune_hd_art_serve(ZuneDeviceHandle dev,const ZuneHDArtConfig *c,
    ZuneHDArtReady ready,void *readyContext,ZuneHDArtProgress progress,void *progressContext,ZuneHDArtResult *r) {
    ++served; *r={}; r->status=serveStatus; r->safe_to_continue=clean;
    r->network_closed=clean; r->transport_uncertain=!clean;
    if (readyWanted) {
        r->ready_called=1;
        const int rc=ready(dev,readyContext);
        if (rc) r->status=ZUNE_HD_ART_IO_ERROR;
    }
    r->asset_count=c->gallery_count+2;
    for (size_t i=0;i<r->asset_count;i++) {
        const auto &a=i==0?c->background:i==1?c->thumbnail:c->gallery[i-2];
        std::snprintf(r->assets[i].guid,sizeof(r->assets[i].guid),"%s",a.guid);
        r->assets[i].requests=i==0||serveStatus==ZUNE_HD_ART_OK;
        r->assets[i].acknowledged=r->assets[i].requests;
    }
    r->biography_acknowledged=serveStatus==ZUNE_HD_ART_OK&&c->biography;
    if(progress) progress(r,progressContext);
    return r->status;
}
}
int main(int argc,char **argv) {
    QCoreApplication app(argc,argv);
    const auto root=qEnvironmentVariable("HD_WORKER_TEST_ROOT");
    if(!root.startsWith("/tmp/zuuned-hd-artist-art-worker-")) return 2;
    QImage picture(480,480,QImage::Format_RGB32); picture.fill(Qt::darkBlue);
    const auto source=root+"/source.png"; if(!picture.save(source)) return 2;
    QString error;
    auto manifest=HdArtistArtStore::prepare(root+"/store",{{"background",source},{"biography","An artist biography."}}, {}, &error);
    if(manifest.isEmpty()||!error.isEmpty()) return 2;
    DeviceWorker w;
    QObject::connect(&w,&DeviceWorker::forgeBatchDone,&app,[&](const QString &token,int ok,int fail){emittedToken=token;okCount=ok;failCount=fail;});
    QObject::connect(&w,&DeviceWorker::hdArtworkDone,&app,[&](const QString &,quint64,const QString &,const QVariantMap &r){reports.append(r);});
    QObject::connect(&w,&DeviceWorker::hdArtworkProgress,&app,[&](const QString &,quint64,const QString &,const QString &,int ack,int total){progresses.append(QVariantMap{{"ack",ack},{"total",total}});});
    const QVariantMap base{{"albumartist","Artist"},{"album","Album"},{"trackIds",QVariantList{11u,12u}},
        {"_forgeRunToken","run"},{"_deviceSerial","fixture"},{"_deviceGeneration",7ull},{"hdArtistArt",manifest}};
    int checks=0, failures=0;
    auto check=[&](bool yes,const char *name){++checks;if(!yes)++failures;std::printf("%s %s\n",yes?"PASS":"FAIL",name);};
    reset(w); auto plain=base;plain.remove("hdArtistArt"); w.doForgeAlbums({plain});
    check(served==0&&ensured==0&&ordinaryArtists==1&&albums==1&&links==3&&okCount==1,"unconfigured music retains ordinary artist/album/track flow");
    reset(w); family=ZUNE_FAMILY_KEEL; w.doForgeAlbums({base});
    check(served==0&&ensured==0&&ordinaryArtists==1&&okCount==1&&reports.size()==1,"Classic bypasses HD API and still organizes music");
    check(!reports.first().toMap().value("delivered").toBool()&&reports.first().toMap().value("status")=="unsupported","Classic reports unsupported without artwork success");
    reset(w); auto stale=base;stale["_deviceGeneration"]=6ull;w.doForgeAlbums({stale});
    check(served+ensured+ordinaryArtists+reads+albums==0&&failCount==1,"stale connection generation touches no hardware");
    reset(w); stale=base;stale["_deviceSerial"]="other";w.doForgeAlbums({stale});
    check(served+ensured+ordinaryArtists+reads+albums==0,"different device serial touches no hardware");
    reset(w); stale=base;stale["_forgeRunToken"]="other-run";w.doForgeAlbums({base,stale});
    check(served+ensured+ordinaryArtists+reads+albums==0&&failCount==2,"mixed run tokens reject whole batch before hardware");
    reset(w); aborted=true;w.doForgeAlbums({base});
    check(served+ensured+ordinaryArtists+albums==0&&clears==0,"existing cancellation is never cleared or retried");
    reset(w); w.doForgeAlbums({base});
    check(served==1&&ensured==1&&ordinaryArtists==0&&albums==1&&okCount==1&&failCount==0,"partial artwork preserves organized music without duplicate fallback");
    check(linkArtists==QList<quint32>{777,777,777},"album and both tracks use ensured artist handle");
    check(reports.size()==1&&!reports[0].toMap().value("delivered").toBool()&&reports[0].toMap().value("assets").toList().size()==2,"partial report retains distinct asset results without false delivery");
    check(progresses.last().toMap().value("ack").toInt()==1&&progresses.last().toMap().value("total").toInt()==3,"progress includes configured biography in expected total");
    reset(w); serveStatus=ZUNE_HD_ART_OK;w.doForgeAlbums({base});
    check(reports[0].toMap().value("delivered").toBool()&&reports[0].toMap().value("biographyAcknowledged").toInt()==1,"full clean result records biography and image acknowledgments");
    reset(w); bindStatus=ZUNE_HD_ART_IDENTITY_CONFLICT;w.doForgeAlbums({base});
    check(ensured==1&&ordinaryArtists==1&&albums==1&&okCount==1&&severed==0,"proven identity conflict falls back once without replacing artist");
    check(!reports[0].toMap().value("delivered").toBool(),"identity conflict cannot be artwork success");
    reset(w); bindStatus=ZUNE_HD_ART_IO_ERROR;bindingWrite=true;auto later=plain;later["albumartist"]="ZZOther";w.doForgeAlbums({base,later});
    check(ordinaryArtists==0&&albums==0&&severed==1&&failCount==2,"attempted unknown artist creation stops all fallback and severs");
    check(reports[0].toMap().value("artistHandle").toUInt()==777&&reports[0].toMap().value("reconnectRequired").toBool(),"unknown creation preserves returned handle and reconnect requirement");
    reset(w); clean=false;readyWanted=false;w.doForgeAlbums({base});
    check(served==1&&ordinaryArtists==0&&albums==0&&severed==1,"uncertain transport without callback prevents ordinary fallback");
    reset(w); changeGeneration=true;w.doForgeAlbums({base});
    check(ensured==1&&albums==0&&ordinaryArtists==0,"generation changing during callback prevents album and track writes");
    reset(w); failLink=true;w.doForgeAlbums({base});
    check(albums==1&&links==1&&ordinaryArtists==0&&failCount==1&&!reports[0].toMap().value("delivered").toBool(),"failed artist link stops remaining links and rejects graph success");
    reset(w); failAlbum=true;w.doForgeAlbums({base});
    check(albums==1&&ordinaryArtists==0&&links==0&&failCount==1,"failed callback album is not repeated by optional-art fallback");
    reset(w); readyWanted=false;w.doForgeAlbums({base});
    check(served==1&&ensured==0&&ordinaryArtists==1&&albums==1&&okCount==1&&!reports[0].toMap().value("delivered").toBool(),"clean timeout before ready preserves music through one ordinary fallback");
    reset(w); auto second=base;second["album"]="Second album";second["trackIds"]=QVariantList{13u};w.doForgeAlbums({base,second});
    check(served==1&&ensured==1&&ordinaryArtists==0&&albums==2&&links==5&&okCount==2,"same artist albums share one catalog session and artist handle");
    reset(w); auto mismatch=base;auto otherManifest=manifest;otherManifest["revision"]="different";mismatch["hdArtistArt"]=otherManifest;w.doForgeAlbums({base,mismatch});
    check(served==0&&ensured==0&&ordinaryArtists==1&&albums==2&&okCount==2&&!reports[0].toMap().value("delivered").toBool(),"conflicting same-artist snapshots disable optional art while preserving music");
    reset(w); auto broken=base;auto brokenManifest=manifest;auto asset=manifest.value("background").toMap();asset["path"]=root+"/missing.jpg";brokenManifest["background"]=asset;broken["hdArtistArt"]=brokenManifest;w.doForgeAlbums({broken});
    check(served==0&&ensured==0&&ordinaryArtists==1&&albums==1&&okCount==1&&!reports[0].toMap().value("delivered").toBool(),"missing immutable artwork is reported without touching HD API or blocking music");
    reset(w); failCheckedInventory=true;w.doForgeAlbums({base});
    check(ensured==1&&reads==1&&ordinaryArtists==0&&albums==0&&links==0&&failCount==1&&!reports[0].toMap().value("delivered").toBool(),"failed checked album inventory never creates albums or falls back after callback");
    std::printf("%d checks, %d failures\n",checks,failures); return failures?1:0;
}
