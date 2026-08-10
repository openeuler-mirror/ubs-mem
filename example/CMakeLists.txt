cmake_minimum_required(VERSION 3.13)
project(ubs_mem_examples LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(UBSM_ROOT "/usr/local/ubs_mem" CACHE PATH "Installed UBS Memory SDK prefix")
find_path(UBSM_INCLUDE_DIR ubs_mem.h PATHS "${UBSM_ROOT}/include" NO_DEFAULT_PATH)
find_library(UBSM_LIBRARY ubsm_sdk PATHS "${UBSM_ROOT}/lib" NO_DEFAULT_PATH)

if(NOT UBSM_INCLUDE_DIR OR NOT UBSM_LIBRARY)
    message(FATAL_ERROR "UBS Memory SDK not found; set UBSM_ROOT to its installation prefix")
endif()

add_executable(cache_shmem_example cache_shmem_example.cpp)
target_include_directories(cache_shmem_example PRIVATE ${UBSM_INCLUDE_DIR})
target_link_libraries(cache_shmem_example PRIVATE ${UBSM_LIBRARY})
