cmake_minimum_required(VERSION 3.20)
project(litch_max VERSION 0.1.0 LANGUAGES C CXX)

file(GLOB LITCH_MAX_SOURCES CONFIGURE_DEPENDS "src/*.cpp")
set(MCU_MAX_SOURCES
    vendor/mcu-max/src/mcu-max.c
    vendor/mcu-max/src/mcu-max-position.c
)

add_library(litch_max STATIC ${LITCH_MAX_SOURCES} ${MCU_MAX_SOURCES})
add_library(litch_max::litch_max ALIAS litch_max)
target_compile_features(litch_max PUBLIC cxx_std_17 PRIVATE c_std_11)
# Keep the measured scorer optimization local to its translation unit.
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    set_property(SOURCE src/classic_chess_evaluation.cpp APPEND PROPERTY
        COMPILE_OPTIONS "$<$<NOT:$<CONFIG:Debug>>:-O3>")
elseif(MSVC)
    set_property(SOURCE src/classic_chess_evaluation.cpp APPEND PROPERTY
        COMPILE_OPTIONS "$<$<NOT:$<CONFIG:Debug>>:/O2>")
endif()
set_target_properties(litch_max PROPERTIES C_EXTENSIONS OFF CXX_EXTENSIONS OFF)
target_include_directories(litch_max PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
    "${CMAKE_CURRENT_SOURCE_DIR}/vendor/mcu-max/src"
    "${CMAKE_CURRENT_SOURCE_DIR}/third_party/foxie_embedded/include"
)

option(LITCH_MAX_BUILD_EXAMPLES "Build the Classic and Duck chess examples" OFF)
if(LITCH_MAX_BUILD_EXAMPLES)
    foreach(variant IN ITEMS classic duck)
        add_executable(example_${variant} "examples/${variant}.cpp")
        target_link_libraries(example_${variant} PRIVATE litch_max::litch_max)
    endforeach()
endif()

include(CTest)
if(BUILD_TESTING)
    foreach(suite IN ITEMS
            classic_kpk
            classic_opening_book
            classic_repetition_key
            classic_search
            classic_tables
            continuous_duck_search
            duck_chess_rules)
        add_executable(test_${suite} "test/test_${suite}/test_main.cpp")
        target_link_libraries(test_${suite} PRIVATE litch_max::litch_max)
        add_test(NAME ${suite} COMMAND test_${suite})
    endforeach()
    target_sources(test_classic_search PRIVATE test/support/allocation_probe.cpp)

    # Compile each vendor suite against the C sources, retaining the upstream
    # platform-hook test's forced configuration independently of the library.
    foreach(suite IN ITEMS
            upstream_fen_castling
            upstream_live_candidates
            upstream_platform_hook
            upstream_query_purity
            upstream_root_candidates
            position_rules)
        add_executable(test_${suite}
            "vendor/mcu-max/tests/test_${suite}.c"
            ${MCU_MAX_SOURCES})
        target_compile_features(test_${suite} PRIVATE c_std_11)
        set_target_properties(test_${suite} PROPERTIES C_EXTENSIONS OFF)
        target_include_directories(test_${suite} PRIVATE vendor/mcu-max/src)
        if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
            target_compile_options(test_${suite} PRIVATE -Wall -Wextra -Werror)
        endif()
        add_test(NAME ${suite} COMMAND test_${suite})
    endforeach()
    set(MCU_MAX_HOOK_CONFIG
        "${CMAKE_CURRENT_SOURCE_DIR}/vendor/mcu-max/tests/test_upstream_platform_hook_config.h")
    if(MSVC)
        target_compile_options(test_upstream_platform_hook PRIVATE "/FI${MCU_MAX_HOOK_CONFIG}")
    else()
        target_compile_options(test_upstream_platform_hook PRIVATE -include "${MCU_MAX_HOOK_CONFIG}")
    endif()
endif()

option(LITCH_MAX_BUILD_GENERATORS "Build the offline knowledge generators" OFF)
if(LITCH_MAX_BUILD_GENERATORS)
    add_executable(litch_max_generate_kpk tools/engine_knowledge/generate_kpk.cpp)
    target_compile_features(litch_max_generate_kpk PRIVATE cxx_std_17)
    add_executable(litch_max_generate_opening_book tools/engine_knowledge/generate_opening_book.cpp)
    target_link_libraries(litch_max_generate_opening_book PRIVATE litch_max::litch_max)
endif()
