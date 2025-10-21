cmake_minimum_required(VERSION 3.22)
project(VelixSDK LANGUAGES CXX)

add_library(${PROJECT_NAME} SHARED 
    src/Entity.cpp
)

target_include_directories(${PROJECT_NAME} PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/Core/include
    ${CMAKE_SOURCE_DIR}/Engine/include
)

target_link_libraries(${PROJECT_NAME} PUBLIC VelixEngine VelixCore)

target_compile_definitions(${PROJECT_NAME}
    PRIVATE VELIX_SDK_BUILD
)

include(GNUInstallDirs)

install(TARGETS VelixSDK
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
)

install(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})

configure_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/VelixSDKConfig.cmake.in
    ${CMAKE_CURRENT_BINARY_DIR}/VelixSDKConfig.cmake
    @ONLY
)

install(FILES ${CMAKE_CURRENT_BINARY_DIR}/VelixSDKConfig.cmake DESTINATION ${CMAKE_INSTALL_DATAROOTDIR}/cmake/VelixSDK)