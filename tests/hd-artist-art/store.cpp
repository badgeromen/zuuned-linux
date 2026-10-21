#include "artwork/HdArtistArtStore.h"
#include <QCoreApplication>
#include <QFile>
#include <QImage>
#include <QTemporaryDir>
#include <QDir>
#include <iostream>
static int checks;
#define CHECK(x) do { ++checks; if (!(x)) { std::cerr << "FAIL line " << __LINE__ << ": " << #x << '\n'; return 1; } } while (0)
int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    QTemporaryDir temp;
    CHECK(temp.isValid());
    QImage source(900,600,QImage::Format_ARGB32); source.fill(QColor(30,90,140,100));
    QString input=temp.path()+"/original.png";
    CHECK(source.save(input));
    QFile original(input); CHECK(original.open(QIODevice::ReadOnly)); const auto originalBytes=original.readAll(); original.close();
    QString error;
    QVariantMap draft{{"background",input},{"biography",QStringLiteral("A band.\nUnicode: Björk 🎵")},{"gallery",QVariantList{input}}};
    auto first=HdArtistArtStore::prepare(temp.path()+"/store",draft,{},&error);
    CHECK(error.isEmpty()); CHECK(!first.isEmpty());
    CHECK(first.value("background").toMap().value("width")==480);
    CHECK(first.value("background").toMap().value("height")==480);
    CHECK(first.value("thumbnail").toMap().value("width")==160);
    CHECK(first.value("thumbnail").toMap().value("height")==120);
    CHECK(first.value("gallery").toList().size()==1);
    auto snap=HdArtistArtStore::readSnapshot(first,&error);
    CHECK(!snap.isEmpty()); CHECK(error.isEmpty());
    CHECK(snap.value("background").toMap().value("jpeg").toByteArray().size()>0);
    CHECK(original.open(QIODevice::ReadOnly)); CHECK(original.readAll()==originalBytes); original.close();
    // Moving/deleting sources cannot break the saved immutable snapshot.
    CHECK(QFile::remove(input)); CHECK(!HdArtistArtStore::readSnapshot(first,&error).isEmpty());
    QVariantMap repeat{{"expectedRevision",first.value("revision")},{"background",first.value("background").toMap().value("original")},
        {"thumbnail",first.value("thumbnail").toMap().value("original")}, {"gallery",QVariantList{first.value("gallery").toList().first().toMap().value("original")}}, {"biography","changed"}};
    auto next=HdArtistArtStore::prepare(temp.path()+"/store",repeat,first,&error);
    CHECK(!next.isEmpty()); CHECK(next.value("catalogGuid")==first.value("catalogGuid"));
    CHECK(next.value("revision")!=first.value("revision"));
    CHECK(next.value("background").toMap().value("id")==first.value("background").toMap().value("id"));
    CHECK(next.value("gallery").toList().first().toMap().value("id")==first.value("gallery").toList().first().toMap().value("id"));
    CHECK(HdArtistArtStore::prepare(temp.path()+"/store",repeat,next,&error).isEmpty()); CHECK(!error.isEmpty());
    auto invalid=repeat; invalid["gallery"]=QVariantList{input,input,input,input,input,input,input};
    CHECK(HdArtistArtStore::prepare(temp.path()+"/store",invalid,first,&error).isEmpty());
    invalid=repeat; invalid["biography"]=QString(4097,'x');
    CHECK(HdArtistArtStore::prepare(temp.path()+"/store",invalid,first,&error).isEmpty());
    invalid=repeat; invalid["biography"]=QString(QChar(0));
    CHECK(HdArtistArtStore::prepare(temp.path()+"/store",invalid,first,&error).isEmpty());
    invalid=repeat; invalid["background"]="https://example.test/art.jpg";
    CHECK(HdArtistArtStore::prepare(temp.path()+"/store",invalid,first,&error).isEmpty());
    invalid=repeat; invalid["gallery"]=QVariantList{repeat["background"],repeat["background"]};
    CHECK(HdArtistArtStore::prepare(temp.path()+"/store",invalid,first,&error).isEmpty());
    auto changed=first; auto bg=changed.value("background").toMap(); bg["jpegHash"]="bad"; changed["background"]=bg;
    CHECK(HdArtistArtStore::readSnapshot(changed,&error).isEmpty()); CHECK(!error.isEmpty());
    // Extreme aspect inputs must not allocate a KeepAspectRatioByExpanding giant.
    for(const auto size:{QSize(1,16384),QSize(16384,1)}) {
        QImage thin(size,QImage::Format_RGB32); thin.fill(Qt::red); CHECK(thin.save(input));
        auto set=HdArtistArtStore::prepare(temp.path()+"/store",{{"background",input}},{},&error);
        CHECK(!set.isEmpty()); CHECK(!HdArtistArtStore::readSnapshot(set,&error).isEmpty());
    }
    auto cleared=HdArtistArtStore::prepare(temp.path()+"/store",{{"expectedRevision",first.value("revision")} },first,&error);
    CHECK(cleared.isEmpty()); CHECK(error.isEmpty());
    CHECK(!HdArtistArtStore::readSnapshot(first,&error).isEmpty());
    std::cout << checks << " HD artist artwork checks passed\n";
}
