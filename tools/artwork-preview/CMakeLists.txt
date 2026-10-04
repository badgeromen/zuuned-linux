cmake_minimum_required(VERSION 3.21)
project(ZuunedArtworkPreview VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
find_package(Qt6 6.5 REQUIRED COMPONENTS Core Gui Qml Quick QuickControls2 Concurrent Sql)
qt_standard_project_setup(REQUIRES 6.5)

set(ZUUNED_SHARED_QML "${CMAKE_CURRENT_SOURCE_DIR}/../../qml")
set(ZUUNED_PRINT_SOURCE "${CMAKE_CURRENT_SOURCE_DIR}/../../src/artwork")

qt_add_executable(zuuned-artwork-preview
    main.cpp
    PreviewController.cpp
    "${ZUUNED_PRINT_SOURCE}/PrintRenderer.cpp"
)
target_include_directories(zuuned-artwork-preview PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}"
    "${ZUUNED_PRINT_SOURCE}"
    "${ZUUNED_PRINT_SOURCE}/.."
)
target_link_libraries(zuuned-artwork-preview PRIVATE
    Qt6::Core Qt6::Gui Qt6::Qml Qt6::Quick Qt6::QuickControls2 Qt6::Concurrent Qt6::Sql
)

# Use the app's actual tokens and lettering. Resource aliases keep Theme's
# relative font URLs valid without pulling the application into this tool.
set_source_files_properties(AppSettings.qml PreviewTheme.qml PROPERTIES QT_QML_SINGLETON_TYPE TRUE)
set_source_files_properties("${ZUUNED_SHARED_QML}/Theme.qml" PROPERTIES
    QT_QML_SINGLETON_TYPE TRUE QT_RESOURCE_ALIAS Theme.qml)
set_source_files_properties("${ZUUNED_SHARED_QML}/GradientText.qml" PROPERTIES
    QT_RESOURCE_ALIAS GradientText.qml)
set_source_files_properties("${ZUUNED_SHARED_QML}/fonts/PermanentMarker-Regular.ttf" PROPERTIES
    QT_RESOURCE_ALIAS fonts/PermanentMarker-Regular.ttf)
set_source_files_properties("${ZUUNED_SHARED_QML}/fonts/SpecimenAlien-Regular.ttf" PROPERTIES
    QT_RESOURCE_ALIAS fonts/SpecimenAlien-Regular.ttf)

qt_add_qml_module(zuuned-artwork-preview
    URI ZuunedPrintPreview
    VERSION 1.0
    SOURCES PreviewController.h
    QML_FILES
        Preview.qml
        PreviewTheme.qml
        AppSettings.qml
        "${ZUUNED_SHARED_QML}/Theme.qml"
        "${ZUUNED_SHARED_QML}/GradientText.qml"
    RESOURCES
        "${ZUUNED_SHARED_QML}/fonts/PermanentMarker-Regular.ttf"
        "${ZUUNED_SHARED_QML}/fonts/SpecimenAlien-Regular.ttf"
)
