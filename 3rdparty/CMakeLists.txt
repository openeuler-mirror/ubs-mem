cmake_minimum_required(VERSION 3.14.1)

include(ExternalProject)
INCLUDE(ProcessorCount)

set(THIRD_PARTY_CMAKE_ARGS
        -DCMAKE_C_COMPILER=${CMAKE_C_COMPILER}
        -DCMAKE_CXX_COMPILER=${CMAKE_CXX_COMPILER}
        -DCMAKE_C_FLAGS=${CMAKE_C_FLAGS}
        -DCMAKE_C_FLAGS_RELEASE=${CMAKE_C_FLAGS_RELEASE}
        -DCMAKE_C_FLAGS_DEBUG=${CMAKE_C_FLAGS_DEBUG}
        -DCMAKE_CXX_FLAGS=${CMAKE_CXX_FLAGS}
        -DCMAKE_CXX_FLAGS_RELEASE=${CMAKE_CXX_FLAGS_RELEASE}
        -DCMAKE_CXX_FLAGS_DEBUG=${CMAKE_CXX_FLAGS_DEBUG}
        -DCMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE}
        -DCMAKE_INSTALL_LIBDIR=<INSTALL_DIR>/lib
        -DCMAKE_INSTALL_BINDIR=<INSTALL_DIR>/bin
        -DCMAKE_INSTALL_PREFIX=<INSTALL_DIR>/)
message(STATUS "THIRD_PARTY_CMAKE_ARGS:")
message(STATUS ${THIRD_PARTY_CMAKE_ARGS})

######################################################################
# openssl
######################################################################
add_subdirectory(openssl)

######################################################################
# hcom
######################################################################
SET(HCOM_LIBS_DIR /usr/lib64 PARENT_SCOPE)
SET(HCOM_C_INCLUDE_DIR /usr/include/hcom/capi PARENT_SCOPE)
SET(HCOM_INCLUDE_DIR /usr/include/hcom PARENT_SCOPE)

MACRO(TARGET_HCOM target)
    TARGET_INCLUDE_DIRECTORIES(${target} SYSTEM PUBLIC ${HCOM_C_INCLUDE_DIR})
    TARGET_INCLUDE_DIRECTORIES(${target} SYSTEM PUBLIC ${HCOM_INCLUDE_DIR})
    TARGET_LINK_DIRECTORIES(${target} PUBLIC ${HCOM_LIBS_DIR})
    TARGET_LINK_LIBRARIES(${target} PUBLIC hcom)
ENDMACRO()

MACRO(TARGET_HCOM_HEADER_ONLY target)
    TARGET_INCLUDE_DIRECTORIES(${target} SYSTEM PUBLIC ${HCOM_C_INCLUDE_DIR})
    TARGET_INCLUDE_DIRECTORIES(${target} SYSTEM PUBLIC ${HCOM_INCLUDE_DIR})
ENDMACRO()

######################################################################
# Spdlog
######################################################################
SET(SPDLOG_CMAKE_ARGS
        ${THIRD_PARTY_CMAKE_ARGS}
        -DBUILD_SHARED_LIBS=OFF)

EXTERNALPROJECT_ADD(spdlog
        URL ${THIRD_PARTY_DIR}/spdlog
        SOURCE_DIR ${THIRD_PARTY_DIR}/spdlog
        CMAKE_ARGS ${SPDLOG_CMAKE_ARGS}
        DOWNLOAD_COMMAND ""
        LOG_DOWNLOAD On
        LOG_CONFIGURE On
        LOG_BUILD On
        LOG_INSTALL On)

EXTERNALPROJECT_GET_PROPERTY(spdlog INSTALL_DIR)
SET(SPDLOG_LIBS_DIR ${INSTALL_DIR}/lib PARENT_SCOPE)
SET(SPDLOG_INCLUDE_DIR ${INSTALL_DIR}/include PARENT_SCOPE)

MACRO(TARGET_SPDLOG target)
    ADD_DEPENDENCIES(${target} spdlog)
    TARGET_INCLUDE_DIRECTORIES(${target} SYSTEM PUBLIC ${SPDLOG_INCLUDE_DIR})
    IF (${CMAKE_BUILD_TYPE} STREQUAL "Debug")
        TARGET_LINK_LIBRARIES(${target} PUBLIC -L${SPDLOG_LIBS_DIR} libspdlogd.a pthread)
    ELSE ()
        TARGET_LINK_LIBRARIES(${target} PUBLIC -L${SPDLOG_LIBS_DIR} libspdlog.a pthread)
    ENDIF ()
ENDMACRO()

MACRO(TARGET_SPDLOG_HEADER_ONLY target)
    ADD_DEPENDENCIES(${target} spdlog)
    TARGET_INCLUDE_DIRECTORIES(${target} SYSTEM PUBLIC ${SPDLOG_INCLUDE_DIR})
ENDMACRO()

######################################################################
# huawei_secure_c
######################################################################
add_subdirectory(secure)

SET(SECUREC_INCLUDE_DIR ${PROJECT_SOURCE_DIR}/3rdparty/secure/huawei_secure_c/include PARENT_SCOPE)

MACRO(TARGET_SECUREC_STATIC target)
    ADD_DEPENDENCIES(${target} securec_static)
    TARGET_INCLUDE_DIRECTORIES(${target} SYSTEM PUBLIC ${SECUREC_INCLUDE_DIR})
    TARGET_LINK_LIBRARIES(${target} PUBLIC securec_static)
ENDMACRO()

MACRO(TARGET_SECUREC_SHARE target)
    ADD_DEPENDENCIES(${target} securec_share)
    TARGET_INCLUDE_DIRECTORIES(${target} SYSTEM PUBLIC ${SECUREC_INCLUDE_DIR})
    TARGET_LINK_LIBRARIES(${target} PUBLIC securec_share)
ENDMACRO()

MACRO(TARGET_SECUREC_HEADER_ONLY target)
    ADD_DEPENDENCIES(${target} securec_static)
    TARGET_INCLUDE_DIRECTORIES(${target} SYSTEM PUBLIC ${SECUREC_INCLUDE_DIR})
ENDMACRO()

######################################################################
# obmm库 so,以下路径为obmm.rpm安装后的路径
######################################################################
MACRO(TARGET_OBMM_HEADER_ONLY target)
    target_include_directories(${target} SYSTEM PUBLIC ${PROJECT_SOURCE_DIR}/3rdparty/obmm_ub)
    target_include_directories(${target} SYSTEM PUBLIC ${PROJECT_SOURCE_DIR}/3rdparty/obmm_ub/linux)
ENDMACRO()

if (BUILD_GENERAL_COMPUTING STREQUAL ON)
    ######################################################################
    # smap
    ######################################################################
    MACRO(TARGET_SMAP_HEADER_ONLY target)
        target_include_directories(${target} SYSTEM PUBLIC ${PROJECT_SOURCE_DIR}/3rdparty/smap)
    ENDMACRO()
    ######################################################################
endif ()
