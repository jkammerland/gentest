add_executable(coordctl
    main.cpp)

target_link_libraries(coordctl
    PRIVATE
        coord::coord)

target_compile_features(coordctl PRIVATE cxx_std_20)
