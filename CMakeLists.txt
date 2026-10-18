cmake_minimum_required(VERSION 3.16)
project(DiskLens VERSION 1.1.2 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(DISKLENS_QT_MAJOR "5" CACHE STRING "Qt major version to build against (5 or 6)")
set_property(CACHE DISKLENS_QT_MAJOR PROPERTY STRINGS 5 6)
if(NOT DISKLENS_QT_MAJOR MATCHES "^[56]$")
    message(FATAL_ERROR "DISKLENS_QT_MAJOR must be 5 or 6")
endif()
find_package(QT NAMES Qt${DISKLENS_QT_MAJOR} REQUIRED COMPONENTS Widgets Concurrent)
find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS Widgets Concurrent)
file(GENERATE OUTPUT "${CMAKE_BINARY_DIR}/disklens-version.txt" CONTENT "${PROJECT_VERSION}\n")

add_library(disklens_core STATIC
    src/types.h
    src/packagebackend.h src/packagebackend.cpp
    src/filescanner.h src/filescanner.cpp
    src/cachebackend.h src/cachebackend.cpp
    src/uninstallcontroller.h src/uninstallcontroller.cpp
)
target_include_directories(disklens_core PUBLIC src)
target_link_libraries(disklens_core PUBLIC Qt${QT_VERSION_MAJOR}::Core)
target_compile_options(disklens_core PRIVATE -Wall -Wextra -Wpedantic)

add_executable(disklens
    src/main.cpp src/mainwindow.h src/mainwindow.cpp
    src/models.h src/models.cpp src/widgets.h src/widgets.cpp
    src/cachepage.h src/cachepage.cpp
    assets/resources.qrc
)
target_link_libraries(disklens PRIVATE disklens_core Qt${QT_VERSION_MAJOR}::Widgets Qt${QT_VERSION_MAJOR}::Concurrent)
target_compile_definitions(disklens PRIVATE DISKLENS_VERSION="${PROJECT_VERSION}")
target_compile_options(disklens PRIVATE -Wall -Wextra -Wpedantic)
install(TARGETS disklens RUNTIME DESTINATION bin)
install(FILES packaging/disklens.desktop DESTINATION share/applications)
install(FILES assets/disklens.svg DESTINATION share/icons/hicolor/scalable/apps)
foreach(icon_size 16 32 48 64 128 256 512)
    install(FILES assets/icons/${icon_size}/disklens.png
        DESTINATION share/icons/hicolor/${icon_size}x${icon_size}/apps)
endforeach()
install(FILES packaging/io.github.broruan.DiskLens.metainfo.xml DESTINATION share/metainfo)
install(FILES packaging/copyright DESTINATION share/doc/disklens)

include(CTest)
if(BUILD_TESTING)
    find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS Test)
    foreach(test_name scanner inventory uninstall cache)
        if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/tests/${test_name}_test.cpp")
            add_executable(${test_name}_test tests/${test_name}_test.cpp)
            target_link_libraries(${test_name}_test PRIVATE disklens_core Qt${QT_VERSION_MAJOR}::Test)
            add_test(NAME ${test_name} COMMAND ${test_name}_test)
        endif()
    endforeach()
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/tests/cachepage_test.cpp")
        add_executable(cachepage_test tests/cachepage_test.cpp src/cachepage.h src/cachepage.cpp src/models.cpp src/widgets.cpp)
        target_link_libraries(cachepage_test PRIVATE disklens_core Qt${QT_VERSION_MAJOR}::Widgets Qt${QT_VERSION_MAJOR}::Concurrent Qt${QT_VERSION_MAJOR}::Test)
        add_test(NAME cachepage COMMAND cachepage_test)
        set_tests_properties(cachepage PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
    endif()
endif()
