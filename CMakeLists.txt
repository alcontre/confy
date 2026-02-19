cmake_minimum_required(VERSION 3.20)

project(confy VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

set(CONFY_SOURCES
    src/main.cpp
    src/App.cpp
    src/MainFrame.cpp
    src/ConfigLoader.cpp
    src/DownloadWorkerQueue.cpp
    src/DownloadProgressDialog.cpp
    src/NexusClient.cpp
    src/AuthCredentials.cpp
)

set(CONFY_HEADERS
    src/App.h
    src/MainFrame.h
    src/ConfigModel.h
    src/ConfigLoader.h
    src/JobTypes.h
    src/DownloadWorkerQueue.h
    src/DownloadProgressDialog.h
    src/NexusClient.h
    src/AuthCredentials.h
)

find_package(wxWidgets REQUIRED COMPONENTS core base)
find_package(CURL REQUIRED)
include(${wxWidgets_USE_FILE})

add_executable(confy ${CONFY_SOURCES} ${CONFY_HEADERS})
target_include_directories(confy PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/rapidxml
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/nlohmann_json/single_include
)
target_link_libraries(confy PRIVATE ${wxWidgets_LIBRARIES} CURL::libcurl)

add_executable(confy_parser_smoke
    tests/ParserSmokeTest.cpp
    src/ConfigLoader.cpp
)
target_include_directories(confy_parser_smoke PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/rapidxml
)

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
)
target_include_directories(confy_nexus_client_auth_test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

enable_testing()
add_test(
    NAME parser_smoke
    COMMAND confy_parser_smoke ${CMAKE_CURRENT_SOURCE_DIR}/samples/config.sample.xml
)

add_test(
    NAME auth_credentials
    COMMAND confy_auth_credentials_test
)

add_test(
    NAME nexus_client_auth
    COMMAND confy_nexus_client_auth_test
)

if(MSVC)
    target_compile_options(confy PRIVATE /W4)
else()
    target_compile_options(confy PRIVATE -Wall -Wextra -Wpedantic)
endif()
