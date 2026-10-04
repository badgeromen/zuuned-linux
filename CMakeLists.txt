cmake_minimum_required(VERSION 3.21)
project(zuuned VERSION 0.1.1 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Qt6 6.8 REQUIRED COMPONENTS
    Quick Gui Concurrent Network OpenGL DBus QuickControls2 QuickDialogs2 Core5Compat)
find_package(PkgConfig REQUIRED)
pkg_check_modules(LIBUSB REQUIRED libusb-1.0)
pkg_check_modules(SQLITE3 REQUIRED sqlite3)
qt_standard_project_setup(REQUIRES 6.8)
qt_policy(SET QTP0004 NEW)
include(packaging/CheckQmlImports.cmake)

# ---- libzune ----
# Built by its own Makefile (auto-selects the libusb backend on Linux).
# CMake just drives `make` and links the resulting static archive.
set(LIBZUNE_DIR ${CMAKE_SOURCE_DIR}/libzune)
set(LIBZUNE_A ${LIBZUNE_DIR}/libzune.a)

add_custom_target(libzune_build
    COMMAND make -C ${LIBZUNE_DIR}
    BYPRODUCTS ${LIBZUNE_A}
    COMMENT "Building libzune (libusb backend)"
)

# ---- App ----
qt_add_executable(zuuned
    src/main.cpp
    src/ApplicationIdentity.cpp
    src/ApplicationIdentity.h
    src/AppSettings.h
    src/diagnostics/SessionLog.cpp
    src/diagnostics/SessionLog.h
    src/diagnostics/Redactor.cpp
    src/diagnostics/Redactor.h
    src/diagnostics/SupportReport.cpp
    src/diagnostics/SupportReport.h
    src/diagnostics/DiagnosticsService.cpp
    src/diagnostics/DiagnosticsService.h
    src/artwork/PrintRenderer.cpp
    src/artwork/PrintRenderer.h
    src/artwork/ArtworkRequest.cpp
    src/artwork/ArtworkRequest.h
    src/artwork/ArtworkPipeline.cpp
    src/artwork/ArtworkPipeline.h
    src/DeviceService.cpp
    src/DeviceService.h
    src/VideoTitleRepair.cpp
    src/VideoTitleRepair.h
    src/library/VideoIdentity.h
    src/DeviceWorker.cpp
    src/DeviceWorker.h
    src/GrungeMaskProvider.cpp
    src/GrungeMaskProvider.h
    src/TrackModel.cpp
    src/TrackModel.h
    src/TrackImportBatch.h
    src/UsbWatcher.cpp
    src/UsbWatcher.h
    src/LibraryService.cpp
    src/library/ArtworkHttp.cpp
    src/LibraryService.h
    src/library/LibraryTypes.h
    src/library/LibraryDb.cpp
    src/library/LibraryDb.h
    src/library/LibraryScanner.cpp
    src/library/LibraryScanner.h
    src/library/AudioCopyFingerprint.h
    src/library/LocalMusicCopies.h
    src/library/VideoNaming.cpp
    src/library/VideoNaming.h
    src/library/TmdbClient.cpp
    src/library/TmdbClient.h
    src/library/VideoMatcher.cpp
    src/library/VideoMatcher.h
    src/library/VideoBrowserModel.cpp
    src/library/VideoBrowserModel.h
    src/library/FrameThumbnailService.cpp
    src/library/FrameThumbnailService.h
    src/library/LocalTrackModel.cpp
    src/library/LocalTrackModel.h
    src/library/OnlineAlbumArtService.cpp
    src/library/OnlineAlbumArtService.h
    src/library/AlbumArtService.cpp
    src/library/ArtistImageService.cpp
    src/library/ArtistImageService.h
    src/library/MusicIdentityClient.cpp
    src/library/MusicIdentityClient.h
    src/library/AlbumArtService.h
    src/sync/SyncEngine.cpp
    src/sync/SyncEngine.h
    src/sync/SyncQueueModel.cpp
    src/sync/SyncQueueModel.h
)

# The installer and manual command read these exact packaged bytes.
qt_add_resources(zuuned "zuuned_packaging"
    PREFIX "/zuuned/packaging"
    BASE "packaging"
    FILES packaging/68-zuuned.rules)

