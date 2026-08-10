#-----------------------------------------------------------------
# Build Server
#-----------------------------------------------------------------

if(WIN32 AND NOT CMAKE_CROSSCOMPILE)
	add_executable(wolfded WIN32 ${COMMON_SRC} ${SERVER_SRC})
	target_link_libraries(wolfded
		server_libraries
		engine_libraries
		os_libraries
		${CURL_LIBRARIES}
		${LZMA_LIBRARIES}
	)
	if(ENABLE_ASAN)
	target_link_options(wolfded PRIVATE /wholearchive:clang_rt.asan-x86_64.lib /STACK:8388608)
	endif()
	target_link_options(wolfded PRIVATE /STACK:8388608)
	target_include_directories(wolfded PRIVATE ${CURL_INCLUDE_DIR} ${LZMA_INCLUDE_DIR})
elseif(UNIX)
	add_executable(wolfded ${COMMON_SRC} ${SERVER_SRC})
	if(WOLF_64BITS)
	else()
		target_link_options(wolfded PRIVATE "LINKER:-melf_i386")
	endif()
	target_link_libraries(wolfded
		server_libraries
		engine_libraries
		os_libraries
		${CURL_LIBRARIES}
		${LZMA_LIBRARIES}
	)
	target_include_directories(wolfded PRIVATE ${CURL_INCLUDE_DIR} ${LZMA_INCLUDE_DIR})
endif()

if(WOLF_64BITS)
	set(WOLFDED_COMPILE_DEF "DEDICATED;USE_ICON;BOTLIB")
else()
	set(WOLFDED_COMPILE_DEF "DEDICATED;USE_ICON;BOTLIB;__i386__")
endif()

if(UNIX)
	set_target_properties(wolfded
		PROPERTIES COMPILE_DEFINITIONS "${WOLFDED_COMPILE_DEF}"
		RUNTIME_OUTPUT_DIRECTORY ""
		RUNTIME_OUTPUT_DIRECTORY_DEBUG ""
		RUNTIME_OUTPUT_DIRECTORY_RELEASE ""
	)
elseif(WIN32 AND NOT CMAKE_CROSSCOMPILE)
	set_target_properties(wolfded
		PROPERTIES COMPILE_DEFINITIONS "${WOLFDED_COMPILE_DEF}"
		RUNTIME_OUTPUT_DIRECTORY ""
		RUNTIME_OUTPUT_DIRECTORY_DEBUG ""
		RUNTIME_OUTPUT_DIRECTORY_RELEASE ""
	)
endif()

if((UNIX) AND NOT APPLE AND NOT ANDROID)
	set_target_properties(wolfded PROPERTIES SUFFIX "${BIN_SUFFIX}")
endif()

if(MSVC AND NOT EXISTS ${CMAKE_CURRENT_BINARY_DIR}/wolfded.vcxproj.user)
	configure_file(${PROJECT_SOURCE_DIR}/cmake/vs2013.vcxproj.user.in ${CMAKE_CURRENT_BINARY_DIR}/wolfded.vcxproj.user @ONLY)
endif()

if(WIN32 AND NOT CMAKE_CROSSCOMPILE)
	install(TARGETS wolfded
		BUNDLE  DESTINATION "${INSTALL_DEFAULT_BASEDIR}"
		RUNTIME DESTINATION "${INSTALL_DEFAULT_BASEDIR}"
	)
	install(FILES $<TARGET_PDB_FILE:wolfded> DESTINATION ${INSTALL_DEFAULT_BASEDIR} OPTIONAL)
	install(FILES wolfded DESTINATION ${INSTALL_DEFAULT_BASEDIR} OPTIONAL)
endif()