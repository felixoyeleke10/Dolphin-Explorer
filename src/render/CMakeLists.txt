add_library(dolphin-render STATIC
    sonar/SSSPalette.cpp
)

target_include_directories(dolphin-render PUBLIC ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(dolphin-render PUBLIC dolphin-core Qt6::Gui)
