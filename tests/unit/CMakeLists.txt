# SPDX-FileCopyrightText: 2026 Open Engineering Platform contributors
# SPDX-License-Identifier: MIT

add_executable(document_state_machine_test document_state_machine_test.cpp)
oep_apply_target_defaults(document_state_machine_test)
target_link_libraries(document_state_machine_test PRIVATE oep::document)

add_test(NAME unit.document_state_machine COMMAND document_state_machine_test)

add_executable(persistence_checkpoint_test persistence_checkpoint_test.cpp)
oep_apply_target_defaults(persistence_checkpoint_test)
target_link_libraries(persistence_checkpoint_test PRIVATE oep::persistence)

add_test(NAME unit.persistence_checkpoint COMMAND persistence_checkpoint_test)

add_executable(feature_dependency_graph_test feature_dependency_graph_test.cpp)
oep_apply_target_defaults(feature_dependency_graph_test)
target_link_libraries(feature_dependency_graph_test PRIVATE oep::persistence)

add_test(NAME unit.feature_dependency_graph COMMAND feature_dependency_graph_test)

add_executable(topology_identity_test topology_identity_test.cpp)
oep_apply_target_defaults(topology_identity_test)
target_link_libraries(topology_identity_test PRIVATE oep::topology_id)

add_test(NAME unit.topology_identity COMMAND topology_identity_test)

add_executable(viewport_projection_test viewport_projection_test.cpp)
oep_apply_target_defaults(viewport_projection_test)
target_link_libraries(viewport_projection_test PRIVATE oep::viewport)

add_test(NAME unit.viewport_projection COMMAND viewport_projection_test)

add_executable(sketch_solver_test sketch_solver_test.cpp)
oep_apply_target_defaults(sketch_solver_test)
target_link_libraries(sketch_solver_test PRIVATE oep::sketch)

add_test(NAME unit.sketch_solver COMMAND sketch_solver_test)

add_executable(simulation_ir_test simulation_ir_test.cpp)
oep_apply_target_defaults(simulation_ir_test)
target_link_libraries(simulation_ir_test PRIVATE oep::simulation_ir)

add_test(NAME unit.simulation_ir COMMAND simulation_ir_test)

add_executable(reference_fdtd_test reference_fdtd_test.cpp)
oep_apply_target_defaults(reference_fdtd_test)
target_link_libraries(reference_fdtd_test PRIVATE oep::fdtd_engine)

add_test(NAME unit.reference_fdtd COMMAND reference_fdtd_test)

add_executable(cad_to_grid_test cad_to_grid_test.cpp)
oep_apply_target_defaults(cad_to_grid_test)
target_link_libraries(cad_to_grid_test PRIVATE oep::em_mesh)

add_test(NAME unit.cad_to_grid COMMAND cad_to_grid_test)

add_executable(simulation_cost_test simulation_cost_test.cpp)
oep_apply_target_defaults(simulation_cost_test)
target_link_libraries(simulation_cost_test PRIVATE oep::simulation_cost)

add_test(NAME unit.simulation_cost COMMAND simulation_cost_test)

add_executable(simulation_worker_lifecycle_test simulation_worker_lifecycle_test.cpp)
oep_apply_target_defaults(simulation_worker_lifecycle_test)
target_link_libraries(
    simulation_worker_lifecycle_test
    PRIVATE
        oep::simulation_orchestrator
)

add_test(NAME unit.simulation_worker_lifecycle COMMAND simulation_worker_lifecycle_test)

add_executable(ipc_frame_test ipc_frame_test.cpp)
oep_apply_target_defaults(ipc_frame_test)
target_link_libraries(ipc_frame_test PRIVATE oep::ipc)

add_test(NAME unit.ipc_frame COMMAND ipc_frame_test)

if(OEP_ENABLE_QT_SHELL)
    add_executable(app_shell_state_test app_shell_state_test.cpp)
    oep_apply_target_defaults(app_shell_state_test)
    target_link_libraries(app_shell_state_test PRIVATE oep::app_shell Qt6::Core)

    add_test(NAME unit.app_shell_state COMMAND app_shell_state_test)
endif()
