
add_library(ubse-client SHARED ubse_sdk.cpp)
target_link_libraries(ubse-client securec_static)
target_include_directories(ubse-client PRIVATE ${PROJECT_SOURCE_DIR}/3rdparty/ubse/include)
install(TARGETS ubse-client DESTINATION ${CMAKE_LIBRARY_OUTPUT_DIRECTORY} PERMISSIONS OWNER_READ OWNER_EXECUTE GROUP_READ GROUP_EXECUTE)