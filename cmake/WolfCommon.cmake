#-----------------------------------------------------------------
# Common
#-----------------------------------------------------------------
	#SET(CMAKE_BUILD_TYPE "Debug")
	#SET(CMAKE_BUILD_TYPE "Release")
	#MESSAGE("No CMAKE_BUILD_TYPE specified, defaulting to ${CMAKE_BUILD_TYPE}")

string(LENGTH "${CMAKE_SOURCE_DIR}/" SOURCE_PATH_SIZE)
add_definitions("-DSOURCE_PATH_SIZE=${SOURCE_PATH_SIZE}")

# CMAKE_LIBRARY_ARCHITECTURE isn't reliably auto-detected in every container/
# toolchain combination, which silently breaks find_package() calls (OpenGL,
# SDL2, etc.) that rely on it to search Debian/Ubuntu's multiarch lib dirs.
# Hint those dirs explicitly so neither MODULE-mode (find_library, e.g.
# FindOpenGL/FindVulkan) nor CONFIG-mode (e.g. SDL2's sdl2-config.cmake)
# searches depend on that detection.
if(UNIX AND NOT APPLE)
	list(APPEND CMAKE_LIBRARY_PATH /usr/lib/x86_64-linux-gnu /usr/lib/i386-linux-gnu)
	list(APPEND CMAKE_PREFIX_PATH /usr/lib/x86_64-linux-gnu /usr/lib/i386-linux-gnu)
endif()

# set WOLF_DEBUG definition for debug build type
# and set up properties to check if the build is visual studio or nmake on windows
string(TOUPPER "${CMAKE_BUILD_TYPE}" buildtype_upper)
string(TOUPPER "${CMAKE_GENERATOR}" buildgen_upper)
if(WIN32 AND buildgen_upper MATCHES "NMAKE MAKEFILES")
	SET(NMAKE_BUILD 1)
else()
	SET(NMAKE_BUILD 0)
endif()

if(buildtype_upper STREQUAL DEBUG)
	SET(DEBUG_BUILD 1)
else()
	SET(DEBUG_BUILD 0)
endif()

if(WIN32 AND buildgen_upper STREQUAL "NINJA")
	SET(NINJA_BUILD 1)
else()
	SET(NINJA_BUILD 0)
endif()

# Since Clang is now a thing with Visual Studio
if(CMAKE_C_COMPILER_ID MATCHES "Clang" AND MSVC)
    set(MSVC_CLANG 1)
endif()

if(MSVC AND NOT NMAKE_BUILD AND NOT NINJA_BUILD)
	SET(VSTUDIO 1)
else()
	SET(VSTUDIO 0)
endif()

if(DEBUG_BUILD OR FORCE_DEBUG)
	add_definitions(-DWOLF_DEBUG=1)
	if(WIN32)
		add_definitions(-DDUMP_MEMLEAKS=1)
	endif()
else()
	if(MSVC)
		set(CMAKE_C_FLAGS_DEBUG "${CMAKE_C_FLAGS_DEBUG} /DWOLF_DEBUG=1")
		set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} /DWOLF_DEBUG=1")
	else()
		if(WIN32)
			set(CMAKE_C_FLAGS_DEBUG "${CMAKE_C_FLAGS_DEBUG} -DWOLF_DEBUG=1")
			set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -DWOLF_DEBUG=1")
		else()
			set(CMAKE_C_FLAGS_DEBUG "${CMAKE_C_FLAGS_DEBUG} -DWOLF_DEBUG=1")
			set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -DWOLF_DEBUG=1")
		endif()
	endif()
endif()

if(ENABLE_PROFILER)
	add_definitions(-DENABLE_PROFILER=1)
endif()

string(TOLOWER "${CMAKE_SYSTEM_PROCESSOR}" system_name_lower)

if(system_name_lower MATCHES "(i386)|(i686)|(x86)|(amd64)")
	message(STATUS "x86 architecture detected")
	set(WOLF_X86 1)
elseif(system_name_lower MATCHES "(arm)|(aarch64)")
	message(STATUS "ARM architecture detected")
	set(WOLF_ARM 1)
else()
	message(WARNING "Unknown architecture detected: ${CMAKE_SYSTEM_PROCESSOR}")
endif()

# Installation options
# If we are in windows clean these so the packaging is cleaner
# these need to be set before any other processing happens!
if(WIN32)
	
