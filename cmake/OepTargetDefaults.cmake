# SPDX-FileCopyrightText: 2026 Open Engineering Platform contributors
# SPDX-License-Identifier: MIT

function(oep_apply_target_defaults target_name)
    target_compile_features(${target_name} PRIVATE cxx_std_20)

    if(MSVC)
        target_compile_options(${target_name} PRIVATE /W4 /permissive-)
        if(OEP_ENABLE_STRICT_WARNINGS)
            target_compile_options(${target_name} PRIVATE /WX)
        endif()
    else()
        target_compile_options(
            ${target_name}
            PRIVATE
                -Wall
                -Wextra
                -Wpedantic
                -Wconversion
                -Wsign-conversion
        )
        if(OEP_ENABLE_STRICT_WARNINGS)
            target_compile_options(${target_name} PRIVATE -Werror)
        endif()
    endif()
endfunction()
