cmake_minimum_required(VERSION 3.21)
set(CMAKE_WARN_DEPRECATED OFF CACHE BOOL "" FORCE)
project(chatty VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# --- Generated version header ---
configure_file(
  ${CMAKE_CURRENT_SOURCE_DIR}/cmake/version.hpp.in
  ${CMAKE_CURRENT_BINARY_DIR}/include/chatty/version.hpp
  @ONLY
)

# --- CPM dependencies ---
include(cmake/CPM.cmake)

CPMAddPackage(
  NAME agt
  GITHUB_REPOSITORY 0x9dhcf/agt
  GIT_TAG v0.6.4
  EXCLUDE_FROM_ALL YES
  SYSTEM YES
)

CPMAddPackage(
  NAME promptty
  GITHUB_REPOSITORY 0x9dhcf/promptty
  GIT_TAG v0.2.3
  EXCLUDE_FROM_ALL YES
  SYSTEM YES
)

CPMAddPackage(
  NAME mdtty
  GITHUB_REPOSITORY 0x9dhcf/mdtty
  GIT_TAG v0.2.0
  EXCLUDE_FROM_ALL YES
  SYSTEM YES
)

CPMAddPackage(
  NAME tomlplusplus
  GITHUB_REPOSITORY marzer/tomlplusplus
  VERSION 3.4.0
  EXCLUDE_FROM_ALL YES
  SYSTEM YES
)

# --- Sources ---
file(GLOB CHATTY_SOURCES CONFIGURE_DEPENDS src/*.cpp)

# --- Warning flags ---
set(CHATTY_WARNINGS
  -Wall -Wextra -Wpedantic -Wconversion -Wshadow
  -Wdouble-promotion -Wformat=2 -Wnull-dereference
)

# --- Debug sanitizers ---
if(CMAKE_BUILD_TYPE STREQUAL "Debug")
  add_compile_options(-fsanitize=address,undefined -fno-omit-frame-pointer)
  add_link_options(-fsanitize=address,undefined)
endif()

# --- Release LTO ---
if(CMAKE_BUILD_TYPE STREQUAL "Release")
  set(CMAKE_INTERPROCEDURAL_OPTIMIZATION ON)
endif()

# --- Executable target ---
add_executable(chatty ${CHATTY_SOURCES})

target_include_directories(chatty
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src
          ${CMAKE_CURRENT_BINARY_DIR}/include
)

target_compile_options(chatty PRIVATE ${CHATTY_WARNINGS})

find_package(SQLite3 REQUIRED)
find_package(CURL REQUIRED)

target_link_libraries(chatty
  PRIVATE agt::agt promptty::promptty mdtty::mdtty tomlplusplus::tomlplusplus SQLite::SQLite3
          CURL::libcurl
)

if(CMAKE_BUILD_TYPE STREQUAL "Release")
  target_compile_options(chatty PRIVATE -march=native)
endif()

# --- Install ---
include(GNUInstallDirs)
install(TARGETS chatty RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
