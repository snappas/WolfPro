#------------------------------------------------------------------------
# Return to Castle Wolfenstein
# 
# Build Linux native:
# CFLAGS=-m32 CXXFLAGS=-m32 LDFLAGS=-m32 cmake -G Ninja -DCMAKE_LIBRARY_PATH=/usr/lib/i386-linux-gnu -DCMAKE_PREFIX_PATH=/usr/lib/i386-linux-gnu/cmake/ ..
# Build Windows cross-compile on Linux:
# cmake .. -G Ninja -DCMAKE_TOOLCHAIN_FILE="../cmake/clang-cl-msvc.cmake" -DHOST_ARCH=x86 -DLLVM_NATIVE_TOOLCHAIN=/usr/ -DMSVC_BASE=../deps/xwin/crt -DWINSDK_BASE=../deps/xwin/sdk
#------------------------------------------------------------------------
cmake_minimum_required(VERSION 3.10.3 FATAL_ERROR)

if(${CMAKE_VERSION} VERSION_GREATER_EQUAL "3.21")
    cmake_policy(SET CMP0121 NEW)
endif()
if(${CMAKE_VERSION} VERSION_GREATER_EQUAL "3.22")
	cmake_policy(SET CMP0127 NEW)
endif()
if(${CMAKE_VERSION} VERSION_GREATER_EQUAL "3.24")
	cmake_policy(SET CMP0135 NEW)
endif()

set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} "${CMAKE_SOURCE_DIR}/cmake")
include(CMakeDependentOption)

#-----------------------------------------------------------------
# General
#-----------------------------------------------------------------

set(MODNAME "wolfpro" CACHE STRING "Default mod name")

#-----------------------------------------------------------------
# Options
#-----------------------------------------------------------------

#option(WOLF_64BITS "Build the 64 bit versions of everything" ON)

option(BUILD_SERVER				"Build the dedicated server executable"							ON)
option(BUILD_CLIENT				"Build the client executable"									ON)
option(BUILD_MOD				"Build the mod libraries"										ON)

option(BUNDLED_LIBS				"Use all available bundled libraries instead of system libs"	ON)
option(BUNDLED_LIBS_DEFAULT		"Default value for bundled libs"								ON)

cmake_dependent_option(BUILD_CLIENT_MOD			"Build the client side mod libraries"							ON "BUILD_MOD" OFF)
cmake_dependent_option(BUILD_SERVER_MOD			"Build the server side mod libraries"							ON "BUILD_MOD" OFF)
cmake_dependent_option(BUILD_MOD_PK3			"Pack the mod libraries and game scripts into mod pk3"			ON "BUILD_CLIENT_MOD" OFF)

# Optional features
option(FORCE_DEBUG				"Force enable debug macros"								OFF)
option(ENABLE_ASAN				"Enable AddressSanitizer on clang"						OFF)
option(ENABLE_PROFILER			"Enable the instrumentation profiler"					ON)
option(ENABLE_MULTI_BUILD		"Enable binary names to include arch"					ON)
option(ENABLE_OMNIBOT           "Enable omnibot support"                                ON)

cmake_dependent_option(FEATURE_WINDOWS_CONSOLE	"Build win32 executables with Windows console"	OFF "WIN32" OFF)

set(INSTALL_DEFAULT_BASEDIR "" CACHE STRING "")

#-----------------------------------------------------------------
# Setup
#-----------------------------------------------------------------

if(WOLF_64BITS)
else()
#set(CMAKE_ASM_FLAGS "-m32 -DELF -x assembler-with-cpp")
endif()
set(CMAKE_TRY_COMPILE_TARGET_TYPE "STATIC_LIBRARY")

project(RTCW-MP C CXX)

if(CMAKE_SIZEOF_VOID_P EQUAL 8)
	message(STATUS "64 bits target architecture detected")
	SET(WOLF_64BITS 1)
	if(WIN32)
		SET(WOLF_WIN64 1)
	endif()
elseif(NOT CMAKE_SIZEOF_VOID_P EQUAL 8 AND NOT CMAKE_SIZEOF_VOID_P EQUAL 4)
	# NOTE: this should never happen, but just in case for an invalid toolchain...
	message(FATAL_ERROR "Unknown target architecture detected. Pointer size: ${CMAKE_SIZEOF_VOID_P}")
else()
	message(STATUS "32 bits target architecture detected")
	SET(WOLF_32BITS 1)
endif()

# Group the VisualStudio projects
set(CMAKE_TARGETS_FOLDER CMake)
set(BUNDLED_TARGETS_FOLDER Bundled)
set_property(GLOBAL PROPERTY USE_FOLDERS ON)
set_property(GLOBAL PROPERTY PREDEFINED_TARGETS_FOLDER ${CMAKE_TARGETS_FOLDER})

# Set language levels
set(CMAKE_CXX_STANDARD 11)
set(CMAKE_C_STANDARD 99)

# Source globs
include(cmake/WolfSources.cmake)

# Setup some basic helper variables
include(cmake/WolfCommon.cmake)

# Dummy targets for library binding
include(cmake/WolfTargets.cmake)

# Platform specific compiler settings
include(cmake/WolfPlatform.cmake)



# Set up includes and libraries based on feature options
include(cmake/WolfSetupFeatures.cmake)

# Group the sources
include(cmake/WolfSourceGroup.cmake)

#-----------------------------------------------------------------
# Build
#-----------------------------------------------------------------

if(BUILD_CLIENT)
	include(cmake/WolfBuildClientLibs.cmake)
	include(cmake/WolfBuildClient.cmake)

	#Since CMake 3.6 you are able to set the Visual Studio startup project
    if(${CMAKE_VERSION} VERSION_GREATER_EQUAL "3.6")
        set_property(DIRECTORY PROPERTY VS_STARTUP_PROJECT "wolfmp")
    endif()
endif(BUILD_CLIENT)

if(BUILD_SERVER)
	include(cmake/WolfBuildServer.cmake)
endif(BUILD_SERVER)

if(BUILD_MOD)
	include(cmake/WolfBuildMod.cmake)
endif(BUILD_MOD)

#-----------------------------------------------------------------
# Post build
#-----------------------------------------------------------------


#set_target_properties(uninstall PROPERTIES FOLDER ${CMAKE_TARGETS_FOLDER})
