# SPDX-FileCopyrightText: 2026 Open Engineering Platform contributors
# SPDX-License-Identifier: MIT

if(TARGET oep_cad_studio)
    add_test(NAME smoke.cad_studio COMMAND oep_cad_studio --smoke-test)
endif()

if(TARGET oep_em_studio)
    add_test(NAME smoke.em_studio COMMAND oep_em_studio --smoke-test)
endif()

if(OEP_ENABLE_QT_SHELL)
    set_tests_properties(
        smoke.cad_studio smoke.em_studio
        PROPERTIES
            ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QSG_RHI_BACKEND=software;XDG_CONFIG_HOME=${CMAKE_BINARY_DIR}/test-config"
    )
endif()

if(TARGET oep_model_engine)
    add_test(NAME smoke.model_engine COMMAND oep_model_engine)
endif()

if(TARGET oep_simulation_worker)
    add_test(NAME smoke.simulation_worker COMMAND oep_simulation_worker)
endif()

add_subdirectory(unit)
add_subdirectory(golden)
add_subdirectory(integration)
