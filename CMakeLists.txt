cmake_minimum_required(VERSION 3.20)

project(confy VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

find_program(CCACHE_PROGRAM ccache)
if(CCACHE_PROGRAM)
    message(STATUS "ccache found: ${CCACHE_PROGRAM}")
    if(NOT CMAKE_C_COMPILER_LAUNCHER)
        set(CMAKE_C_COMPILER_LAUNCHER "${CCACHE_PROGRAM}")
    endif()
    if(NOT CMAKE_CXX_COMPILER_LAUNCHER)
        set(CMAKE_CXX_COMPILER_LAUNCHER "${CCACHE_PROGRAM}")
    endif()
endif()

if(MSVC)
    add_compile_definitions(NOMINMAX _CRT_SECURE_NO_WARNINGS)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

set(CONFY_SOURCES
    src/main.cpp
    src/App.cpp
    src/AppSettings.cpp
    src/DebugConsole.cpp
    src/PickMenuFrame.cpp
    src/MainFrame.cpp
    src/ConfigLoader.cpp
    src/ConfigWriter.cpp
    src/DownloadWorkerQueue.cpp
    src/DownloadProgressDialog.cpp
    src/NexusClient.cpp
    src/GitClient.cpp
    src/BitbucketClient.cpp
    src/AuthCredentials.cpp
)

set(CONFY_HEADERS
    src/App.h
    src/AppSettings.h
    src/DebugConsole.h
    src/PickMenuFrame.h
    src/MainFrame.h
    src/ConfigModel.h
    src/ConfigLoader.h
    src/ConfigWriter.h
    src/JobTypes.h
    src/DownloadWorkerQueue.h
    src/DownloadProgressDialog.h
    src/NexusClient.h
    src/GitClient.h
    src/BitbucketClient.h
    src/AuthCredentials.h
)

find_package(CURL REQUIRED)

set(wxBUILD_SHARED OFF)
set(wxBUILD_PRECOMP OFF)
set(wxUSE_LIBTIFF OFF)
add_subdirectory(third_party/wxWidgets EXCLUDE_FROM_ALL)
add_subdirectory(third_party/doctest EXCLUDE_FROM_ALL)

add_executable(confy WIN32 ${CONFY_SOURCES} ${CONFY_HEADERS})
target_include_directories(confy PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/rapidxml
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/nlohmann_json/single_include
)
target_link_libraries(confy PRIVATE wx::core wx::base CURL::libcurl)

add_executable(confy_config_io_test
    tests/DoctestMain.cpp
    tests/ParserSmokeTest.cpp
    tests/ConfigLoaderRegexValidationTest.cpp
    tests/ConfigLoaderPathMacroTest.cpp
    tests/ConfigLoaderFileErrorTest.cpp
    tests/ConfigWriterTest.cpp
    src/ConfigWriter.cpp
    src/ConfigLoader.cpp
)
target_include_directories(confy_config_io_test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/rapidxml
)
target_link_libraries(confy_config_io_test PRIVATE doctest::doctest)

add_executable(confy_service_test
    tests/DoctestMain.cpp
    tests/AuthCredentialsTest.cpp
    tests/NexusClientAuthTest.cpp
    tests/NexusClientPathSegmentTest.cpp
    tests/GitClientTest.cpp
    tests/BitbucketClientTest.cpp
    tests/DownloadWorkerQueueTest.cpp
    src/AuthCredentials.cpp
    src/NexusClient.cpp
    src/GitClient.cpp
    src/BitbucketClient.cpp
    src/DownloadWorkerQueue.cpp
)
target_include_directories(confy_service_test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/rapidxml
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/nlohmann_json/single_include
)
target_link_libraries(confy_service_test PRIVATE doctest::doctest wx::base CURL::libcurl)

enable_testing()
include(${CMAKE_CURRENT_SOURCE_DIR}/third_party/doctest/scripts/cmake/doctest.cmake)
doctest_discover_tests(confy_config_io_test)
doctest_discover_tests(confy_service_test)

if(MSVC)
    target_compile_options(confy PRIVATE /W4)
else()
    target_compile_options(confy PRIVATE -Wall -Wextra -Wpedantic)
endif()
