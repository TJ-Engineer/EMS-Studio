# SPDX-FileCopyrightText: 2026 Open Engineering Platform contributors
# SPDX-License-Identifier: MIT

add_library(oep_simulation_ir STATIC simulation_ir/src/simulation_ir.cpp)
add_library(oep::simulation_ir ALIAS oep_simulation_ir)
target_include_directories(
    oep_simulation_ir
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/simulation_ir/include>
)
target_link_libraries(oep_simulation_ir PUBLIC oep::document)
oep_apply_target_defaults(oep_simulation_ir)
add_library(oep_em_geometry STATIC em_geometry/src/computational_geometry.cpp)
add_library(oep::em_geometry ALIAS oep_em_geometry)
target_include_directories(
    oep_em_geometry
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/em_geometry/include>
)
target_link_libraries(oep_em_geometry PUBLIC oep::simulation_ir)
oep_apply_target_defaults(oep_em_geometry)

add_library(oep_em_mesh STATIC em_mesh/src/cartesian_grid.cpp)
add_library(oep::em_mesh ALIAS oep_em_mesh)
target_include_directories(
    oep_em_mesh
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/em_mesh/include>
)
target_link_libraries(oep_em_mesh PUBLIC oep::em_geometry)
oep_apply_target_defaults(oep_em_mesh)
oep_add_boundary_library(em_solver_api em_solver_api)
add_library(oep_fdtd_engine STATIC fdtd_engine/src/reference_fdtd.cpp)
add_library(oep::fdtd_engine ALIAS oep_fdtd_engine)
target_include_directories(
    oep_fdtd_engine
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/fdtd_engine/include>
)
target_link_libraries(oep_fdtd_engine PUBLIC oep::em_solver_api oep::em_mesh)
oep_apply_target_defaults(oep_fdtd_engine)
oep_add_boundary_library(fem_engine fem_engine)
oep_add_boundary_library(simulation_cost simulation_cost)
oep_add_boundary_library(simulation_cache simulation_cache)
oep_add_boundary_library(em_optimizer em_optimizer)
oep_add_boundary_library(basis_library basis_library)
oep_add_boundary_library(simulation_orchestrator simulation_orchestrator)
oep_add_boundary_library(simulation_results simulation_results)

target_link_libraries(oep_fem_engine INTERFACE oep::em_solver_api oep::em_mesh)
target_link_libraries(oep_simulation_cost INTERFACE oep::simulation_ir oep::em_mesh)
target_link_libraries(oep_simulation_cache INTERFACE oep::simulation_ir oep::em_solver_api)
target_link_libraries(oep_em_optimizer INTERFACE oep::simulation_ir oep::simulation_results)
target_link_libraries(oep_basis_library INTERFACE oep::simulation_ir oep::simulation_results)
target_link_libraries(
    oep_simulation_orchestrator
    INTERFACE
        oep::em_solver_api
        oep::simulation_cost
        oep::simulation_cache
)
target_link_libraries(oep_simulation_results INTERFACE oep::simulation_ir)