else()
	set(INSTALL_DEFAULT_BASEDIR ""					CACHE STRING "Appended to CMAKE_INSTALL_PREFIX")
	set(INSTALL_DEFAULT_BINDIR "bin"				CACHE STRING "Appended to CMAKE_INSTALL_PREFIX")
	set(INSTALL_DEFAULT_SHAREDIR "share"			CACHE STRING "Appended to CMAKE_INSTALL_PREFIX")
	set(INSTALL_DEFAULT_MODDIR "lib/wolfpro"		CACHE STRING "Appended to CMAKE_INSTALL_PREFIX")
endif()

if(INSTALL_DEFAULT_BASEDIR)
	# On OS X the base dir is the .app's parent path, and is set in the code itself
	# so we do NOT want to define DEFAULT_BASEDIR at build time.
	if(NOT APPLE)
		add_definitions(-DDEFAULT_BASEDIR=\"${INSTALL_DEFAULT_BASEDIR}\")
	endif()
endif()

if (ENABLE_SSE)
	if (APPLE AND CMAKE_OSX_ARCHITECTURES)
		list(LENGTH CMAKE_OSX_ARCHITECTURES OSX_ARCH_COUNT)
	endif()

	if (CMAKE_CROSSCOMPILING OR OSX_ARCH_COUNT GREATER "1")
		message(VERBOSE "We are crosscompiling, so we skip the SSE test")
		add_definitions(-DETL_ENABLE_SSE=1)
	else()
		include(CheckCSourceCompiles)
		check_c_source_compiles("
		#include <immintrin.h>
		int main()
		{
			__m128 tmp;
			float result = 0.f;
			tmp = _mm_set_ss(12.f);
			tmp = _mm_rsqrt_ss(tmp);
			result = _mm_cvtss_f32(tmp);
			return 0;
		}" WOLF_ENABLE_SSE)

		if (WOLF_ENABLE_SSE)
			message(STATUS "x86 intrinsics available")
			add_definitions(-DWOLF_ENABLE_SSE=1)
		else()
			message(WARNING "No x86 intrinsics available while trying to enable it")
		endif()
	endif()
endif()

#-----------------------------------------------------------------
# cURL
#-----------------------------------------------------------------
find_package(CURL)
if(CURL_FOUND)
	
	set(CMAKE_REQUIRED_INCLUDES ${CURL_INCLUDE_DIR})
	set(CMAKE_REQUIRED_LIBRARIES ${CURL_LIBRARY})
	message(STATUS "CURL Include dir: " ${CURL_INCLUDE_DIR})
	message(STATUS "CURL library: " ${CURL_LIBRARY})
endif()

add_library(cimgui STATIC ${RENDERER_CIMGUI_FILES})
set_property(TARGET cimgui PROPERTY POSITION_INDEPENDENT_CODE ON)
target_include_directories(cimgui PRIVATE src/cimgui)

set_target_properties(cimgui PROPERTIES
	COMPILE_DEFINITIONS "${WOLF_COMPILE_DEF}"
	RUNTIME_OUTPUT_DIRECTORY "${WOLF_OUTPUT_DIR}"
	RUNTIME_OUTPUT_DIRECTORY_DEBUG "${WOLF_OUTPUT_DIR}"
	RUNTIME_OUTPUT_DIRECTORY_RELEASE "${WOLF_OUTPUT_DIR}"
)

add_library(cimplot STATIC ${RENDERER_CIMPLOT_FILES})
set_property(TARGET cimplot PROPERTY POSITION_INDEPENDENT_CODE ON)
target_include_directories(cimplot PRIVATE src/cimplot src/cimplot/implot)
# PUBLIC: cimplot.h itself does `#include "cimgui.h"` (bare, resolved relative to
# cimplot.h's own directory first), so any consumer that includes cimplot.h - e.g.
# src/client/cl_profiler.c via "../cimplot/cimplot.h" - needs src/cimgui (and
# src/cimgui/imgui, for the underlying Dear ImGui headers cimgui.h itself pulls in)
# on its own include path too. Without PUBLIC here that bare include only resolves
# inside cimplot.cpp's own translation unit, not in consumers linked via
# target_link_libraries(... cimplot).
target_include_directories(cimplot PUBLIC src/cimgui src/cimgui/imgui)

set_target_properties(cimplot PROPERTIES
	COMPILE_DEFINITIONS "${WOLF_COMPILE_DEF}"
	RUNTIME_OUTPUT_DIRECTORY "${WOLF_OUTPUT_DIR}"
	RUNTIME_OUTPUT_DIRECTORY_DEBUG "${WOLF_OUTPUT_DIR}"
	RUNTIME_OUTPUT_DIRECTORY_RELEASE "${WOLF_OUTPUT_DIR}"
)