# The installed icon theme handles menu/file-manager launchers. This compiled
# copy covers direct AppImage launches and the live compositor/taskbar window.
qt_add_resources(zuuned "zuuned_app_identity"
    PREFIX "/zuuned/app"
    BASE "packaging/icons/hicolor/256x256/apps"
    FILES packaging/icons/hicolor/256x256/apps/zuuned.png)

# Must be set BEFORE qt_add_qml_module — the flag is read when the module
# processes the file. After, it's silently ignored and Theme registers as
# a plain type: every Theme.* binding falls back to defaults (white app).
set_source_files_properties(qml/Theme.qml qml/TrayState.qml qml/PhotoTrayState.qml qml/Prefs.qml qml/Gallery.qml PROPERTIES
    QT_QML_SINGLETON_TYPE TRUE
)

# Put the QML files in the module's canonical resource directory. Qt 6.8.2
# treats the QTP0004 subdirectory redirect plus an explicit self-import as
# ambiguous. Aliasing away the source-only qml/ prefix gives implicit and
# explicit imports one location while keeping fonts/images/JS paths relative.
# This glob assigns aliases only; the explicit module inventory below still
# decides which files are compiled and shipped.
file(GLOB_RECURSE zuuned_qml_resources CONFIGURE_DEPENDS
    RELATIVE "${CMAKE_CURRENT_SOURCE_DIR}/qml"
    "${CMAKE_CURRENT_SOURCE_DIR}/qml/*.qml"
    "${CMAKE_CURRENT_SOURCE_DIR}/qml/*.js"
    "${CMAKE_CURRENT_SOURCE_DIR}/qml/fonts/*"
    "${CMAKE_CURRENT_SOURCE_DIR}/qml/images/*")
foreach(resource IN LISTS zuuned_qml_resources)
    set_source_files_properties("qml/${resource}" PROPERTIES QT_RESOURCE_ALIAS "${resource}")
endforeach()

qt_add_qml_module(zuuned
    URI Zuuned
    VERSION 1.0
    NO_GENERATE_EXTRA_QMLDIRS
    QML_FILES
        qml/Main.qml
        qml/Theme.qml
        qml/GradientText.qml
        qml/GrungeGlass.qml
        qml/EnvironmentBackground.qml
        qml/SidebarView.qml
        qml/PivotBar.qml
        qml/DevicePanel.qml
        qml/QueueEntryView.qml
        qml/VideoTitleRepairSheet.qml
        qml/MusicPage.qml
        qml/TrackListRow.qml
        qml/OnComputerBadge.qml
        qml/ArtistCard.qml
        qml/AlbumCard.qml
        qml/GenreTile.qml
        qml/MusicBreadcrumbBar.qml
        qml/ArtistDetail.qml
        qml/AlbumDetail.qml
        qml/DetailActionButton.qml
        qml/AlbumArt.qml
        qml/ArtworkImage.qml
        qml/ArtworkStyleSettings.qml
        qml/AlbumArtPlaceholder.qml
        qml/ZuneSearchField.qml
        qml/DeviceListPage.qml
        qml/LocalLibraryPage.qml
        qml/SplashView.qml
        qml/OnboardingView.qml
        qml/SettingsPage.qml
        qml/DiagnosticsSettings.qml
        qml/ToastHost.qml
        qml/CurvedText.qml
        qml/SpinningDiscView.qml
        qml/VinylRecordView.qml
        qml/QueueRibbonBar.qml
        qml/VolumeSlim.qml
        qml/CassetteTile.qml
        qml/ZuneScrollBar.qml
        qml/MiniPlayerWindow.qml
        qml/OnDeviceBadge.qml
        qml/ZuneMenuItem.qml
        qml/PosterCard.qml
        qml/LibraryVideosView.qml
        qml/VideoMovieDetail.qml
        qml/VideoSeriesDetail.qml
        qml/TmdbLookupSheet.qml
        qml/DeviceVideosPage.qml
        qml/CapacityRejectSheet.qml
        qml/RenameSheet.qml
        qml/VideoPlayerView.qml
        qml/OrbitalControls.qml
        qml/LibraryPhotosView.qml
        qml/PhotoGalleryView.qml
        qml/DevicePicturesPage.qml
        qml/PlaylistsPage.qml
        qml/SyncCompleteSheet.qml
        qml/SplitAddButton.qml
        qml/QuickPicker.qml
        qml/ZuneMenu.qml
        qml/TrayState.qml
        qml/Gallery.qml
        qml/GalleryView.qml
        qml/GenreDetail.qml
        qml/CustomizeSheet.qml
        qml/CustomizeArtwork.qml
        qml/CustomizeField.qml
        qml/MusicIdentity.js
        qml/PageHeaderBand.qml
        qml/Prefs.qml
        qml/PlaylistTray.qml
        qml/PhotoTrayState.qml
        qml/PhotoAlbumTray.qml
        qml/AddToPlaylistMenu.qml
        qml/EditTrackSheet.qml
        qml/DragGhost.qml
        qml/AlphabetRail.qml
        qml/NowPlayingIsland.qml
    RESOURCES
        qml/images/zuuned.png
        qml/fonts/PermanentMarker-Regular.ttf
        qml/fonts/SpecimenAlien-Regular.ttf
)

