add_executable(oep_simulation_worker main.cpp)
oep_apply_target_defaults(oep_simulation_worker)
target_compile_definitions(oep_simulation_worker PRIVATE OEP_VERSION="${PROJECT_VERSION}")
target_link_libraries(
    oep_simulation_worker
    PRIVATE
        oep::fdtd_engine
        oep::ipc
        oep::simulation_cache
        oep::simulation_results
)
set_target_properties(oep_simulation_worker PROPERTIES OUTPUT_NAME simulation-worker)
