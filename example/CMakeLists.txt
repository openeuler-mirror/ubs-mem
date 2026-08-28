cmake_minimum_required(VERSION 3.13)
project(ubs_mem_examples LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(UBSM_ROOT "" CACHE PATH "Optional UBS Memory SDK installation prefix")
find_path(UBSM_INCLUDE_DIR ubs_mem.h HINTS "${UBSM_ROOT}" PATH_SUFFIXES include include/ubs_mem)
find_library(UBSM_LIBRARY ubsm_sdk HINTS "${UBSM_ROOT}" PATH_SUFFIXES lib lib64)

if(NOT UBSM_INCLUDE_DIR OR NOT UBSM_LIBRARY)
    message(FATAL_ERROR "UBS Memory SDK not found; set UBSM_ROOT to its installation prefix")
endif()

add_executable(cache_shmem_example cache_shmem_example.cpp)
target_include_directories(cache_shmem_example PRIVATE ${UBSM_INCLUDE_DIR})
target_link_libraries(cache_shmem_example PRIVATE ${UBSM_LIBRARY})