add_dependencies(zuuned libzune_build)

# Re-evaluate source identification at every build, including after a commit
# without a CMake reconfigure. The helper only replaces changed output files.
set(ZUUNED_SOURCE_REVISION "" CACHE STRING "Full app revision for a source archive without Git metadata")
set(ZUUNED_LIBZUNE_REVISION "" CACHE STRING "Full libzune revision for a source archive without Git metadata")
set(ZUUNED_SOURCE_DIRTY "" CACHE STRING "Archive app state: true or false; unset means unknown")
set(ZUUNED_LIBZUNE_DIRTY "" CACHE STRING "Archive libzune state: true or false; unset means unknown")
add_custom_target(zuuned_build_info
    COMMAND ${CMAKE_COMMAND}
        "-DSOURCE_DIR=${CMAKE_SOURCE_DIR}"
        "-DOUTPUT_DIR=${CMAKE_CURRENT_BINARY_DIR}/generated"
        "-DPROJECT_VERSION_VALUE=${PROJECT_VERSION}"
        "-DQT_VERSION_VALUE=${Qt6_VERSION}"
        "-DZUUNED_SOURCE_REVISION=${ZUUNED_SOURCE_REVISION}"
        "-DZUUNED_LIBZUNE_REVISION=${ZUUNED_LIBZUNE_REVISION}"
        "-DZUUNED_SOURCE_DIRTY=${ZUUNED_SOURCE_DIRTY}"
        "-DZUUNED_LIBZUNE_DIRTY=${ZUUNED_LIBZUNE_DIRTY}"
        -P "${CMAKE_SOURCE_DIR}/packaging/WriteBuildInfo.cmake"
    BYPRODUCTS
        "${CMAKE_CURRENT_BINARY_DIR}/generated/ZuunedBuildInfo.h"
        "${CMAKE_CURRENT_BINARY_DIR}/generated/zuuned-build.json"
    VERBATIM
)
add_dependencies(zuuned zuuned_build_info)
target_include_directories(zuuned PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/generated")

target_include_directories(zuuned PRIVATE src src/artwork src/diagnostics src/library src/sync ${LIBZUNE_DIR}/include ${LIBUSB_INCLUDE_DIRS})
target_link_libraries(zuuned PRIVATE
    Qt6::Quick Qt6::Gui Qt6::Concurrent Qt6::Network
    ${LIBZUNE_A}
    gcrypt
    ${LIBUSB_LIBRARIES}
    ${SQLITE3_LIBRARIES}
    zuuned_transcode
)

# ═══════════════════════════════════════════════════════════════════
# Phase 2: transcode layer (transcode/) — shared C logic with the mac
# app, built against SYSTEM FFmpeg + LAME. Standalone static lib +
# the transcodetool CLI gate. NOT yet linked into the zuuned app
# (that lands with Phase 3's library scanner).
# ═══════════════════════════════════════════════════════════════════
pkg_check_modules(FFMPEG REQUIRED
    libavformat libavcodec libavutil libswresample libswscale)

add_library(zuuned_transcode STATIC
    transcode/libav_transcode.c
    transcode/id3_rewrite.c
)
target_include_directories(zuuned_transcode PUBLIC transcode ${FFMPEG_INCLUDE_DIRS})
target_link_libraries(zuuned_transcode PUBLIC ${FFMPEG_LIBRARIES} mp3lame m)

add_executable(transcodetool transcode/transcodetool.c)
target_link_libraries(transcodetool PRIVATE zuuned_transcode)

# ═══════════════════════════════════════════════════════════════════
# Phase 3: library core CLI gate (librarytool) — scans a folder into a
# temp DB via the same LibraryDb/LibraryScanner the app uses.
# ═══════════════════════════════════════════════════════════════════
qt_add_executable(librarytool
    src/library/librarytool.cpp
    src/library/LibraryDb.cpp
    src/library/LibraryScanner.cpp
    src/library/VideoNaming.cpp
    src/library/TmdbClient.cpp
    src/library/VideoMatcher.cpp
    src/library/VideoMatcher.h
)
add_dependencies(librarytool libzune_build)
target_include_directories(librarytool PRIVATE
    src/library ${LIBZUNE_DIR}/include ${SQLITE3_INCLUDE_DIRS})
target_link_libraries(librarytool PRIVATE
    Qt6::Core Qt6::Concurrent Qt6::Network
    ${SQLITE3_LIBRARIES}
    ${LIBZUNE_A}
    gcrypt
    ${LIBUSB_LIBRARIES}
    ${FFMPEG_LIBRARIES}
)

# ═══════════════════════════════════════════════════════════════════
# Phase 4: music playback core (src/player/) — MpvController (libmpv
# audio context + event thread) and PlayerService (QML singleton,
# PlayerService.swift port). playertool is the headless CLI gate.
# ═══════════════════════════════════════════════════════════════════
pkg_check_modules(MPV REQUIRED mpv)

target_sources(zuuned PRIVATE
    src/player/MpvController.cpp
    src/player/MpvController.h
    src/player/MprisService.cpp
    src/player/MprisService.h
    src/player/PlayerService.cpp
    src/player/PlayerService.h
    # Phase 7: video player (render API + binge queue service)
    src/player/MpvVideoItem.cpp
    src/player/MpvVideoItem.h
    src/player/VideoPlayerService.cpp
    src/player/VideoPlayerService.h
)
target_include_directories(zuuned PRIVATE src/player ${MPV_INCLUDE_DIRS})
target_link_libraries(zuuned PRIVATE ${MPV_LIBRARIES} Qt6::OpenGL Qt6::DBus)

qt_add_executable(playertool
    src/player/playertool.cpp
    src/player/MpvController.cpp
    src/player/PlayerService.cpp
)
target_include_directories(playertool PRIVATE src/player ${MPV_INCLUDE_DIRS})
target_link_libraries(playertool PRIVATE
    Qt6::Core Qt6::Qml
    ${MPV_LIBRARIES}
)

# ── Install (E3 ship mechanics) ──
include(GNUInstallDirs)
install(TARGETS zuuned RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(FILES LICENSE COPYING.md
        DESTINATION ${CMAKE_INSTALL_DATADIR}/licenses/zuuned)
install(FILES "${CMAKE_CURRENT_BINARY_DIR}/generated/zuuned-build.json"
        DESTINATION ${CMAKE_INSTALL_DATADIR}/zuuned)
install(FILES packaging/zuuned.desktop
        DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)
install(FILES packaging/icons/zuuned.svg
        DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/scalable/apps)
foreach(size 16 24 32 48 64 128 256 512)
    install(FILES packaging/icons/hicolor/${size}x${size}/apps/zuuned.png
            DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/${size}x${size}/apps)
endforeach()
# udev rules are architecture independent: GNUInstallDirs' libdir may be a
# Debian multiarch directory or lib64, neither of which udev searches.
install(FILES packaging/68-zuuned.rules
        DESTINATION lib/udev/rules.d)
install(FILES packaging/68-zuuned.rules
        DESTINATION lib/udev/rules.d RENAME 72-zuuned.rules)
