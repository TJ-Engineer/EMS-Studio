# SPDX-FileCopyrightText: 2026 Open Engineering Platform contributors
# SPDX-License-Identifier: MIT

add_executable(oep_em_studio main.cpp)
oep_apply_target_defaults(oep_em_studio)
target_compile_definitions(oep_em_studio PRIVATE OEP_VERSION="${PROJECT_VERSION}")
target_link_libraries(
    oep_em_studio
    PRIVATE
        oep::app_shell
        oep::basis_library
        oep::em_geometry
        oep::em_mesh
        oep::em_optimizer
        oep::simulation_cost
        oep::simulation_ir
        oep::simulation_orchestrator
        oep::simulation_results
        oep::viewport
)
set_target_properties(oep_em_studio PROPERTIES OUTPUT_NAME em-studio)
