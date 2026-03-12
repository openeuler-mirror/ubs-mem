#build secure
set(SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/huawei_secure_c)
set(BUILD_DIR ${PROJECT_BINARY_DIR}/3rdparty/secure/)
set(SECURE_INSTALL_DIR ${BUILD_DIR}/huawei_secure_c/)

message("-- secure: check if need to build, find file in dir:${SECURE_INSTALL_DIR}")

# build the lib if not built yet
set(LIBS_BUILT_SECURE_C "${SECURE_INSTALL_DIR}/libsecurec.a")
# check if the file exists or not, if not build it
if(NOT EXISTS "${LIBS_BUILT_SECURE_C}")
    set(CMAKE_C_STANDARD 11)
    set(CMAKE_C_STANDARD 11)
    set(CMAKE_CXX_STANDARD 11)
    SET(CMAKE_C_FLAGS "-s")
    aux_source_directory(${PROJECT_SOURCE_DIR}/3rdparty/secure/huawei_secure_c/src SECURE_SRC)
    add_library(securec_object OBJECT ${SECURE_SRC})
    target_include_directories(securec_object PUBLIC ${PROJECT_SOURCE_DIR}/3rdparty/secure/huawei_secure_c/include)

    add_library(securec_static STATIC)
    target_link_libraries(securec_static PRIVATE securec_object)
    
    add_library(securec_share SHARED)
    target_link_libraries(securec_share PRIVATE securec_object)
    set_target_properties(securec_share PROPERTIES OUTPUT_NAME "securec")
    set_target_properties(securec_share PROPERTIES CLEAN_DIRECT_OUTPUT 1)
else()
    message("-- secure: ${LIBS_BUILT_SECURE_C}")
    message("-- secure: has been built, ignored")
endif()