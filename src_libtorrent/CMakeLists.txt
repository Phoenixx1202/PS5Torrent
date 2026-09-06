cmake_minimum_required(VERSION 3.20)

project(ps5torrent_libtorrent_lab LANGUAGES CXX)

if(NOT PS5)
  message(FATAL_ERROR
    "Use the PS5 toolchain: -DCMAKE_TOOLCHAIN_FILE=$ENV{PS5_PAYLOAD_SDK}/toolchain/prospero.cmake")
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(LIBTORRENT_SOURCE_DIR "" CACHE PATH "Path to the pinned libtorrent source")
set(BOOST_ROOT "" CACHE PATH "Path to the Boost release tree")

if(NOT EXISTS "${LIBTORRENT_SOURCE_DIR}/CMakeLists.txt")
  message(FATAL_ERROR "LIBTORRENT_SOURCE_DIR does not contain libtorrent")
endif()

if(NOT EXISTS "${BOOST_ROOT}/boost/version.hpp")
  message(FATAL_ERROR "BOOST_ROOT does not contain Boost headers")
endif()

set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
set(build_tests OFF CACHE BOOL "" FORCE)
set(build_examples OFF CACHE BOOL "" FORCE)
set(build_tools OFF CACHE BOOL "" FORCE)
set(python-bindings OFF CACHE BOOL "" FORCE)
set(dht ON CACHE BOOL "" FORCE)
set(encryption ON CACHE BOOL "" FORCE)
set(extensions ON CACHE BOOL "" FORCE)
set(i2p OFF CACHE BOOL "" FORCE)
set(logging ON CACHE BOOL "" FORCE)
set(mutable-torrents OFF CACHE BOOL "" FORCE)
set(streaming OFF CACHE BOOL "" FORCE)
set(deprecated-functions OFF CACHE BOOL "" FORCE)
set(Boost_NO_SYSTEM_PATHS ON CACHE BOOL "" FORCE)
set(Boost_NO_BOOST_CMAKE ON CACHE BOOL "" FORCE)
set(Boost_INCLUDE_DIR "${BOOST_ROOT}" CACHE PATH "" FORCE)

add_subdirectory("${LIBTORRENT_SOURCE_DIR}" libtorrent-build EXCLUDE_FROM_ALL)

add_executable(ps5torrent-libtorrent-lab.elf
  ps5torrent_libtorrent_lab.cpp)
target_compile_definitions(ps5torrent-libtorrent-lab.elf PRIVATE
  PS5TORRENT_VERSION="2.0.4")
target_compile_options(ps5torrent-libtorrent-lab.elf PRIVATE
  -Wall -Wextra -Werror)
target_link_libraries(ps5torrent-libtorrent-lab.elf PRIVATE
  torrent-rasterbar
  SceSystemService)
