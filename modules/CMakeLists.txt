function(oep_add_boundary_library target_name relative_path)
    add_library(oep_${target_name} INTERFACE)
    add_library(oep::${target_name} ALIAS oep_${target_name})
    target_include_directories(
        oep_${target_name}
        INTERFACE
            $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/${relative_path}/include>
    )
endfunction()

add_subdirectory(platform)
add_subdirectory(cad)
add_subdirectory(em)
