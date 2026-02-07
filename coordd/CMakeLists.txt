add_executable(coordd
    main.cpp)

target_link_libraries(coordd
    PRIVATE
        coord::coord)

target_compile_features(coordd PRIVATE cxx_std_20)
