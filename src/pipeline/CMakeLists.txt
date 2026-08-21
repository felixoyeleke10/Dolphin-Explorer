add_library(dolphin-pipeline STATIC
    # NodeGraph — structure / traversal / execution / serialization
    NodeGraph.cpp
    NodeGraph.Traversal.cpp
    NodeGraph.Execution.cpp
    NodeGraph.Serialization.cpp
    NodeRegistry.cpp
    GraphRunner.cpp
    ProcessingContract.cpp
    SidescanEnhancementAlgorithms.cpp
    SidescanRadiometryAlgorithms.cpp

    # DataIn nodes
    nodes/input/SidescanInputNode.cpp
    nodes/input/SBPInputNode.cpp
    nodes/input/MBESInputNode.cpp

    # Correction nodes
    nodes/correction/TvgNode.cpp
    nodes/correction/ArcNode.cpp
    nodes/correction/BandPassNode.cpp
    nodes/correction/BottomDetectNode.cpp
    nodes/correction/SlantRangeNode.cpp
    nodes/correction/GeoCorrectNode.cpp
    nodes/correction/NavSmoothNode.cpp

    # Filter nodes
    nodes/filter/ClipRangeNode.cpp
    nodes/filter/SpeckleFilterNode.cpp
    nodes/filter/DecimatNode.cpp

    # Enhancement nodes
    nodes/enhancement/GainNormalizeNode.cpp
    nodes/enhancement/ContrastEnhanceNode.cpp
    nodes/enhancement/HistogramEqNode.cpp
    nodes/enhancement/SidescanEnhancementNode.cpp

    # Analysis nodes
    nodes/analysis/ShadowDetectNode.cpp

    # Merge nodes
    nodes/merge/MergeNode.cpp

    # Output nodes
    nodes/output/ExportGeoTiffNode.cpp
    nodes/output/ExportCsvNode.cpp
)

target_include_directories(dolphin-pipeline PUBLIC ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(dolphin-pipeline PUBLIC dolphin-core dolphin-geo dolphin-util)
