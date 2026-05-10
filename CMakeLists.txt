cmake_minimum_required(VERSION 3.20)
project(lwdb_embedded LANGUAGES CXX)

add_library(lwdb-embedded
    src/core/db.cpp
    plugins/storage_memory/memory_storage.cpp
)

target_include_directories(lwdb-embedded
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)
