# - Find curl

if(WOLF_64BITS)
	set(DEPS deps64)
	set(CURL_NAMES libcurl-x64 curl-x64)
	message(${CURL_NAMES})
endif()
if(WOLF_32BITS)
	set(DEPS deps)
	set(CURL_NAMES libcurl-x86 curl-x86)
endif()




if(CMAKE_CROSSCOMPILING)
find_path(CURL_INCLUDE_DIR curl/curlver.h
	${PROJECT_SOURCE_DIR}/${DEPS}/curl-win/curl/include/
	${PROJECT_SOURCE_DIR}/${DEPS}/curl/build/include/
	/usr/include
	/usr/local/include
	/sw/include
	/opt/local/include
	DOC "The directory where curlver.h resides"
)
find_library(CURL_LIBRARY
	NAMES ${CURL_NAMES} libcurl
	PATHS
    ${PROJECT_SOURCE_DIR}/${DEPS}/bin
    ${PROJECT_SOURCE_DIR}/${DEPS}/curl/bin
    ${PROJECT_SOURCE_DIR}/${DEPS}/curl/build
	${PROJECT_SOURCE_DIR}/${DEPS}/curl/build-win
    ${PROJECT_SOURCE_DIR}/${DEPS}/curl-win/curl/bin
	/usr/lib64
	/usr/lib
	/usr/local/lib64
	/usr/local/lib
	/sw/lib
	/opt/local/lib
	DOC "CURL library"
)
else()
if(UNIX)
find_path(CURL_INCLUDE_DIR curl/curlver.h
	/usr/include/x86_64-linux-gnu
	/usr/include
	/usr/local/include
	/sw/include
	/opt/local/include
	DOC "The directory where curlver.h resides"
)
else()
find_path(CURL_INCLUDE_DIR curl/curlver.h
	${PROJECT_SOURCE_DIR}/${DEPS}/curl-win/curl/include/
	${PROJECT_SOURCE_DIR}/${DEPS}/curl/include
	DOC "The directory where curlver.h resides"
)
endif()
if(UNIX)
find_library(CURL_LIBRARY
	NAMES ${CURL_NAMES} curl
	PATHS
	/usr/lib/x86_64-linux-gnu
	/usr/lib/i386-linux-gnu/
	/usr/lib64
	/usr/lib
	/usr/local/lib64
	/usr/local/lib
	/sw/lib
	/opt/local/lib
	DOC "CURL library"
)
else()
find_library(CURL_LIBRARY
	NAMES ${CURL_NAMES} libcurl
	PATHS
    ${PROJECT_SOURCE_DIR}/${DEPS}/bin
    ${PROJECT_SOURCE_DIR}/${DEPS}/curl/bin
    ${PROJECT_SOURCE_DIR}/${DEPS}/curl/build/lib
	${PROJECT_SOURCE_DIR}/${DEPS}/curl/build-win
	${PROJECT_SOURCE_DIR}/${DEPS}/curl-win/curl/bin
	${PROJECT_SOURCE_DIR}/${DEPS}/curl/bin/
	DOC "CURL library"
)
endif()
endif()
# Determine curl version
if(CURL_INCLUDE_DIR AND EXISTS "${CURL_INCLUDE_DIR}/curlver.h")
	file(STRINGS "${CURL_INCLUDE_DIR}/curlver.h" curl_version_str REGEX "^#define LIBCURL_VERSION[ ]+[0-9].[0-9].[0-9]")
	string(REGEX REPLACE "^#define LIBCURL_VERSION[ ]+([^\"]*).*" "\\1" CURL_VERSION_STRING "${curl_version_str}")
	unset(curl_version_str)
endif()

# handle the QUIETLY and REQUIRED arguments and set CURL_FOUND to TRUE if
# all listed variables are TRUE
include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(CURL
	REQUIRED_VARS CURL_LIBRARY CURL_INCLUDE_DIR
	VERSION_VAR CURL_VERSION_STRING)

if(CURL_FOUND)
	set(CURL_LIBRARIES ${CURL_LIBRARY})
endif()
