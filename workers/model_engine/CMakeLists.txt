add_executable(oep_model_engine main.cpp)
oep_apply_target_defaults(oep_model_engine)
target_compile_definitions(oep_model_engine PRIVATE OEP_VERSION="${PROJECT_VERSION}")
target_link_libraries(
    oep_model_engine
    PRIVATE
        oep::assembly
        oep::document
        oep::exchange
        oep::ipc
        oep::modeler
        oep::persistence
        oep::sketch
        oep::tessellation
        oep::topology_id
)
set_target_properties(oep_model_engine PROPERTIES OUTPUT_NAME model-engine)
