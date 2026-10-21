cmake_minimum_required(VERSION 3.21)
project(SpokenNativeGate LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 20)
find_package(Qt6 6.8 REQUIRED COMPONENTS Core Gui Quick Qml Network Test)
find_package(PkgConfig REQUIRED)
pkg_check_modules(AV REQUIRED IMPORTED_TARGET libavformat libavcodec libavutil)
pkg_check_modules(MPV REQUIRED IMPORTED_TARGET mpv)
pkg_check_modules(SQLITE REQUIRED IMPORTED_TARGET sqlite3)
qt_standard_project_setup(REQUIRES 6.8)
qt_policy(SET QTP0004 NEW)
get_filename_component(ROOT "${CMAKE_CURRENT_LIST_DIR}/../../.." ABSOLUTE)
qt_add_executable(spoken-native
 main.cpp planner.cpp
 ${ROOT}/src/spoken/SpokenMediaService.cpp ${ROOT}/src/spoken/SpokenMediaService.h
 ${ROOT}/src/spoken/PodcastQueuePlanner.cpp ${ROOT}/src/spoken/PodcastQueuePlanner.h
 ${ROOT}/src/spoken/SpokenStore.cpp ${ROOT}/src/spoken/SpokenStore.h
 ${ROOT}/src/spoken/PodcastFeed.cpp ${ROOT}/src/spoken/AudiobookProbe.cpp
 ${ROOT}/src/player/PlayerService.cpp ${ROOT}/src/player/PlayerService.h
 ${ROOT}/src/player/MpvController.cpp ${ROOT}/src/player/MpvController.h
 ${ROOT}/src/AppSettings.h)
target_include_directories(spoken-native PRIVATE ${ROOT}/src ${ROOT}/src/spoken ${ROOT}/src/player)
target_link_libraries(spoken-native PRIVATE Qt6::Quick Qt6::Network Qt6::Test PkgConfig::AV PkgConfig::MPV PkgConfig::SQLITE)
set(QML_FILES Main.qml DeviceService.qml SyncEngine.qml TrayState.qml VideoPlayerService.qml
 ${ROOT}/tests/artwork-print/stubs/ArtworkRequest.qml)
foreach(name Theme Prefs Gallery GalleryView PageHeaderBand PivotBar MusicBreadcrumbBar AlphabetRail BookTrayState BookLibraryTray RoundedArtwork PodcastSyncPicker PodcastCleanupControl PodcastStatus EpisodeList KeyedRows OnDeviceBadge SidebarView QueueRibbonBar NowPlayingIsland SpinningDiscView VinylRecordView VolumeSlim GradientText AlbumArtPlaceholder MediaKindIcon SpokenPlaybackControls SpokenFolderImport SpokenButton SpokenCheckBox SpokenMediaPage SpokenDetailHeading SpokenShelf SpokenCard SpokenCoverSheet SpokenImportSheet ZuneSearchField ZuneMenu ZuneMenuItem QuickPicker DragGhost ArtworkImage ZuneScrollBar SplitAddButton)
 list(APPEND QML_FILES ${ROOT}/qml/${name}.qml)
endforeach()
foreach(file IN LISTS QML_FILES)
 get_filename_component(name "${file}" NAME)
 set_source_files_properties("${file}" PROPERTIES QT_RESOURCE_ALIAS "${name}")
endforeach()
set_source_files_properties(DeviceService.qml SyncEngine.qml TrayState.qml VideoPlayerService.qml ${ROOT}/qml/Prefs.qml ${ROOT}/qml/Theme.qml ${ROOT}/qml/Gallery.qml ${ROOT}/qml/BookTrayState.qml PROPERTIES QT_QML_SINGLETON_TYPE TRUE)
set_source_files_properties(${ROOT}/qml/fonts/ZuunedSans-Regular.ttf PROPERTIES QT_RESOURCE_ALIAS fonts/ZuunedSans-Regular.ttf)
set_source_files_properties(${ROOT}/qml/fonts/SpecimenAlien-Regular.ttf PROPERTIES QT_RESOURCE_ALIAS fonts/SpecimenAlien-Regular.ttf)
set_source_files_properties(${ROOT}/qml/fonts/PermanentMarker-Regular.ttf PROPERTIES QT_RESOURCE_ALIAS fonts/PermanentMarker-Regular.ttf)
set_source_files_properties(${ROOT}/qml/images/zuuned.png PROPERTIES QT_RESOURCE_ALIAS images/zuuned.png)
qt_add_qml_module(spoken-native URI Zuuned VERSION 1.0 NO_GENERATE_EXTRA_QMLDIRS QML_FILES ${QML_FILES}
 RESOURCES ${ROOT}/qml/images/zuuned.png ${ROOT}/qml/fonts/SpecimenAlien-Regular.ttf ${ROOT}/qml/fonts/PermanentMarker-Regular.ttf ${ROOT}/qml/fonts/ZuunedSans-Regular.ttf)
