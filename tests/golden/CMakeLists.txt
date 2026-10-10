# SPDX-FileCopyrightText: 2026 Open Engineering Platform contributors
# SPDX-License-Identifier: MIT

add_library(
    oep_cad_kernel_corpus
    STATIC
        cad/kernel_corpus.cpp
)
target_include_directories(
    oep_cad_kernel_corpus
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/cad/include
)
target_link_libraries(oep_cad_kernel_corpus PUBLIC oep::modeler)
oep_apply_target_defaults(oep_cad_kernel_corpus)

add_library(
    oep_fake_geometry_kernel
    STATIC
        cad/fake_geometry_kernel.cpp
)
target_include_directories(
    oep_fake_geometry_kernel
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/cad/include
)
target_link_libraries(oep_fake_geometry_kernel PUBLIC oep::modeler)
oep_apply_target_defaults(oep_fake_geometry_kernel)

add_executable(
    cad_kernel_adapter_contract_test
    cad/kernel_adapter_contract_test.cpp
)
oep_apply_target_defaults(cad_kernel_adapter_contract_test)
target_link_libraries(
    cad_kernel_adapter_contract_test
    PRIVATE
        oep_cad_kernel_corpus
        oep_fake_geometry_kernel
)

add_test(NAME golden.cad_kernel_adapter_contract COMMAND cad_kernel_adapter_contract_test)

add_library(
    oep_topology_mutation_corpus
    STATIC
        cad/topology_mutation_corpus.cpp
)
target_include_directories(
    oep_topology_mutation_corpus
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/cad/include
)
target_link_libraries(
    oep_topology_mutation_corpus
    PUBLIC
        oep::modeler
        oep::topology_id
)
oep_apply_target_defaults(oep_topology_mutation_corpus)

add_executable(
    topology_mutation_test
    cad/topology_mutation_test.cpp
)
oep_apply_target_defaults(topology_mutation_test)
target_link_libraries(
    topology_mutation_test
    PRIVATE
        oep_topology_mutation_corpus
        oep_fake_geometry_kernel
)
add_test(NAME golden.topology_mutation COMMAND topology_mutation_test)

add_library(
    oep_modeling_thread_corpus
    STATIC
        cad/modeling_thread_corpus.cpp
)
target_include_directories(
    oep_modeling_thread_corpus
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/cad/include
)
target_link_libraries(
    oep_modeling_thread_corpus
    PUBLIC
        oep::cad_feature_model
        oep::persistence
)
oep_apply_target_defaults(oep_modeling_thread_corpus)

add_executable(modeling_thread_test cad/modeling_thread_test.cpp)
oep_apply_target_defaults(modeling_thread_test)
target_link_libraries(
    modeling_thread_test
    PRIVATE
        oep_modeling_thread_corpus
        oep_fake_geometry_kernel
)
add_test(NAME golden.modeling_thread COMMAND modeling_thread_test)

add_library(
    oep_maxwell_verification_harness
    STATIC
        em/maxwell_harness.cpp
)
target_include_directories(
    oep_maxwell_verification_harness
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/em/include
)
oep_apply_target_defaults(oep_maxwell_verification_harness)

add_executable(
    maxwell_harness_test
    em/maxwell_harness_test.cpp
)
target_link_libraries(
    maxwell_harness_test
    PRIVATE
        oep_maxwell_verification_harness
)
oep_apply_target_defaults(maxwell_harness_test)
add_test(NAME golden.maxwell_harness COMMAND maxwell_harness_test)

add_executable(
    reference_fdtd_maxwell_test
    em/reference_fdtd_maxwell_test.cpp
)
target_link_libraries(
    reference_fdtd_maxwell_test
    PRIVATE
        oep::fdtd_engine
        oep_maxwell_verification_harness
)
oep_apply_target_defaults(reference_fdtd_maxwell_test)
add_test(NAME golden.reference_fdtd_maxwell COMMAND reference_fdtd_maxwell_test)

add_library(
    oep_port_contract_harness
    STATIC
        em/port_contract_harness.cpp
)
target_include_directories(
    oep_port_contract_harness
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/em/include
)
oep_apply_target_defaults(oep_port_contract_harness)

add_executable(
    port_contract_harness_test
    em/port_contract_harness_test.cpp
)
target_link_libraries(
    port_contract_harness_test
    PRIVATE
        oep_port_contract_harness
)
oep_apply_target_defaults(port_contract_harness_test)
add_test(NAME golden.port_contract_harness COMMAND port_contract_harness_test)

if(TARGET oep_occt_geometry_kernel)
    add_executable(
        occt_kernel_adapter_contract_test
        cad/occt_kernel_adapter_contract_test.cpp
    )
    oep_apply_target_defaults(occt_kernel_adapter_contract_test)
    target_link_libraries(
        occt_kernel_adapter_contract_test
        PRIVATE
            oep_cad_kernel_corpus
            oep::occt_geometry_kernel
    )
    if(OEP_OCCT_ROOT)
        set_target_properties(
            occt_kernel_adapter_contract_test
            PROPERTIES
                BUILD_RPATH "${OEP_OCCT_ROOT}/lib;${OEP_OCCT_ROOT}/lib64;${OEP_OCCT_ROOT}/lib/x86_64-linux-gnu"
        )
    endif()
    add_test(NAME golden.occt_kernel_adapter_contract COMMAND occt_kernel_adapter_contract_test)
    add_executable(
        occt_topology_mutation_test
        cad/occt_topology_mutation_test.cpp
    )
    oep_apply_target_defaults(occt_topology_mutation_test)
    target_link_libraries(
        occt_topology_mutation_test
        PRIVATE
            oep_topology_mutation_corpus
            oep::occt_geometry_kernel
    )
    add_test(NAME golden.occt_topology_mutation COMMAND occt_topology_mutation_test)
    add_executable(
        occt_modeling_thread_test
        cad/occt_modeling_thread_test.cpp
    )
    oep_apply_target_defaults(occt_modeling_thread_test)
    target_link_libraries(
        occt_modeling_thread_test
        PRIVATE
            oep_modeling_thread_corpus
            oep::occt_geometry_kernel
    )
    add_test(NAME golden.occt_modeling_thread COMMAND occt_modeling_thread_test)
    if(OEP_OCCT_ROOT)
        set_tests_properties(
            golden.occt_kernel_adapter_contract
            golden.occt_topology_mutation
            golden.occt_modeling_thread
            PROPERTIES
                ENVIRONMENT "LD_LIBRARY_PATH=${OEP_OCCT_ROOT}/lib:${OEP_OCCT_ROOT}/lib64:${OEP_OCCT_ROOT}/lib/x86_64-linux-gnu"
        )
    endif()
endif()
