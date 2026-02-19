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
)

set(CONFY_HEADERS
    src/App.h
    src/MainFrame.h
    src/ConfigModel.h
    src/ConfigLoader.h
)

find_package(wxWidgets REQUIRED COMPONENTS core base)
include(${wxWidgets_USE_FILE})

add_executable(confy ${CONFY_SOURCES} ${CONFY_HEADERS})
target_include_directories(confy PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/third_party/rapidxml)
target_link_libraries(confy PRIVATE ${wxWidgets_LIBRARIES})

add_executable(confy_parser_smoke
    tests/ParserSmokeTest.cpp
    src/ConfigLoader.cpp
)
target_include_directories(confy_parser_smoke PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/rapidxml
)

enable_testing()
add_test(
    NAME parser_smoke
    COMMAND confy_parser_smoke ${CMAKE_CURRENT_SOURCE_DIR}/samples/config.sample.xml
)

if(MSVC)
    target_compile_options(confy PRIVATE /W4)
else()
    target_compile_options(confy PRIVATE -Wall -Wextra -Wpedantic)
endif()