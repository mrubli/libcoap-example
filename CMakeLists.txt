cmake_minimum_required(VERSION 3.10)
project(libcoap-minimal CXX)

# Set C++ standard
set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Find libcoap headers and library
if(NOT DEFINED LIBCOAP_SOURCEDIR)
	set(LIBCOAP_SOURCEDIR ${CMAKE_CURRENT_SOURCE_DIR}/../libcoap)
endif()
if(NOT DEFINED LIBCOAP_BUILDDIR)
	set(LIBCOAP_BUILDDIR ${CMAKE_CURRENT_SOURCE_DIR}/../libcoap/builddir)
endif()
find_path(LIBCOAP_INCLUDE_DIRS
	NAMES coap3/coap.h coap3/coap_defines.h
	PATHS ${LIBCOAP_SOURCEDIR}/include ${LIBCOAP_BUILDDIR}/include
	NO_DEFAULT_PATH
)
find_library(LIBCOAP_LIBRARIES
	NAMES coap-3
	PATHS ${LIBCOAP_BUILDDIR}
	NO_DEFAULT_PATH
)
if(NOT LIBCOAP_INCLUDE_DIRS)
	message(FATAL_ERROR "Could not find libcoap headers in ${LIBCOAP_SOURCEDIR}/include and ${LIBCOAP_BUILDDIR}/include.")
endif()
if(NOT LIBCOAP_LIBRARIES)
	message(FATAL_ERROR "Could not find libcoap library in ${LIBCOAP_BUILDDIR}.")
endif()

# Add compiler warnings
add_compile_options(-Wall -Wextra)

# Define executables
add_executable(client client.cc common.cc)
add_executable(server server.cc)

# Link libraries
target_link_libraries(client PRIVATE ${LIBCOAP_LIBRARIES})
target_link_libraries(server PRIVATE ${LIBCOAP_LIBRARIES})

# Include directories
target_include_directories(client PRIVATE ${LIBCOAP_INCLUDE_DIRS})
target_include_directories(server PRIVATE ${LIBCOAP_INCLUDE_DIRS})
