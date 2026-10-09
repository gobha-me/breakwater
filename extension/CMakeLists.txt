include(FetchContent)

set(GODOTCPP_API_VERSION "4.7" CACHE STRING "Godot API the extension targets" FORCE)
set(GODOTCPP_SYSTEM_HEADERS ON CACHE BOOL "" FORCE)
if(CMAKE_BUILD_TYPE STREQUAL "Release")
    set(GODOTCPP_TARGET "template_release" CACHE STRING "" FORCE)
else()
    set(GODOTCPP_TARGET "template_debug" CACHE STRING "" FORCE)
endif()

FetchContent_Declare(
    godot-cpp
    GIT_REPOSITORY https://github.com/godotengine/godot-cpp.git
    GIT_TAG 10.0.0-stable
    GIT_SHALLOW TRUE
    SYSTEM
)
FetchContent_MakeAvailable(godot-cpp)

add_library(breakwater_extension SHARED src/register_types.cpp src/breakwater_board.cpp src/breakwater_mission.cpp)
target_link_libraries(breakwater_extension PRIVATE breakwater::core breakwater_warnings godot-cpp)

get_target_property(GODOTCPP_SUFFIX godot-cpp GODOTCPP_SUFFIX)
set(_bin_dir "${PROJECT_SOURCE_DIR}/game/bin")
set_target_properties(
    breakwater_extension
    PROPERTIES
        CXX_VISIBILITY_PRESET hidden
        VISIBILITY_INLINES_HIDDEN ON
        POSITION_INDEPENDENT_CODE ON
        PREFIX "lib"
        OUTPUT_NAME "breakwater${GODOTCPP_SUFFIX}"
        LIBRARY_OUTPUT_DIRECTORY "$<1:${_bin_dir}>"
)
