add_library(dolphin-io STATIC
    # Probe dispatcher
    ProbeDispatch.cpp

    # Cache layer
    cache/ParsedCache.cpp
    cache/ParsedCache.Index.cpp
    cache/ParsedCache.Read.cpp

    # XTF reader
    xtf/XtfReader.cpp
    xtf/XtfIndex.cpp
    xtf/XtfPayload.cpp
    xtf/XtfNav.cpp
    xtf/XtfReader.Probe.cpp

    # JSF reader
    jsf/JsfReader.cpp
    jsf/JsfReader.Index.cpp
    jsf/JsfReader.Decode.cpp
    jsf/JsfReader.Probe.cpp

    # SEG-Y reader (sub-bottom profiler)
    segy/SegyReader.cpp
    segy/SegyReader.Index.cpp
    segy/SegyReader.Decode.cpp
    segy/SegyReader.Probe.cpp

    # Raster reader/writer (GeoTIFF, image + world file) via GDAL
    raster/RasterReader.cpp
    raster/RasterWriter.cpp
)

target_include_directories(dolphin-io PUBLIC ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(dolphin-io PUBLIC dolphin-core)

# GDAL — industry-standard geospatial raster I/O (GeoTIFF read/write, CRS, world
# files). Provided by vcpkg; see build_mingw.bat (CMAKE_PREFIX_PATH). PRIVATE: it
# is an implementation detail of the raster reader/writer, not exposed in headers.
find_package(GDAL CONFIG REQUIRED)
target_link_libraries(dolphin-io PRIVATE GDAL::GDAL)
