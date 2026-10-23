# SPDX-FileCopyrightText: 2026 Open Engineering Platform contributors
# SPDX-License-Identifier: MIT

oep_add_boundary_library(interaction interaction)
add_library(oep_app_shell STATIC)
add_library(oep::app_shell ALIAS oep_app_shell)
target_include_directories(
    oep_app_shell
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/app_shell/include>
)

if(OEP_ENABLE_QT_SHELL)
    set(
        OEP_APP_SHELL_QML_FILES
        ${CMAKE_CURRENT_SOURCE_DIR}/app_shell/qml/Main.qml
        ${CMAKE_CURRENT_SOURCE_DIR}/app_shell/qml/ShellButton.qml
        ${CMAKE_CURRENT_SOURCE_DIR}/app_shell/qml/InspectorSection.qml
        ${CMAKE_CURRENT_SOURCE_DIR}/app_shell/qml/Theme.js
        ${CMAKE_CURRENT_SOURCE_DIR}/app_shell/qml/qmldir
    )
    target_sources(
        oep_app_shell
        PRIVATE
            app_shell/src/application_qt.cpp
            app_shell/src/shell_controller.cpp
            app_shell/include/oep/app_shell/shell_controller.hpp
    )
    target_link_libraries(
        oep_app_shell
        PRIVATE
            oep::persistence
            Qt6::Core
            Qt6::Gui
            Qt6::Qml
            Qt6::Quick
            Qt6::QuickControls2
    )
    set_target_properties(oep_app_shell PROPERTIES AUTOMOC ON)
    qt_add_resources(
        oep_app_shell oep_app_shell_qml
        PREFIX "/oep/app_shell"
        BASE "${CMAKE_CURRENT_SOURCE_DIR}/app_shell/qml"
        FILES ${OEP_APP_SHELL_QML_FILES}
    )

    if(NOT TARGET Qt6::qmllint)
        message(FATAL_ERROR "Qt's qmllint tool is required by OEP_ENABLE_QT_SHELL")
    endif()
    if(OEP_QT_ROOT)
        set(
            OEP_QML_IMPORT_PATH
            "${OEP_QT_ROOT}/lib/${CMAKE_LIBRARY_ARCHITECTURE}/qt6/qml"
        )
    else()
        get_target_property(OEP_QMAKE_EXECUTABLE Qt6::qmake IMPORTED_LOCATION)
        execute_process(
            COMMAND ${OEP_QMAKE_EXECUTABLE} -query QT_INSTALL_QML
            OUTPUT_VARIABLE OEP_QML_IMPORT_PATH
            OUTPUT_STRIP_TRAILING_WHITESPACE
            COMMAND_ERROR_IS_FATAL ANY
        )
    endif()
    set(
        OEP_QML_TYPE_PATH
        "${CMAKE_CURRENT_SOURCE_DIR}/app_shell/qml/lint_modules"
    )
    add_custom_target(
        oep_qml_lint
        COMMAND $<TARGET_FILE:Qt6::qmllint>
            --bare
            -I ${OEP_QML_IMPORT_PATH}
            -I ${OEP_QML_TYPE_PATH}
            ${CMAKE_CURRENT_SOURCE_DIR}/app_shell/qml/Main.qml
            ${CMAKE_CURRENT_SOURCE_DIR}/app_shell/qml/ShellButton.qml
            ${CMAKE_CURRENT_SOURCE_DIR}/app_shell/qml/InspectorSection.qml
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/app_shell/qml
        COMMENT "Linting the native application shell QML"
        VERBATIM
    )
    if(BUILD_TESTING)
        add_test(
            NAME lint.qml_app_shell
            COMMAND $<TARGET_FILE:Qt6::qmllint>
                --bare
                -I ${OEP_QML_IMPORT_PATH}
                -I ${OEP_QML_TYPE_PATH}
                ${CMAKE_CURRENT_SOURCE_DIR}/app_shell/qml/Main.qml
                ${CMAKE_CURRENT_SOURCE_DIR}/app_shell/qml/ShellButton.qml
                ${CMAKE_CURRENT_SOURCE_DIR}/app_shell/qml/InspectorSection.qml
        )
        set_tests_properties(
            lint.qml_app_shell
            PROPERTIES WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/app_shell/qml
        )
    endif()
else()
    target_sources(oep_app_shell PRIVATE app_shell/src/application_headless.cpp)
endif()
oep_apply_target_defaults(oep_app_shell)

add_library(oep_ipc STATIC ipc/src/frame.cpp)
add_library(oep::ipc ALIAS oep_ipc)
target_include_directories(
    oep_ipc
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/ipc/include>
)
oep_apply_target_defaults(oep_ipc)
oep_add_boundary_library(automation automation)
oep_add_boundary_library(sync sync)

add_library(oep_document STATIC document/src/document.cpp)
add_library(oep::document ALIAS oep_document)
target_include_directories(
    oep_document
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/document/include>
)
oep_apply_target_defaults(oep_document)

add_library(oep_persistence STATIC persistence/src/checkpoint.cpp)
add_library(oep::persistence ALIAS oep_persistence)
target_include_directories(
    oep_persistence
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/persistence/include>
)
target_link_libraries(oep_persistence PUBLIC oep::document)
oep_apply_target_defaults(oep_persistence)

target_link_libraries(oep_app_shell PUBLIC oep::interaction oep::document oep::ipc oep::simulation_ir)
target_link_libraries(oep_app_shell PRIVATE oep::viewport)
target_link_libraries(oep_app_shell PRIVATE oep::simulation_cost oep::simulation_orchestrator)
target_link_libraries(oep_interaction INTERFACE oep::document)
target_link_libraries(oep_persistence INTERFACE oep::document)
target_link_libraries(oep_automation INTERFACE oep::document oep::ipc)
target_link_libraries(oep_sync INTERFACE oep::document oep::persistence)

if(OEP_ENABLE_QT_SHELL AND OEP_ENABLE_OCCT_ADAPTER)
    target_link_libraries(oep_app_shell PRIVATE oep::cad_feature_model oep::occt_geometry_kernel)
    target_compile_definitions(oep_app_shell PRIVATE OEP_SHELL_OCCT)
endif()
