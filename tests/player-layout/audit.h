#include <QQuickItem>
#include <QTest>
#include <QQmlExpression>
#include <QQmlContext>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QFile>
#include <QScreen>

static QList<QQuickItem*> allItems(QQuickItem *r) {
    QList<QQuickItem*> out{r};
    for(auto *c:r->childItems())out.append(allItems(c));
    return out;
}
static QQuickItem *findItem(QQuickWindow *w,const char *type) {
    for(auto *i:allItems(w->contentItem()))if(QString(i->metaObject()->className()).startsWith(type))return i;
    return nullptr;
}
static QRectF itemRect(QQuickItem *i){return i->mapRectToScene(i->boundingRect());}
static bool onScreen(QQuickWindow *w,QQuickItem *i){
    if(!i||!i->isVisible()||i->opacity()<.1)return false;
    QRectF r=itemRect(i);
    if(r.isEmpty()||!QRectF(0,0,w->width(),w->height()).adjusted(-.25,-.25,.25,.25).contains(r))return false;
    for(auto*p=i->parentItem();p;p=p->parentItem())if(p->clip()&&!itemRect(p).adjusted(-.25,-.25,.25,.25).contains(r))return false;
    return true;
}
static void runLayoutAudit(QQmlApplicationEngine &engine,PlayerService *player,VideoPlayerService *video){
    auto*w=qobject_cast<QQuickWindow*>(engine.rootObjects().first());
    const QString root=qEnvironmentVariable("AUDIT_DIR"), out=root+"/"+qEnvironmentVariable("AUDIT_CASE");
    QDir().mkpath(out);
    w->setProperty("showOnboarding",false);
    w->setProperty("currentPage","videos");
    w->showFullScreen();
    QTest::qWait(1500);
    QJsonArray results;
    auto snap=[&](QString name){
        QTest::qWait(400);
        w->grabWindow().save(out+"/"+name+".png");
        QJsonObject data{{"case",name},{"width",w->width()},{"height",w->height()},{"dpr",w->devicePixelRatio()},{"fullscreen",w->visibility()==QWindow::FullScreen},{"showNowPlaying",w->property("showNowPlaying").toBool()}};
        QJsonArray items;
        for(auto*i:allItems(w->contentItem())){
            QString type=i->metaObject()->className();
            QString text=i->property("text").toString(),glyph=i->property("glyph").toString();
            if(!i->isVisible()||i->opacity()<.1)continue;
            if(!(type.startsWith("QueueRibbonBar")||type.startsWith("NowPlayingIsland")||type.startsWith("VideoPlayerView")||type.startsWith("PageHeaderBand")||type.startsWith("SpokenPlaybackControls")||i->objectName()=="nowPlayingCloseButton"||text=="close"||glyph=="✕"||text=="now playing"||text=="movies"))continue;
            QRectF r=itemRect(i);
            items.append(QJsonObject{{"type",type},{"objectName",i->objectName()},{"text",text},{"glyph",glyph},{"x",r.x()},{"y",r.y()},{"w",r.width()},{"h",r.height()},{"reachable",onScreen(w,i)}});
        }
        data["items"]=items;results.append(data);
    };
    auto expression=[&](QString e){QQmlExpression expr(qmlContext(w),w,e);expr.evaluate();if(expr.hasError())qWarning()<<expr.error();};
    for(QString style:{"disc","vinyl"}){
        expression("Prefs.playerStyle = '"+style+"'");
        for(QString kind:{"music","podcast","audiobook"}){
            QVariantList queue;
            for(int n=0;n<9;++n){QVariantMap row{{"filepath",root+"/sample.wav"},{"title",QString("Layout test %1: %2").arg(n+1).arg(kind)},{"artist","Test artist"},{"album","Test album"},{"libraryId",-1}};
                if(kind!="music"){row["spokenId"]="audit-"+QString::number(n);row["spokenKind"]=kind;}
                queue.append(row);
            }
            player->stop();w->setProperty("showNowPlaying",false);
            player->setQueue(queue,4);QTest::qWait(500);
            snap(style+"-"+kind+"-automatic");
            const bool compact=w->width()<1280 || w->height()<1100;
            auto*ribbon=findItem(w,"QueueRibbonBar");
            results.append(QJsonObject{{"case",style+"-"+kind+"-automatic-policy"},
                {"passed",w->property("ribbonRetired").toBool()==compact
                    && w->property("showNowPlaying").toBool()==compact
                    && (compact ? ribbon->opacity()<.01 : ribbon->opacity()>.99)}});
            w->setProperty("showNowPlaying",true);snap(style+"-"+kind+"-island");
            QTest::keyClick(w,Qt::Key_Escape);QTest::qWait(100);
            results.append(QJsonObject{{"case",style+"-"+kind+"-island-escape"},{"closed",!w->property("showNowPlaying").toBool()}});
            w->setProperty("showNowPlaying",true);QTest::qWait(400);
            auto*island=findItem(w,"NowPlayingIsland");
            QQuickItem*close=nullptr;
            for(auto*i:allItems(island))if(i->objectName()=="nowPlayingCloseButton")close=i;
            results.append(QJsonObject{{"case",style+"-"+kind+"-close-visible"},
                {"passed",onScreen(w,close) && close && itemRect(close).top()<itemRect(island).top()+16}});
            if(close)QTest::mouseClick(w,Qt::LeftButton,Qt::NoModifier,itemRect(close).center().toPoint());
            QTest::qWait(400);
            results.append(QJsonObject{{"case",style+"-"+kind+"-island-close"},{"closed",!w->property("showNowPlaying").toBool()}});
        }
        w->showNormal();w->resize(1600,1200);w->setProperty("showNowPlaying",false);QTest::qWait(400);
        w->resize(1600,720);QTest::qWait(400);
        results.append(QJsonObject{{"case",style+"-resize-playing"},{"passed",w->property("showNowPlaying").toBool()}});
        w->setProperty("showNowPlaying",false);player->next();QTest::qWait(400);
        results.append(QJsonObject{{"case",style+"-closed-stays-closed"},{"passed",!w->property("showNowPlaying").toBool()}});
        w->resize(1600,1200);QTest::qWait(800);
        auto*ribbon=findItem(w,"QueueRibbonBar");
        results.append(QJsonObject{{"case",style+"-resize-expanded"},{"opacity",ribbon->opacity()},{"retired",w->property("ribbonRetired").toBool()},{"width",w->width()},{"height",w->height()},{"passed",!w->property("ribbonRetired").toBool() && ribbon->opacity()>.99}});
        player->playPause();QTest::qWait(200);w->resize(1600,720);QTest::qWait(400);
        results.append(QJsonObject{{"case",style+"-resize-paused"},{"passed",!player->playing() && w->property("showNowPlaying").toBool()}});
        player->stop();w->setProperty("showNowPlaying",false);w->showFullScreen();QTest::qWait(400);
        video->play(root+"/sample.mp4","Video layout test",-1,0);QTest::qWait(500);
        auto*view=findItem(w,"VideoPlayerView");
        QMetaObject::invokeMethod(view,"showControls");snap(style+"-video");
        QTest::keyClick(w,Qt::Key_Escape);QTest::qWait(200);
        results.append(QJsonObject{{"case",style+"-video-escape"},{"closed",!video->active()}});
        video->play(root+"/sample.mp4","Video close test",-1,0);QTest::qWait(200);
        QMetaObject::invokeMethod(view,"showControls");QTest::qWait(300);
        QQuickItem *chip=nullptr;
        for(auto*i:allItems(view))if(i->property("glyph").toString()=="✕")chip=i;
        if(chip)QTest::mouseClick(w,Qt::LeftButton,Qt::NoModifier,itemRect(chip).center().toPoint());
        QTest::qWait(200);
        results.append(QJsonObject{{"case",style+"-video-close"},{"closed",!video->active()}});
        video->stop();w->showNormal();QTest::qWait(200);
        video->play(root+"/sample.mp4","Video fullscreen test",-1,0);QTest::qWait(200);
        QTest::keyClick(w,Qt::Key_F);QTest::qWait(200);
        bool entered=w->visibility()==QWindow::FullScreen;
        QTest::keyClick(w,Qt::Key_F);QTest::qWait(200);
        bool returned=w->visibility()==QWindow::Windowed;
        results.append(QJsonObject{{"case",style+"-video-fullscreen-toggle"},{"entered",entered},{"returned",returned}});
        video->stop();w->showFullScreen();QTest::qWait(200);
        video->stop();
    }
    QFile file(out+"/results.json");if(!file.open(QIODevice::WriteOnly))qFatal("Cannot write audit results");file.write(QJsonDocument(results).toJson());
    player->stop();video->stop();QCoreApplication::quit();
}
