# SPDX-FileCopyrightText: 2026 Open Engineering Platform contributors
# SPDX-License-Identifier: MIT

oep_add_boundary_library(app_shell app_shell)
oep_add_boundary_library(interaction interaction)
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

target_link_libraries(oep_app_shell INTERFACE oep::interaction oep::document oep::ipc)
target_link_libraries(oep_interaction INTERFACE oep::document)
target_link_libraries(oep_persistence INTERFACE oep::document)
target_link_libraries(oep_automation INTERFACE oep::document oep::ipc)
target_link_libraries(oep_sync INTERFACE oep::document oep::persistence)
