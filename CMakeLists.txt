# SPDX-License-Identifier: GPL-3.0-or-later
cmake_minimum_required(VERSION 3.20)
project(crau-nbd
    VERSION 0.1.0
    DESCRIPTION "Linux Network Block Device server for ChromeOS/Android CrAU payload.bin"
    HOMEPAGE_URL "https://github.com/BlissRoms/crau-nbd"
    LANGUAGES CXX C
)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include(GNUInstallDirs)
option(BUILD_STATIC "Build statically linked executable" OFF)
if(BUILD_STATIC)
    set(BUILD_SHARED_LIBS OFF)
    set(CMAKE_FIND_LIBRARY_SUFFIXES ".a")
    set(PKG_CONFIG_USE_STATIC_LIBS ON)
    set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -static")
endif()


# GNU and POSIX feature test macros
add_compile_definitions(_GNU_SOURCE _POSIX_C_SOURCE=200809L)

# Compiler warnings conforming to GNU/LLVM standards
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    add_compile_options(-Wall -Wextra -Wpedantic -Wformat=2 -Wno-deprecated-declarations)
endif()

# Dependencies
find_package(Threads REQUIRED)
find_package(Protobuf REQUIRED)
find_package(PkgConfig REQUIRED)

pkg_check_modules(PROTOBUF REQUIRED IMPORTED_TARGET protobuf)
pkg_check_modules(LZMA REQUIRED IMPORTED_TARGET liblzma)
pkg_check_modules(BZ2 REQUIRED IMPORTED_TARGET bzip2)

# Generate Protobuf C++ sources
protobuf_generate_cpp(PROTO_SRCS PROTO_HDRS proto/update_metadata.proto)

set(CRAU_NBD_SOURCES
    src/main.cpp
    src/error.cpp
    src/source_provider.cpp
    src/crau_parser.cpp
    src/extent_index.cpp
    src/block_cache.cpp
    src/decompressor.cpp
    src/nbd_server.cpp
    ${PROTO_SRCS}
    ${PROTO_HDRS}
)

add_executable(crau-nbd ${CRAU_NBD_SOURCES})

target_include_directories(crau-nbd PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_BINARY_DIR}
    ${Protobuf_INCLUDE_DIRS}
)

target_link_libraries(crau-nbd PRIVATE
    PkgConfig::PROTOBUF
    Threads::Threads
    PkgConfig::LZMA
    PkgConfig::BZ2
)

# Automated test runner
add_executable(test_reader
    tests/test_reader.cpp
    src/error.cpp
    src/source_provider.cpp
    src/crau_parser.cpp
    src/extent_index.cpp
    src/block_cache.cpp
    src/decompressor.cpp
    ${PROTO_SRCS}
    ${PROTO_HDRS}
)

target_include_directories(test_reader PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_BINARY_DIR}
    ${Protobuf_INCLUDE_DIRS}
)

target_link_libraries(test_reader PRIVATE
    PkgConfig::PROTOBUF
    Threads::Threads
    PkgConfig::LZMA
    PkgConfig::BZ2
)

enable_testing()
add_test(NAME test_reader COMMAND test_reader /home/shadichy/Downloads/lineage-21.0-20260331-UNOFFICIAL-x86_64_tv_go-signed.zip)

# Installation targets conforming to GNU and FreeDesktop standards
install(TARGETS crau-nbd
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

install(PROGRAMS scripts/crau-nbd-mount.sh
    DESTINATION ${CMAKE_INSTALL_BINDIR}
    RENAME crau-nbd-mount
)

install(FILES doc/crau-nbd.1
    DESTINATION ${CMAKE_INSTALL_MANDIR}/man1
)

install(FILES data/polkit/org.blissos.craunbd.policy
    DESTINATION ${CMAKE_INSTALL_DATADIR}/polkit-1/actions
)

install(FILES data/desktop/org.blissos.craunbd.desktop
    DESTINATION ${CMAKE_INSTALL_DATADIR}/applications
)

install(FILES data/mime/org.blissos.craunbd.xml
    DESTINATION ${CMAKE_INSTALL_DATADIR}/mime/packages
)
