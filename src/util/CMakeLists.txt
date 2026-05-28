add_library(dolphin-util STATIC
    Json.cpp
)
target_include_directories(dolphin-util PUBLIC ${CMAKE_SOURCE_DIR}/src)
