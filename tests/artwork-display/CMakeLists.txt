cmake_minimum_required(VERSION 3.21)
project(ArtworkDisplayTests LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 20)
find_package(Qt6 6.5 REQUIRED COMPONENTS Core Gui Qml Quick QuickControls2 QuickTest Concurrent)
qt_standard_project_setup(REQUIRES 6.5)
set(repo "${CMAKE_CURRENT_SOURCE_DIR}/../..")
qt_add_executable(artwork-display main.cpp
    "${repo}/src/artwork/ArtworkRequest.cpp"
    "${repo}/src/artwork/ArtworkPipeline.cpp"
    "${repo}/src/artwork/PrintRenderer.cpp")
target_include_directories(artwork-display PRIVATE "${repo}/src" "${repo}/src/artwork")
target_link_libraries(artwork-display PRIVATE Qt6::QuickTest Qt6::QuickControls2 Qt6::Concurrent)
foreach(component ArtworkImage ArtworkStyleSettings GradientText Theme)
    set_source_files_properties("${repo}/qml/${component}.qml" PROPERTIES QT_RESOURCE_ALIAS "${component}.qml")
endforeach()
set_source_files_properties("${repo}/qml/Theme.qml" PROPERTIES QT_QML_SINGLETON_TYPE TRUE)
foreach(font PermanentMarker-Regular.ttf SpecimenAlien-Regular.ttf)
    set_source_files_properties("${repo}/qml/fonts/${font}" PROPERTIES QT_RESOURCE_ALIAS "fonts/${font}")
endforeach()
qt_add_qml_module(artwork-display URI Zuuned VERSION 1.0
    SOURCES "${repo}/src/AppSettings.h" "${repo}/src/artwork/ArtworkRequest.h"
    QML_FILES "${repo}/qml/ArtworkImage.qml" "${repo}/qml/ArtworkStyleSettings.qml"
        "${repo}/qml/GradientText.qml" "${repo}/qml/Theme.qml"
    RESOURCES "${repo}/qml/fonts/PermanentMarker-Regular.ttf" "${repo}/qml/fonts/SpecimenAlien-Regular.ttf")
