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

add_executable(confy_auth_credentials_test
    tests/AuthCredentialsTest.cpp
    src/AuthCredentials.cpp
)
target_include_directories(confy_auth_credentials_test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/rapidxml
)

add_executable(confy_nexus_client_auth_test
    tests/NexusClientAuthTest.cpp
    src/NexusClient.cpp
    src/AuthCredentials.cpp
)
target_include_directories(confy_nexus_client_auth_test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/rapidxml
)
target_link_libraries(confy_nexus_client_auth_test PRIVATE CURL::libcurl)

add_executable(confy_nexus_client_path_segment_test
    tests/NexusClientPathSegmentTest.cpp
    src/NexusClient.cpp
    src/AuthCredentials.cpp
)
target_include_directories(confy_nexus_client_path_segment_test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/rapidxml
)
target_link_libraries(confy_nexus_client_path_segment_test PRIVATE CURL::libcurl)

add_executable(confy_git_client_test
    tests/GitClientTest.cpp
    src/GitClient.cpp
    src/AuthCredentials.cpp
)
target_include_directories(confy_git_client_test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/rapidxml
)

add_executable(confy_bitbucket_client_test
    tests/BitbucketClientTest.cpp
    src/BitbucketClient.cpp
    src/AuthCredentials.cpp
)
target_include_directories(confy_bitbucket_client_test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/rapidxml
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/nlohmann_json/single_include
)
target_link_libraries(confy_bitbucket_client_test PRIVATE CURL::libcurl)

add_executable(confy_download_worker_queue_test
    tests/DownloadWorkerQueueTest.cpp
    src/DownloadWorkerQueue.cpp
    src/NexusClient.cpp
    src/GitClient.cpp
    src/AuthCredentials.cpp
)
target_include_directories(confy_download_worker_queue_test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/rapidxml
)
target_link_libraries(confy_download_worker_queue_test PRIVATE wx::base CURL::libcurl)

enable_testing()
include(${CMAKE_CURRENT_SOURCE_DIR}/third_party/doctest/scripts/cmake/doctest.cmake)
doctest_discover_tests(confy_config_io_test)

add_test(
    NAME auth_credentials
    COMMAND confy_auth_credentials_test
)

add_test(
    NAME nexus_client_auth
    COMMAND confy_nexus_client_auth_test
)

add_test(
    NAME nexus_client_path_segment
    COMMAND confy_nexus_client_path_segment_test
)

add_test(
    NAME git_client
    COMMAND confy_git_client_test
)

add_test(
    NAME bitbucket_client
    COMMAND confy_bitbucket_client_test
)

add_test(
    NAME download_worker_queue
    COMMAND confy_download_worker_queue_test
)

if(MSVC)
    target_compile_options(confy PRIVATE /W4)
else()
    target_compile_options(confy PRIVATE -Wall -Wextra -Wpedantic)
endif()
