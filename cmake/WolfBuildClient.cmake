#-----------------------------------------------------------------
# Build Client
#-----------------------------------------------------------------

set(WOLF_OUTPUT_DIR "")


function(setup_client wolfmp_target IS_VULKAN)

if(IS_VULKAN)
	set(CLIENT_SRC ${CLIENT_SRC_VK})
	if(WIN32)
	LIST(APPEND WOLF_COMPILE_DEF "RTCW_VULKAN" "VK_USE_PLATFORM_WIN32_KHR")
	else()
	LIST(APPEND WOLF_COMPILE_DEF "RTCW_VULKAN" "VK_USE_PLATFORM_XLIB_KHR")
	endif()
else()
	set(CLIENT_SRC ${CLIENT_SRC_GL})
endif()

#todo add debug macro in debug build
if(WIN32)
	set(CMAKE_C_FLAGS_DEBUG "/DEBUG /Zi")
	set(CMAKE_C_FLAGS_RELEASE "/Zi /DEBUG")
	set(CLANGRT "C:/Program Files/LLVM/lib/clang/19/lib/windows")
	if(ENABLE_ASAN)
	set(CMAKE_EXE_LINKER_FLAGS_RELEASE "${CMAKE_SHARED_LINKER_FLAGS_RELEASE} /DEBUG /OPT:REF /OPT:ICF /O0 -g /LIBPATH:${CLANGRT} /EHsc")
	else()
	set(CMAKE_EXE_LINKER_FLAGS_RELEASE "${CMAKE_SHARED_LINKER_FLAGS_RELEASE} /DEBUG /OPT:REF /OPT:ICF")
	endif()
	set(CMAKE_EXE_LINKER_FLAGS_DEBUG "${CMAKE_SHARED_LINKER_FLAGS_DEBUG} /DEBUG /OPT:REF /OPT:ICF ")
	add_executable(${wolfmp_target} WIN32 ${COMMON_SRC} ${CLIENT_SRC})
	if(CMAKE_BUILD_TYPE MATCHES "Debug")
			set_property(TARGET ${wolfmp_target} PROPERTY
             MSVC_RUNTIME_LIBRARY "MultiThreadedDebug")
	else()
			 set_property(TARGET ${wolfmp_target} PROPERTY
             MSVC_RUNTIME_LIBRARY "MultiThreaded")
	endif()
	set_property(TARGET ${wolfmp_target} PROPERTY VS_STARTUP_PROJECT INSTALL)
	
else()
	add_executable(${wolfmp_target} ${COMMON_SRC} ${CLIENT_SRC})
endif()




if(IS_VULKAN)
	target_link_libraries(${wolfmp_target}
		client_libraries_vk
		engine_libraries
		os_libraries
		${CURL_LIBRARIES}
	)
	target_link_libraries(${wolfmp_target} cimgui)
else()
	target_link_libraries(${wolfmp_target}
	client_libraries_gl
	engine_libraries
	os_libraries
	${CURL_LIBRARIES}
	)
endif()
if(ENABLE_ASAN)
target_link_options(${wolfmp_target} PRIVATE /wholearchive:clang_rt.asan-x86_64.lib)
endif()

target_include_directories(${wolfmp_target} PRIVATE ${CURL_INCLUDE_DIR})

message(STATUS CMAKE_BUILD_TYPE)


message(STATUS "Wolfmp Compile defs: " ${WOLF_COMPILE_DEF})
set_target_properties(${wolfmp_target} PROPERTIES
	COMPILE_DEFINITIONS "${WOLF_COMPILE_DEF}"
	RUNTIME_OUTPUT_DIRECTORY "${WOLF_OUTPUT_DIR}"
	RUNTIME_OUTPUT_DIRECTORY_DEBUG "${WOLF_OUTPUT_DIR}"
	RUNTIME_OUTPUT_DIRECTORY_RELEASE "${WOLF_OUTPUT_DIR}"
)

if((UNIX) AND NOT APPLE AND NOT ANDROID)
	set_target_properties(${wolfmp_target} PROPERTIES SUFFIX "${BIN_SUFFIX}")
endif()

target_compile_definitions(${wolfmp_target} PRIVATE WOLF_CLIENT=1)
if(MSVC)
	target_link_options(${wolfmp_target} PRIVATE /DEBUG /STACK:8388608 )
endif()

if(MSVC AND NOT EXISTS ${CMAKE_CURRENT_BINARY_DIR}/wolfmp.vcxproj.user)
	configure_file(${PROJECT_SOURCE_DIR}/cmake/vs2013.vcxproj.user.in ${CMAKE_CURRENT_BINARY_DIR}/wolfmp.vcxproj.user @ONLY)
endif()

install(TARGETS ${wolfmp_target}
	BUNDLE  DESTINATION "${INSTALL_DEFAULT_BASEDIR}"
	RUNTIME DESTINATION "${INSTALL_DEFAULT_BASEDIR}"
)
if(WIN32)
install(FILES $<TARGET_PDB_FILE:${wolfmp_target}> DESTINATION ${INSTALL_DEFAULT_BASEDIR} OPTIONAL)
install(FILES $<TARGET_PDB_FILE:wolfded> DESTINATION ${INSTALL_DEFAULT_BASEDIR} OPTIONAL)
install(FILES $<TARGET_PDB_FILE:qagame> DESTINATION ${INSTALL_DEFAULT_BASEDIR}/${MODNAME} OPTIONAL)
install(FILES $<TARGET_PDB_FILE:ui> DESTINATION ${INSTALL_DEFAULT_BASEDIR}/${MODNAME} OPTIONAL)
install(FILES $<TARGET_PDB_FILE:cgame> DESTINATION ${INSTALL_DEFAULT_BASEDIR}/${MODNAME} OPTIONAL)
endif()
endfunction()

if(WOLF_64BITS)
	setup_client(wolfmp_gl_x64 FALSE)
	setup_client(wolfmp_vk_x64 TRUE)
else()
	setup_client(wolfmp_gl_x86 FALSE)
	setup_client(wolfmp_vk_x86 TRUE)
endif()
