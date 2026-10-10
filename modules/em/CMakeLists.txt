add_library(oep_simulation_ir STATIC simulation_ir/src/simulation_ir.cpp)
add_library(oep::simulation_ir ALIAS oep_simulation_ir)
target_include_directories(
    oep_simulation_ir
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/simulation_ir/include>
)
target_link_libraries(oep_simulation_ir PUBLIC oep::document)
oep_apply_target_defaults(oep_simulation_ir)
oep_add_boundary_library(em_geometry em_geometry)
oep_add_boundary_library(em_mesh em_mesh)
oep_add_boundary_library(em_solver_api em_solver_api)
oep_add_boundary_library(fdtd_engine fdtd_engine)
oep_add_boundary_library(fem_engine fem_engine)
oep_add_boundary_library(simulation_cost simulation_cost)
oep_add_boundary_library(simulation_cache simulation_cache)
oep_add_boundary_library(em_optimizer em_optimizer)
oep_add_boundary_library(basis_library basis_library)
oep_add_boundary_library(simulation_orchestrator simulation_orchestrator)
oep_add_boundary_library(simulation_results simulation_results)

target_link_libraries(oep_em_geometry INTERFACE oep::simulation_ir oep::modeler oep::topology_id)
target_link_libraries(oep_em_mesh INTERFACE oep::em_geometry)
target_link_libraries(oep_fdtd_engine INTERFACE oep::em_solver_api oep::em_mesh)
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
