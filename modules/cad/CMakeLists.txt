add_library(oep_sketch STATIC sketch/src/sketch.cpp)
add_library(oep::sketch ALIAS oep_sketch)
target_include_directories(
    oep_sketch
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/sketch/include>
)
oep_apply_target_defaults(oep_sketch)
oep_add_boundary_library(assembly assembly)
oep_add_boundary_library(tessellation tessellation)
oep_add_boundary_library(viewport viewport)
oep_add_boundary_library(exchange exchange)

add_library(oep_modeler STATIC modeler/src/kernel_adapter.cpp)
add_library(oep::modeler ALIAS oep_modeler)
target_include_directories(
    oep_modeler
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/modeler/include>
)
target_link_libraries(oep_modeler PUBLIC oep::document oep::sketch)
oep_apply_target_defaults(oep_modeler)

add_library(oep_topology_id STATIC topology_id/src/topology_identity.cpp)
add_library(oep::topology_id ALIAS oep_topology_id)
target_include_directories(
    oep_topology_id
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/topology_id/include>
)
target_link_libraries(oep_topology_id PUBLIC oep::modeler)
oep_apply_target_defaults(oep_topology_id)

add_library(oep_cad_feature_model STATIC modeler/src/cad_feature_evaluator.cpp)
add_library(oep::cad_feature_model ALIAS oep_cad_feature_model)
target_include_directories(
    oep_cad_feature_model
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/modeler/include>
)
target_link_libraries(
    oep_cad_feature_model
    PUBLIC
        oep::document
        oep::modeler
        oep::sketch
        oep::topology_id
)
oep_apply_target_defaults(oep_cad_feature_model)

if(OEP_ENABLE_OCCT_ADAPTER)
    find_path(
        OEP_OCCT_INCLUDE_DIR
        NAMES Standard.hxx
        HINTS ${OEP_OCCT_ROOT}
        PATH_SUFFIXES include/opencascade
        REQUIRED
    )

    set(
        OEP_OCCT_REQUIRED_LIBRARIES
        TKernel
        TKMath
        TKG2d
        TKG3d
        TKGeomBase
        TKBRep
        TKGeomAlgo
        TKTopAlgo
        TKPrim
        TKBO
        TKShHealing
        TKBool
        TKFillet
    )
    set(OEP_OCCT_LINK_LIBRARIES)
    foreach(library_name IN LISTS OEP_OCCT_REQUIRED_LIBRARIES)
        find_library(
            OEP_OCCT_${library_name}_LIBRARY
            NAMES ${library_name}
            HINTS ${OEP_OCCT_ROOT}
            PATH_SUFFIXES lib lib64 lib/x86_64-linux-gnu
            REQUIRED
        )
        list(APPEND OEP_OCCT_LINK_LIBRARIES ${OEP_OCCT_${library_name}_LIBRARY})
    endforeach()

    add_library(oep_occt_geometry_kernel STATIC modeler/src/occt_geometry_kernel.cpp)
    add_library(oep::occt_geometry_kernel ALIAS oep_occt_geometry_kernel)
    target_include_directories(
        oep_occt_geometry_kernel
        SYSTEM PRIVATE
            ${OEP_OCCT_INCLUDE_DIR}
    )
    target_link_libraries(
        oep_occt_geometry_kernel
        PUBLIC oep::modeler
        PRIVATE ${OEP_OCCT_LINK_LIBRARIES}
    )
    target_compile_definitions(
        oep_occt_geometry_kernel
        PRIVATE OEP_OCCT_VERSION="${OEP_OCCT_VERSION}"
    )
    oep_apply_target_defaults(oep_occt_geometry_kernel)
endif()

target_link_libraries(oep_assembly INTERFACE oep::document oep::modeler)
target_link_libraries(oep_tessellation INTERFACE oep::document oep::modeler oep::topology_id)
target_link_libraries(oep_viewport INTERFACE oep::interaction oep::tessellation)
target_link_libraries(oep_exchange INTERFACE oep::document oep::modeler)
