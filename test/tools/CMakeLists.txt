add_subdirectory(benchmarks)

add_executable(multi_provider_shmem_test multi_provider_shmem_test.cpp)
target_include_directories(multi_provider_shmem_test PRIVATE ${PROJECT_SOURCE_DIR}/src/app_lib/include)
target_link_libraries(multi_provider_shmem_test PRIVATE ubsm_sdk_shared)
install(TARGETS multi_provider_shmem_test DESTINATION ${MATRIX_MEM_TEST_BINARY_DIR}
        PERMISSIONS ${DAEMON_INSTALL_EXE_PERMISSIONS})
