add_executable(bw_replay main.cpp)
target_link_libraries(bw_replay PRIVATE breakwater::core breakwater_warnings)
