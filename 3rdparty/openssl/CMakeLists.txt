set(OPENSSL_SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/openssl)
set(OPENSSL_BUILD_DIR ${PROJECT_BINARY_DIR}/3rdparty/openssl)
set(OPENSSL_INSTALL_DIR ${OPENSSL_BUILD_DIR}/output)

set(CHECK_FILE_OPENSSL ${OPENSSL_INSTALL_DIR}/lib/libssl.so)
message("-- openssl : check if need to build ")

# build the lib if not built yet
if(EXISTS ${CHECK_FILE_OPENSSL})
    message("-- openssl: ${CHECK_FILE_OPENSSL}")
    message("-- openssl: has been built, ignored")
else()
    message("-- openssl: need build.")
    # create build dir
    exec_program(mkdir ${PROJECT_3RDPARTY_BIN_DIR} ARGS -p ${OPENSSL_INSTALL_DIR})

    # configure
    exec_program(chmod ${OPENSSL_SOURCE_DIR} ARGS +x config)
    exec_program(chmod ${OPENSSL_SOURCE_DIR} ARGS +x Configure)
    exec_program(dos2unix ${OPENSSL_SOURCE_DIR} ARGS config)
    exec_program(dos2unix ${OPENSSL_SOURCE_DIR} ARGS Configure)
    exec_program(./config ${OPENSSL_SOURCE_DIR} ARGS --prefix=${OPENSSL_INSTALL_DIR}
            no-tests
            CFLAGS="-fstack-protector-strong -O2 -D_FORTIFY_SOURCE=2"
            CXXFLAGS="-fstack-protector-strong -O2 -D_FORTIFY_SOURCE=2"
            LDFLAGS="-Wl,-z,now -s -Wl,--build-id=none"
            )

    # execute make && make install
    exec_program(make ${OPENSSL_SOURCE_DIR} ARGS clean)
    exec_program(make ${OPENSSL_SOURCE_DIR} ARGS -s -j16 build_libs)
    exec_program(make ${OPENSSL_SOURCE_DIR} ARGS -s install_sw)
endif()