cmake_minimum_required(VERSION 3.21)
set(CMAKE_WARN_DEPRECATED OFF CACHE BOOL "" FORCE)
project(chatty VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# --- CPM dependencies ---
include(cmake/CPM.cmake)

CPMAddPackage(
  NAME agt
  URL https://github.com/0x9dhcf/agt/archive/refs/heads/main.tar.gz
)

CPMAddPackage(
  NAME promptty
  URL https://github.com/0x9dhcf/promptty/archive/refs/heads/main.tar.gz
)

CPMAddPackage(
  NAME mdtty
  URL https://github.com/0x9dhcf/mdtty/archive/refs/heads/main.tar.gz
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
)

target_compile_options(chatty PRIVATE ${CHATTY_WARNINGS})

find_package(SQLite3 REQUIRED)

target_link_libraries(chatty
  PRIVATE agt::agt promptty::promptty mdtty::mdtty SQLite::SQLite3
)

if(CMAKE_BUILD_TYPE STREQUAL "Release")
  target_compile_options(chatty PRIVATE -march=native)
endif()

# --- Install ---
include(GNUInstallDirs)
install(TARGETS chatty RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
