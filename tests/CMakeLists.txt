# -- CancellationToken unit tests ----------------------------------------------
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

add_executable(test_execution_progress_dialog
    test_execution_progress_dialog.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/features/import/ImportProgressDialog.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/features/import/ImportProgressDialog.Jobs.cpp
)
target_include_directories(test_execution_progress_dialog PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(test_execution_progress_dialog PRIVATE dolphin-ui-shared Qt6::Widgets)
set_target_properties(test_execution_progress_dialog PROPERTIES CXX_STANDARD 20 AUTOMOC ON)
add_test(NAME ExecutionProgressDialog COMMAND test_execution_progress_dialog)
set_tests_properties(ExecutionProgressDialog PROPERTIES ENVIRONMENT
    "QT_QPA_PLATFORM=offscreen;QT_QPA_PLATFORM_PLUGIN_PATH=$<TARGET_FILE_DIR:Qt6::Core>/../plugins/platforms")

# -- ProcessingService: batch/single-run exclusion and reservation release ----
add_executable(test_processing_service
    test_processing_service.cpp
)
target_include_directories(test_processing_service PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(test_processing_service PRIVATE dolphin-app Qt6::Core)
set_target_properties(test_processing_service PROPERTIES CXX_STANDARD 20 AUTOMOC OFF)
add_test(NAME ProcessingService COMMAND test_processing_service)

add_executable(test_artifact_sidecar test_artifact_sidecar.cpp)
target_include_directories(test_artifact_sidecar PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(test_artifact_sidecar PRIVATE dolphin-app)
set_target_properties(test_artifact_sidecar PROPERTIES CXX_STANDARD 20 AUTOMOC OFF)
add_test(NAME ArtifactSidecar COMMAND test_artifact_sidecar)

# -- NodeGraph persistence: transactional validation and complete round-trip --
add_executable(test_node_graph
    test_node_graph.cpp
)
target_include_directories(test_node_graph PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(test_node_graph PRIVATE dolphin-pipeline)
set_target_properties(test_node_graph PROPERTIES CXX_STANDARD 20 AUTOMOC OFF)
add_test(NAME NodeGraph COMMAND test_node_graph)

add_executable(test_slant_range_node test_slant_range_node.cpp)
target_include_directories(test_slant_range_node PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(test_slant_range_node PRIVATE dolphin-pipeline)
set_target_properties(test_slant_range_node PROPERTIES CXX_STANDARD 20 AUTOMOC OFF)
add_test(NAME SlantRangeNode COMMAND test_slant_range_node)

# -- AppStyle tokenization: no malformed/unresolved typography declarations --
add_executable(test_app_style
    test_app_style.cpp
)
target_include_directories(test_app_style PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(test_app_style PRIVATE dolphin-ui-style Qt6::Core)
set_target_properties(test_app_style PROPERTIES CXX_STANDARD 20 AUTOMOC OFF)
add_test(NAME AppStyle COMMAND test_app_style)

add_executable(test_parsed_cache
    test_parsed_cache.cpp
)

target_include_directories(test_parsed_cache PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(test_parsed_cache PRIVATE dolphin-io dolphin-geo)
set_target_properties(test_parsed_cache PROPERTIES CXX_STANDARD 20)

add_test(NAME ParsedCache COMMAND test_parsed_cache)

add_executable(test_geo_utils test_geo_utils.cpp)
target_include_directories(test_geo_utils PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(test_geo_utils PRIVATE dolphin-geo)
set_target_properties(test_geo_utils PROPERTIES CXX_STANDARD 20 AUTOMOC OFF)
add_test(NAME GeoUtils COMMAND test_geo_utils)

# -- Project storage model regression tests ------------------------------------
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

# -- SSS processing system: scope, per-layer isolation, and sole mutation path --
add_executable(test_sidescan_processing_coordinator
    test_sidescan_processing_coordinator.cpp
)
target_include_directories(test_sidescan_processing_coordinator PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(test_sidescan_processing_coordinator PRIVATE
    dolphin-ui-mainwindow
    Qt6::Core
)
set_target_properties(test_sidescan_processing_coordinator PROPERTIES CXX_STANDARD 20)
add_test(NAME SidescanProcessingCoordinator COMMAND test_sidescan_processing_coordinator)

add_executable(test_survey_display_coordinator test_survey_display_coordinator.cpp)
target_include_directories(test_survey_display_coordinator PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(test_survey_display_coordinator PRIVATE dolphin-ui-mainwindow Qt6::Core)
set_target_properties(test_survey_display_coordinator PROPERTIES CXX_STANDARD 20 AUTOMOC OFF)
add_test(NAME SurveyDisplayCoordinator COMMAND test_survey_display_coordinator)

# -- Project creation must never overwrite a manifest or hide path failures ---
add_executable(test_project_creation
    test_project_creation.cpp
)
target_include_directories(test_project_creation PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(test_project_creation PRIVATE dolphin-app Qt6::Core)
set_target_properties(test_project_creation PROPERTIES CXX_STANDARD 20 AUTOMOC OFF)
add_test(NAME ProjectCreation COMMAND test_project_creation)

# -- Contact model: monotonic labels, recycle bin, serialization round-trip ----
add_executable(test_contacts
    test_contacts.cpp
)
target_include_directories(test_contacts PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(test_contacts PRIVATE
    dolphin-app
    Qt6::Core
)
set_target_properties(test_contacts PROPERTIES
    CXX_STANDARD 20
    AUTOMOC OFF
)
add_test(NAME Contacts COMMAND test_contacts)

# -- Contact export: one canonical, atomic CSV path for every UI entry point --
add_executable(test_contact_report
    test_contact_report.cpp
)
target_include_directories(test_contact_report PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(test_contact_report PRIVATE
    dolphin-ui-contacts
    Qt6::Core
)
set_target_properties(test_contact_report PROPERTIES
    CXX_STANDARD 20
    AUTOMOC OFF
)
add_test(NAME ContactReport COMMAND test_contact_report)

# -- Contact snapshot interaction: constrained calibrated dimension drawing --
add_executable(test_contact_snapshot_measurement
    test_contact_snapshot_measurement.cpp
)
target_include_directories(test_contact_snapshot_measurement PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(test_contact_snapshot_measurement PRIVATE
    dolphin-ui-contacts
    Qt6::Widgets
)
set_target_properties(test_contact_snapshot_measurement PROPERTIES
    CXX_STANDARD 20
    AUTOMOC OFF
)
add_test(NAME ContactSnapshotMeasurement COMMAND test_contact_snapshot_measurement)

# -- ZipWriter: structural ZIP validity (backs .docx export) --------------------
add_executable(test_zip_writer
    test_zip_writer.cpp
)
target_include_directories(test_zip_writer PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(test_zip_writer PRIVATE dolphin-util)
set_target_properties(test_zip_writer PROPERTIES CXX_STANDARD 20 AUTOMOC OFF)
add_test(NAME ZipWriter COMMAND test_zip_writer)

# -- Raster reader/writer round-trip (GDAL) ------------------------------------
find_package(GDAL CONFIG REQUIRED)   # makes GDAL::GDAL visible in this scope
add_executable(test_raster_io
    test_raster_io.cpp
)
target_include_directories(test_raster_io PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(test_raster_io PRIVATE dolphin-io)
set_target_properties(test_raster_io PROPERTIES CXX_STANDARD 20 AUTOMOC OFF)
add_test(NAME RasterIO COMMAND test_raster_io)
# GDAL + its deps live in the vcpkg bin dir alongside gdal.dll — put it on PATH so
# the test resolves them at runtime (config-correct: debug/bin for Debug builds).
# GDAL_DATA / PROJ data come from the vcpkg share tree (GDAL_DIR == .../share/gdal)
# so the warp/reprojection test can transform UTM -> WGS84.
set_tests_properties(RasterIO PROPERTIES ENVIRONMENT_MODIFICATION
    "PATH=path_list_prepend:$<TARGET_FILE_DIR:GDAL::GDAL>;GDAL_DATA=set:${GDAL_DIR};PROJ_DATA=set:${GDAL_DIR}/../proj;PROJ_LIB=set:${GDAL_DIR}/../proj")

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

# -- Geographic longitude branch alignment (2D/3D map antimeridian) -----------
add_executable(test_map_longitude_branch
    test_map_longitude_branch.cpp
)
target_include_directories(test_map_longitude_branch PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(test_map_longitude_branch PRIVATE dolphin-ui-map)
set_target_properties(test_map_longitude_branch PROPERTIES
    CXX_STANDARD 20
    AUTOMOC OFF
)
add_test(NAME MapLongitudeBranch COMMAND test_map_longitude_branch)

# -- Raster cache round-trip / staleness (raster-first persistence) ------------
add_executable(test_raster_cache
    test_raster_cache.cpp
)
target_include_directories(test_raster_cache PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(test_raster_cache PRIVATE
    dolphin-ui-map
    dolphin-ui-contacts
)
set_target_properties(test_raster_cache PROPERTIES
    CXX_STANDARD 20
    AUTOMOC OFF
)
add_test(NAME RasterCache COMMAND test_raster_cache)

# -- Nav-correction unit tests -------------------------------------------------
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

add_executable(test_subbottom_view_math test_subbottom_view_math.cpp)
target_include_directories(test_subbottom_view_math PRIVATE ${CMAKE_SOURCE_DIR}/src)
set_target_properties(test_subbottom_view_math PROPERTIES CXX_STANDARD 20 AUTOMOC OFF)
add_test(NAME SubBottomViewMath COMMAND test_subbottom_view_math)

add_executable(test_subbottom_corrections test_subbottom_corrections.cpp)
target_include_directories(test_subbottom_corrections PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(test_subbottom_corrections PRIVATE dolphin-app)
set_target_properties(test_subbottom_corrections PROPERTIES CXX_STANDARD 20 AUTOMOC OFF)
add_test(NAME SubBottomCorrections COMMAND test_subbottom_corrections)

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

        foreach(_tgt test_project_storage test_sidescan_georef test_map_longitude_branch)
            # Qt6Core.dll from the Qt installation
            add_custom_command(TARGET ${_tgt} POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    "${_qt6_bin}/Qt6Core.dll"
                    "$<TARGET_FILE_DIR:${_tgt}>/Qt6Core.dll")
            # MinGW runtime — only needed when compiled with MinGW, not MSVC.
            if(MINGW)
                foreach(_dll libgcc_s_seh-1.dll libstdc++-6.dll libwinpthread-1.dll)
                    add_custom_command(TARGET ${_tgt} POST_BUILD
                        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                            "${_cxx_bin}/${_dll}"
                            "$<TARGET_FILE_DIR:${_tgt}>/${_dll}")
                endforeach()
            endif()
        endforeach()
    endif()
endif()

# -- Stage 04 import classifier and dedup regression tests --------------------
add_executable(test_import_classifier
    test_import_classifier.cpp
)
target_include_directories(test_import_classifier PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(test_import_classifier PRIVATE dolphin-app dolphin-io Qt6::Core)
set_target_properties(test_import_classifier PROPERTIES
    CXX_STANDARD 20
    AUTOMOC OFF
)
add_test(NAME ImportClassifier COMMAND test_import_classifier)

if(WIN32)
    if(_qt6_core_loc)
        add_custom_command(TARGET test_import_classifier POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${_qt6_bin}/Qt6Core.dll"
                "$<TARGET_FILE_DIR:test_import_classifier>/Qt6Core.dll")
        if(MINGW)
            foreach(_dll libgcc_s_seh-1.dll libstdc++-6.dll libwinpthread-1.dll)
                add_custom_command(TARGET test_import_classifier POST_BUILD
                    COMMAND ${CMAKE_COMMAND} -E copy_if_different
                        "${_cxx_bin}/${_dll}"
                        "$<TARGET_FILE_DIR:test_import_classifier>/${_dll}")
            endforeach()
        endif()
    endif()
endif()

# -- Stage 03 performance baseline --------------------------------------------
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
        if(MINGW)
            foreach(_dll libgcc_s_seh-1.dll libstdc++-6.dll libwinpthread-1.dll)
                add_custom_command(TARGET test_perf_baseline POST_BUILD
                    COMMAND ${CMAKE_COMMAND} -E copy_if_different
                        "${_cxx_bin}/${_dll}"
                        "$<TARGET_FILE_DIR:test_perf_baseline>/${_dll}")
            endforeach()
        endif()
        add_custom_command(TARGET test_perf_baseline POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${_qt6_bin}/Qt6Core.dll"
                "$<TARGET_FILE_DIR:test_perf_baseline>/Qt6Core.dll")
    endif()
endif()

# -- XTF parser compatibility tests (Stage 02) ---------------------------------
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

# -- JSF parser framing / Message Type 80 contract tests ----------------------
add_executable(test_jsf_reader
    test_jsf_reader.cpp
)
target_include_directories(test_jsf_reader PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(test_jsf_reader PRIVATE dolphin-io dolphin-geo)
set_target_properties(test_jsf_reader PROPERTIES
    CXX_STANDARD 20
    AUTOMOC OFF
)
add_test(NAME JsfReader COMMAND test_jsf_reader)

# -- SEG-Y parser tests --------------------------------------------------------
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

# -- GL initialization smoke test ----------------------------------------------
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
        if(MINGW)
            foreach(_dll libgcc_s_seh-1.dll libstdc++-6.dll libwinpthread-1.dll)
                add_custom_command(TARGET test_gl_smoke POST_BUILD
                    COMMAND ${CMAKE_COMMAND} -E copy_if_different
                        "${_cxx_bin}/${_dll}"
                        "$<TARGET_FILE_DIR:test_gl_smoke>/${_dll}")
            endforeach()
        endif()
    endif()
endif()
