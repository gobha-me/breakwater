add_library(breakwater_core STATIC src/board.cpp src/log.cpp src/mission.cpp)
add_library(breakwater::core ALIAS breakwater_core)

target_include_directories(breakwater_core PUBLIC include)
target_link_libraries(breakwater_core PRIVATE breakwater_warnings PUBLIC breakwater_sanitizers)

# Linked into the GDExtension shared library.
set_target_properties(breakwater_core PROPERTIES POSITION_INDEPENDENT_CODE ON)
