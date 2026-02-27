cmake_minimum_required(VERSION 3.16)

add_executable(velix_texture_importer
    src/velix_texture_importer.cpp
)

target_link_libraries(velix_texture_importer
    PRIVATE
        VelixEngine
        VelixCore
)

target_compile_features(velix_texture_importer PRIVATE cxx_std_20)

