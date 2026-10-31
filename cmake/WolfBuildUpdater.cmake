# wolfpro_updater: standalone Win32 helper that applies a staged client update.
if(WIN32 AND BUILD_CLIENT)
	add_executable(wolfpro_updater WIN32
		${CMAKE_SOURCE_DIR}/src/updater/updater_main.c
		${CMAKE_SOURCE_DIR}/src/updater/updater_core.c
		${CMAKE_SOURCE_DIR}/src/client/cl_update_logic.c
		${CMAKE_SOURCE_DIR}/src/updater/wolfpro_updater.rc
	)
	# lets rc/windres find the manifest the .rc embeds
	target_include_directories(wolfpro_updater PRIVATE ${CMAKE_SOURCE_DIR}/src/updater)
	target_compile_definitions(wolfpro_updater PRIVATE UPDATER_NO_JSON _CRT_SECURE_NO_WARNINGS UNICODE _UNICODE)
	target_link_libraries(wolfpro_updater PRIVATE shell32 user32)
	set_target_properties(wolfpro_updater PROPERTIES
		OUTPUT_NAME "wolfpro_updater"
		RUNTIME_OUTPUT_DIRECTORY "${WOLF_OUTPUT_DIR}"
		RUNTIME_OUTPUT_DIRECTORY_DEBUG "${WOLF_OUTPUT_DIR}"
		RUNTIME_OUTPUT_DIRECTORY_RELEASE "${WOLF_OUTPUT_DIR}"
	)
	if(MSVC)
		# the .rc carries the manifest; a linker-generated one would collide
		target_link_options(wolfpro_updater PRIVATE /MANIFEST:NO)
		# imported DLLs load from System32 only
		if(CMAKE_C_COMPILER_ID STREQUAL "MSVC")
			target_link_options(wolfpro_updater PRIVATE /DEPENDENTLOADFLAG:0x800)
		elseif(NOT CMAKE_VERSION VERSION_LESS 3.18)
			include(CheckLinkerFlag)
			check_linker_flag(C "/DEPENDENTLOADFLAG:0x800" WOLF_HAVE_DEPENDENTLOADFLAG)
			if(WOLF_HAVE_DEPENDENTLOADFLAG)
				target_link_options(wolfpro_updater PRIVATE /DEPENDENTLOADFLAG:0x800)
			endif()
		endif()
	endif()
	if(MSVC AND NOT ENABLE_ASAN)
		if(CMAKE_BUILD_TYPE MATCHES "Debug")
			set_property(TARGET wolfpro_updater PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreadedDebug")
		else()
			set_property(TARGET wolfpro_updater PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded")
		endif()
	endif()
	install(TARGETS wolfpro_updater RUNTIME DESTINATION "${INSTALL_DEFAULT_BASEDIR}")
endif()
