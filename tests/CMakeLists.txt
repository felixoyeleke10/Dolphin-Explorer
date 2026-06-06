# ── CancellationToken unit tests ──────────────────────────────────────────────
add_executable(test_cancellation_token
    test_cancellation_token.cpp
)
target_include_directories(test_cancellation_token PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(test_cancellation_token PRIVATE dolphin-app)
set_target_properties(test_cancellation_token PROPERTIES CXX_STANDARD 20 AUTOMOC OFF)
add_test(NAME CancellationToken COMMAND test_cancellation_token)

add_executable(test_task_registry
    test_task_registry.cpp
)
target_include_directories(test_task_registry PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(test_task_registry PRIVATE dolphin-app)
set_target_properties(test_task_registry PROPERTIES CXX_STANDARD 20 AUTOMOC OFF)
add_test(NAME TaskRegistry COMMAND test_task_registry)

add_executable(test_parsed_cache
    test_parsed_cache.cpp
)

target_include_directories(test_parsed_cache PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(test_parsed_cache PRIVATE dolphin-io dolphin-geo)
set_target_properties(test_parsed_cache PROPERTIES CXX_STANDARD 20)

add_test(NAME ParsedCache COMMAND test_parsed_cache)

# ── Project storage model regression tests ────────────────────────────────────
add_executable(test_project_storage
    test_project_storage.cpp
)

target_include_directories(test_project_storage PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(test_project_storage PRIVATE
    dolphin-app
    Qt6::Core
)
set_target_properties(test_project_storage PROPERTIES
    CXX_STANDARD 20
    AUTOMOC OFF
)

add_test(NAME ProjectStorage COMMAND test_project_storage)

add_executable(test_sidescan_georef
    test_sidescan_georef.cpp
)

target_include_directories(test_sidescan_georef PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(test_sidescan_georef PRIVATE dolphin-ui)
set_target_properties(test_sidescan_georef PROPERTIES
    CXX_STANDARD 20
    AUTOMOC OFF
)

add_test(NAME SidescanGeoref COMMAND test_sidescan_georef)

# ── Nav-correction unit tests ─────────────────────────────────────────────────
add_executable(test_nav_corrections
    test_nav_corrections.cpp
)

target_include_directories(test_nav_corrections PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(test_nav_corrections PRIVATE dolphin-ui)
set_target_properties(test_nav_corrections PROPERTIES
    CXX_STANDARD 20
    AUTOMOC OFF
)

add_test(NAME NavCorrections COMMAND test_nav_corrections)

add_executable(test_seabed_auto_detector
    test_seabed_auto_detector.cpp
)

target_include_directories(test_seabed_auto_detector PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(test_seabed_auto_detector PRIVATE dolphin-ui-waterfall)
set_target_properties(test_seabed_auto_detector PROPERTIES
    CXX_STANDARD 20
    AUTOMOC OFF
)

add_test(NAME SeabedAutoDetector COMMAND test_seabed_auto_detector)

add_executable(test_waterfall_processing_algorithms
    test_waterfall_processing_algorithms.cpp
)

target_include_directories(test_waterfall_processing_algorithms PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(test_waterfall_processing_algorithms PRIVATE dolphin-ui-waterfall)
set_target_properties(test_waterfall_processing_algorithms PROPERTIES
    CXX_STANDARD 20
    AUTOMOC OFF
)

add_test(NAME WaterfallProcessingAlgorithms COMMAND test_waterfall_processing_algorithms)

# On Windows: deploy Qt DLLs and the CORRECT MinGW compiler runtime next to the
# test executables so they run without PATH manipulation.
#
# windeployqt bundles a different libgcc_s_seh-1.dll than the one used by the
# compiler (C:/Qt/Tools/mingw1310_64).  Deploying from the compiler directory
# ensures the runtime matches what the test was compiled against.
if(WIN32)
    get_target_property(_qt6_core_loc Qt6::Core IMPORTED_LOCATION_RELEASE)
    if(NOT _qt6_core_loc)
        get_target_property(_qt6_core_loc Qt6::Core IMPORTED_LOCATION)
    endif()
    get_filename_component(_cxx_bin "${CMAKE_CXX_COMPILER}" DIRECTORY)

    if(_qt6_core_loc)
        get_filename_component(_qt6_bin "${_qt6_core_loc}" DIRECTORY)

        foreach(_tgt test_project_storage test_sidescan_georef)
            # Qt6Core.dll from the Qt installation
            add_custom_command(TARGET ${_tgt} POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    "${_qt6_bin}/Qt6Core.dll"
                    "$<TARGET_FILE_DIR:${_tgt}>/Qt6Core.dll")
            # MinGW runtime from the compiler toolchain (must match what the exe was
            # compiled against — NOT the Qt-bundled copies which differ in ABI).
            foreach(_dll libgcc_s_seh-1.dll libstdc++-6.dll libwinpthread-1.dll)
                add_custom_command(TARGET ${_tgt} POST_BUILD
                    COMMAND ${CMAKE_COMMAND} -E copy_if_different
                        "${_cxx_bin}/${_dll}"
                        "$<TARGET_FILE_DIR:${_tgt}>/${_dll}")
            endforeach()
        endforeach()
    endif()
endif()

# ── Stage 03 performance baseline ────────────────────────────────────────────
add_executable(test_perf_baseline
    test_perf_baseline.cpp
)
target_include_directories(test_perf_baseline PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(test_perf_baseline PRIVATE dolphin-app dolphin-io Qt6::Core)
target_compile_definitions(test_perf_baseline PRIVATE
    XTF_FIXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/fixtures")
set_target_properties(test_perf_baseline PROPERTIES
    CXX_STANDARD 20
    AUTOMOC OFF
)
add_test(NAME PerfBaseline COMMAND test_perf_baseline)

if(WIN32)
    if(_qt6_core_loc)
        foreach(_dll libgcc_s_seh-1.dll libstdc++-6.dll libwinpthread-1.dll)
            add_custom_command(TARGET test_perf_baseline POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    "${_cxx_bin}/${_dll}"
                    "$<TARGET_FILE_DIR:test_perf_baseline>/${_dll}")
        endforeach()
        add_custom_command(TARGET test_perf_baseline POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${_qt6_bin}/Qt6Core.dll"
                "$<TARGET_FILE_DIR:test_perf_baseline>/Qt6Core.dll")
    endif()
endif()

# ── XTF parser compatibility tests (Stage 02) ─────────────────────────────────
add_executable(test_xtf_reader
    test_xtf_reader.cpp
)

target_include_directories(test_xtf_reader PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(test_xtf_reader PRIVATE dolphin-io)
# Absolute path to the reduced real-vendor fixtures (FIX-016/FIX-017) so the
# test can load them regardless of the working directory ctest runs from.
target_compile_definitions(test_xtf_reader PRIVATE
    XTF_FIXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/fixtures")
set_target_properties(test_xtf_reader PROPERTIES
    CXX_STANDARD 20
    AUTOMOC OFF
)

add_test(NAME XtfReader COMMAND test_xtf_reader)

# ── SEG-Y parser tests ────────────────────────────────────────────────────────
add_executable(test_segy_reader
    test_segy_reader.cpp
)

target_include_directories(test_segy_reader PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(test_segy_reader PRIVATE dolphin-io)
set_target_properties(test_segy_reader PROPERTIES
    CXX_STANDARD 20
    AUTOMOC OFF
)

add_test(NAME SegyReader COMMAND test_segy_reader)

# ── GL initialization smoke test ──────────────────────────────────────────────
# Verifies WaterfallView and MapView3D start up without shader errors.
# The test uses the native Qt platform on Windows (no QT_QPA_PLATFORM override)
# so that a real OpenGL context is available.
add_executable(test_gl_smoke test_gl_smoke.cpp)
target_include_directories(test_gl_smoke PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(test_gl_smoke PRIVATE dolphin-ui Qt6::Widgets Qt6::OpenGLWidgets)
set_target_properties(test_gl_smoke PROPERTIES
    CXX_STANDARD 20
    AUTOMOC OFF
)
add_test(NAME GlSmoke COMMAND test_gl_smoke)

if(WIN32)
    get_target_property(_qt6_core_loc Qt6::Core IMPORTED_LOCATION_RELEASE)
    if(NOT _qt6_core_loc)
        get_target_property(_qt6_core_loc Qt6::Core IMPORTED_LOCATION)
    endif()
    if(_qt6_core_loc)
        get_filename_component(_qt6_bin "${_qt6_core_loc}" DIRECTORY)
        get_filename_component(_cxx_bin "${CMAKE_CXX_COMPILER}" DIRECTORY)
        find_program(_windeployqt windeployqt PATHS "${_qt6_bin}")
        if(_windeployqt)
            add_custom_command(TARGET test_gl_smoke POST_BUILD
                COMMAND "${_windeployqt}" --no-translations
                        "$<TARGET_FILE:test_gl_smoke>"
                COMMENT "Deploying Qt DLLs for test_gl_smoke")
        endif()
        foreach(_dll libgcc_s_seh-1.dll libstdc++-6.dll libwinpthread-1.dll)
            add_custom_command(TARGET test_gl_smoke POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    "${_cxx_bin}/${_dll}"
                    "$<TARGET_FILE_DIR:test_gl_smoke>/${_dll}")
        endforeach()
    endif()
endif()
