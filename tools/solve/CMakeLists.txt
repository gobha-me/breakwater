add_executable(bw_solve main.cpp)
find_package(Threads REQUIRED)
target_link_libraries(bw_solve PRIVATE breakwater::core breakwater_warnings Threads::Threads)
