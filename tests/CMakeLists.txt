if(TARGET oep_cad_studio)
    add_test(NAME smoke.cad_studio COMMAND oep_cad_studio)
endif()

if(TARGET oep_em_studio)
    add_test(NAME smoke.em_studio COMMAND oep_em_studio)
endif()

if(TARGET oep_model_engine)
    add_test(NAME smoke.model_engine COMMAND oep_model_engine)
endif()

if(TARGET oep_simulation_worker)
    add_test(NAME smoke.simulation_worker COMMAND oep_simulation_worker)
endif()

add_subdirectory(unit)
add_subdirectory(golden)
