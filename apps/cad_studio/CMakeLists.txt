add_executable(oep_cad_studio main.cpp)
oep_apply_target_defaults(oep_cad_studio)
target_compile_definitions(oep_cad_studio PRIVATE OEP_VERSION="${PROJECT_VERSION}")
target_link_libraries(
    oep_cad_studio
    PRIVATE
        oep::app_shell
        oep::assembly
        oep::automation
        oep::exchange
        oep::modeler
        oep::persistence
        oep::simulation_ir
        oep::sync
        oep::viewport
)
set_target_properties(oep_cad_studio PROPERTIES OUTPUT_NAME cad-studio)
