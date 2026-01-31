cmake_minimum_required(VERSION 3.28.3)
project(Kitaplik VERSION 0.1 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)

find_package(Qt6 REQUIRED COMPONENTS Core Widgets)

# Sources
set(KITAPLIK_SOURCES
    src/gui/kitaplik.cpp
    src/gui/ui/kitaplik.ui
    resources/resources.qrc
)
set(KITAPLIK_HEADERS
    src/gui/kitaplik.hpp
)

# Library target
add_library(Kitaplik STATIC ${KITAPLIK_SOURCES} ${KITAPLIK_HEADERS})
set_target_properties(Kitaplik PROPERTIES
    AUTOUIC_SEARCH_PATHS "${CMAKE_CURRENT_SOURCE_DIR}/src/gui/ui"
)
target_include_directories(Kitaplik
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src/gui>
        $<INSTALL_INTERFACE:include>
)

target_link_libraries(Kitaplik
    PUBLIC
        Qt6::Core
        Qt6::Widgets
)

add_library(Kitaplik::Kitaplik ALIAS Kitaplik)

option(KITAPLIK_BUILD_APP "Build the Kitaplik app" OFF)
if(KITAPLIK_BUILD_APP)
    add_executable(kitaplik src/gui/main.cpp)
    target_link_libraries(kitaplik PRIVATE Kitaplik::Kitaplik Qt6::Widgets)
endif()

# Simple install rules (optional)
install(TARGETS Kitaplik EXPORT KitaplikTargets
    ARCHIVE DESTINATION lib
    LIBRARY DESTINATION lib
    RUNTIME DESTINATION bin
    INCLUDES DESTINATION include
)
install(FILES ${KITAPLIK_HEADERS} DESTINATION include/kitaplik)

# Export config
install(EXPORT KitaplikTargets
    FILE KitaplikTargets.cmake
    NAMESPACE Kitaplik::
    DESTINATION lib/cmake/Kitaplik
)

configure_file("${CMAKE_CURRENT_SOURCE_DIR}/cmake/KitaplikConfig.cmake.in" "${CMAKE_CURRENT_BINARY_DIR}/KitaplikConfig.cmake" @ONLY)
install(FILES "${CMAKE_CURRENT_BINARY_DIR}/KitaplikConfig.cmake" DESTINATION lib/cmake/Kitaplik)
