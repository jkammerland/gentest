add_executable(netd-helper
    main.cpp)

target_link_libraries(netd-helper
    PRIVATE
        coord::coord)

target_compile_features(netd-helper PRIVATE cxx_std_20)
