add_executable(bw_solve main.cpp)
target_link_libraries(bw_solve PRIVATE breakwater::core breakwater_warnings)
