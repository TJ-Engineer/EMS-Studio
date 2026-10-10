# SPDX-FileCopyrightText: 2026 Open Engineering Platform contributors
# SPDX-License-Identifier: MIT

if(TARGET oep_simulation_worker)
    add_executable(
        simulation_worker_process_test
        simulation_worker_process_test.cpp
    )
    oep_apply_target_defaults(simulation_worker_process_test)
    target_link_libraries(
        simulation_worker_process_test
        PRIVATE
            oep::ipc
            oep::simulation_orchestrator
    )
    add_dependencies(simulation_worker_process_test oep_simulation_worker)
    add_test(
        NAME integration.simulation_worker_process
        COMMAND simulation_worker_process_test $<TARGET_FILE:oep_simulation_worker>
    )
endif()
