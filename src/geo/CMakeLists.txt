add_library(dolphin-geo STATIC
    CoordinateTransformService.cpp
    EpsgDatabase.cpp
    GeoUtils.cpp
    SpatialRefParsing.cpp
    NormalizeSidescanPings.cpp
)

find_package(PROJ CONFIG REQUIRED)

target_include_directories(dolphin-geo PUBLIC ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(dolphin-geo PUBLIC dolphin-core PRIVATE PROJ::proj)
target_compile_definitions(dolphin-geo PRIVATE
    DOLPHIN_PROJ_DATA_DIR="${PROJ_DIR}"
)
