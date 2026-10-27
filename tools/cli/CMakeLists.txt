add_executable(sloptube-cli main.cpp)
target_link_libraries(sloptube-cli PRIVATE sloptube::core sloptube::options)
