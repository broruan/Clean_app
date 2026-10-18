cmake_minimum_required(VERSION 3.16)
project(DiskLens VERSION 1.3.0 LANGUAGES CXX)

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
    src/spaceanalyzer.h src/spaceanalyzer.cpp
    src/developerbackend.h src/developerbackend.cpp
    src/appstorage.h src/appstorage.cpp
)
target_include_directories(disklens_core PUBLIC src)
target_link_libraries(disklens_core PUBLIC Qt${QT_VERSION_MAJOR}::Core)
target_compile_options(disklens_core PRIVATE -Wall -Wextra -Wpedantic)

add_executable(disklens
    src/main.cpp src/mainwindow.h src/mainwindow.cpp
    src/appearance.h src/appearance.cpp
    src/models.h src/models.cpp src/widgets.h src/widgets.cpp
    src/cachepage.h src/cachepage.cpp
    src/explorerpage.h src/explorerpage.cpp
    src/developerpage.h src/developerpage.cpp
    src/appstoragedialog.h src/appstoragedialog.cpp
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
    foreach(test_name scanner inventory uninstall cache spaceanalyzer developer appstorage)
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
    add_executable(localization_test tests/localization_test.cpp assets/resources.qrc)
    target_link_libraries(localization_test PRIVATE disklens_core Qt${QT_VERSION_MAJOR}::Test)
    target_compile_definitions(localization_test PRIVATE DISKLENS_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
    add_test(NAME localization COMMAND localization_test)
    add_executable(appearance_test tests/appearance_test.cpp
        src/appearance.h src/appearance.cpp src/mainwindow.h src/mainwindow.cpp
        src/models.cpp src/widgets.cpp src/cachepage.cpp src/explorerpage.cpp
        src/developerpage.cpp src/appstoragedialog.cpp assets/resources.qrc)
    target_link_libraries(appearance_test PRIVATE disklens_core Qt${QT_VERSION_MAJOR}::Widgets Qt${QT_VERSION_MAJOR}::Concurrent Qt${QT_VERSION_MAJOR}::Test)
    add_test(NAME appearance COMMAND appearance_test)
    set_tests_properties(appearance PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
    foreach(test_name explorerpage developerpage appstoragedialog)
        if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/tests/${test_name}_test.cpp")
            add_executable(${test_name}_test tests/${test_name}_test.cpp src/${test_name}.h src/${test_name}.cpp src/models.cpp src/widgets.cpp)
            target_link_libraries(${test_name}_test PRIVATE disklens_core Qt${QT_VERSION_MAJOR}::Widgets Qt${QT_VERSION_MAJOR}::Concurrent Qt${QT_VERSION_MAJOR}::Test)
            add_test(NAME ${test_name} COMMAND ${test_name}_test)
            set_tests_properties(${test_name} PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
        endif()
    endforeach()
endif()
