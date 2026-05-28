add_library(dolphin-geo STATIC
    CoordinateTransformService.cpp
    EpsgDatabase.cpp
    GeoUtils.cpp
    NavSmoother.cpp
    SpatialRefParsing.cpp
    NormalizeSidescanPings.cpp
)

target_include_directories(dolphin-geo PUBLIC ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(dolphin-geo PUBLIC dolphin-core)
