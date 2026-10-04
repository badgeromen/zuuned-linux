# Qt's C++ development packages do not guarantee that distro-split QML
# modules are installed. Fail at configure time instead of on first launch.
if(NOT ZUUNED_QML_IMPORT_ROOT)
    get_target_property(_zuuned_qmake Qt6::qmake IMPORTED_LOCATION)
    if(NOT _zuuned_qmake)
        get_target_property(_zuuned_qmake Qt6::qmake IMPORTED_LOCATION_RELEASE)
    endif()
    execute_process(COMMAND "${_zuuned_qmake}" -query QT_INSTALL_QML
        RESULT_VARIABLE _zuuned_query_result
        OUTPUT_VARIABLE _zuuned_qml_root OUTPUT_STRIP_TRAILING_WHITESPACE)
    if(NOT _zuuned_query_result EQUAL 0 OR NOT IS_DIRECTORY "${_zuuned_qml_root}")
        message(FATAL_ERROR "Cannot locate Qt QML modules; set ZUUNED_QML_IMPORT_ROOT to the target Qt QML directory.")
    endif()
    set(ZUUNED_QML_IMPORT_ROOT "${_zuuned_qml_root}" CACHE PATH "Target Qt QML module directory")
endif()
foreach(_zuuned_module
        QtQml QtQml/Models QtQml/WorkerScript QtQuick QtQuick/Window QtQuick/Layouts
        QtQuick/Templates QtQuick/Controls QtQuick/Controls/Basic
        QtQuick/Dialogs Qt5Compat/GraphicalEffects Qt5Compat/GraphicalEffects/private)
    if(NOT EXISTS "${ZUUNED_QML_IMPORT_ROOT}/${_zuuned_module}/qmldir")
        message(FATAL_ERROR
            "Missing QML module ${_zuuned_module} in ${ZUUNED_QML_IMPORT_ROOT}. "
            "Install Qt Declarative QML runtime modules and Qt5Compat GraphicalEffects "
            "(Arch: qt6-declarative qt6-5compat). See README.md.")
    endif()
endforeach()
