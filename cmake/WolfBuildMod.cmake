#-----------------------------------------------------------------
# Build mod pack
#-----------------------------------------------------------------

# find libm where it exists and link game modules against it
include(CheckLibraryExists)
check_library_exists(m pow "" LIBM)
if(UNIX)
	if(LIBM)
		target_link_libraries(cgame_libraries INTERFACE m)
		target_link_libraries(ui_libraries INTERFACE m)
	endif()
endif(UNIX)

#
# cgame
#
if(BUILD_CLIENT_MOD)
	add_library(cgame MODULE ${CGAME_SRC})
	if(ENABLE_ASAN)
	target_link_libraries(cgame cgame_libraries mod_libraries cimgui clang_rt.asan_dynamic-x86_64 clang_rt.asan_dynamic_runtime_thunk-x86_64)
	else()
	target_link_libraries(cgame cgame_libraries mod_libraries cimgui)
	endif()

	set_target_properties(cgame
		PROPERTIES
		PREFIX ""
		C_STANDARD 11
		OUTPUT_NAME "cgame${LIB_SUFFIX}${ARCH}"
		LIBRARY_OUTPUT_DIRECTORY "${MODNAME}"
		LIBRARY_OUTPUT_DIRECTORY_DEBUG "${MODNAME}"
		LIBRARY_OUTPUT_DIRECTORY_RELEASE "${MODNAME}"
		RUNTIME_OUTPUT_DIRECTORY "${MODNAME}"
		RUNTIME_OUTPUT_DIRECTORY_DEBUG "${MODNAME}"
		RUNTIME_OUTPUT_DIRECTORY_RELEASE "${MODNAME}"
	)
	target_compile_definitions(cgame PRIVATE CGAMEDLL=1 MODLIB=1)
	if(MSVC)
	if(ENABLE_ASAN)
		target_link_options(cgame PRIVATE /DEF:cgame.def /DEBUG /wholearchive:clang_rt.asan_dynamic_runtime_thunk-x86_64.lib)
	else()
		target_link_options(cgame PRIVATE /DEF:cgame.def /DEBUG)
	endif()
	elseif(MINGW)
		target_sources(cgame PRIVATE src/cgame/cgame.def)
	endif()
endif()

#
# ui
#
if(BUILD_CLIENT_MOD)
	add_library(ui MODULE ${UI_SRC})
	if(ENABLE_ASAN)
	target_link_libraries(ui ui_libraries mod_libraries clang_rt.asan_dynamic-x86_64 clang_rt.asan_dynamic_runtime_thunk-x86_64)
	else()
	target_link_libraries(ui ui_libraries mod_libraries)
	endif()
	set_target_properties(ui
		PROPERTIES
		PREFIX ""
		C_STANDARD 11
		OUTPUT_NAME "ui${LIB_SUFFIX}${ARCH}"
		LIBRARY_OUTPUT_DIRECTORY "${MODNAME}"
		LIBRARY_OUTPUT_DIRECTORY_DEBUG "${MODNAME}"
		LIBRARY_OUTPUT_DIRECTORY_RELEASE "${MODNAME}"
		RUNTIME_OUTPUT_DIRECTORY "${MODNAME}"
		RUNTIME_OUTPUT_DIRECTORY_DEBUG "${MODNAME}"
		RUNTIME_OUTPUT_DIRECTORY_RELEASE "${MODNAME}"
	)
	target_compile_definitions(ui PRIVATE UIDLL=1 MODLIB=1)
	if(MSVC)
	if(ENABLE_ASAN)
		target_link_options(ui PRIVATE /DEF:ui.def /DEBUG /wholearchive:clang_rt.asan_dynamic_runtime_thunk-x86_64.lib)
	else()
		target_link_options(ui PRIVATE /DEF:ui.def /DEBUG)
	endif()
	elseif(MINGW)
		target_sources(ui PRIVATE src/ui/ui.def)
	endif()
endif()

#
# qagame
#
if(BUILD_SERVER_MOD)
	add_library(qagame MODULE ${QAGAME_SRC})
	find_package(JANSSON)
	target_include_directories(qagame_libraries INTERFACE ${JANSSON_INCLUDE_DIR})
	message("Jansson include: ${JANSSON_INCLUDE_DIR}")
	target_link_libraries(qagame_libraries INTERFACE ${JANSSON_LIBRARY})
	if(ENABLE_OMNIBOT)
		find_package(OMNIBOT)
		target_include_directories(qagame_libraries INTERFACE ${OMNIBOT_INCLUDE_DIR})
	endif()
	if(ENABLE_ASAN)
	target_link_libraries(qagame qagame_libraries mod_libraries clang_rt.asan_dynamic-x86_64 clang_rt.asan_dynamic_runtime_thunk-x86_64)
	else()
	target_link_libraries(qagame qagame_libraries mod_libraries)
	endif()

	set_target_properties(qagame
		PROPERTIES
		PREFIX ""
		C_STANDARD 11
		OUTPUT_NAME "qagame${LIB_SUFFIX}${ARCH}"
		LIBRARY_OUTPUT_DIRECTORY "${MODNAME}"
		LIBRARY_OUTPUT_DIRECTORY_DEBUG "${MODNAME}"
		LIBRARY_OUTPUT_DIRECTORY_RELEASE "${MODNAME}"
		RUNTIME_OUTPUT_DIRECTORY "${MODNAME}"
		RUNTIME_OUTPUT_DIRECTORY_DEBUG "${MODNAME}"
		RUNTIME_OUTPUT_DIRECTORY_RELEASE "${MODNAME}"
	)
	target_compile_definitions(qagame PRIVATE GAMEDLL=1 MODLIB=1)
	if(MSVC)
	if(ENABLE_ASAN)
		target_link_options(qagame PRIVATE /DEF:qagame.def /DEBUG /wholearchive:clang_rt.asan_dynamic_runtime_thunk-x86_64.lib)
	else()
		target_link_options(qagame PRIVATE /DEF:qagame.def /DEBUG)
	endif()
	elseif(MINGW)
		target_sources(qagame PRIVATE src/qagame/qagame.def)
	endif()
	
endif()

# install bins of cgame, ui and qgame
if(BUILD_SERVER_MOD)
	install(TARGETS qagame
		RUNTIME DESTINATION "${INSTALL_DEFAULT_BASEDIR}/${MODNAME}"
		LIBRARY DESTINATION "${INSTALL_DEFAULT_BASEDIR}/${MODNAME}"
		ARCHIVE DESTINATION "${INSTALL_DEFAULT_BASEDIR}/${MODNAME}"
	)
endif()

if(NOT BUILD_MOD_PK3 AND BUILD_CLIENT_MOD)
	install(TARGETS cgame ui
		RUNTIME DESTINATION "${INSTALL_DEFAULT_BASEDIR}/${MODNAME}"
		LIBRARY DESTINATION "${INSTALL_DEFAULT_BASEDIR}/${MODNAME}"
		ARCHIVE DESTINATION "${INSTALL_DEFAULT_BASEDIR}/${MODNAME}"
	)
endif()

#
# mod pk3
# Full cross-compile build needs this OFF to update the pk3 with the 2nd build's files
# 
if(BUILD_MOD_PK3)
	# main
	file(GLOB MAIN_FILES "${CMAKE_CURRENT_SOURCE_DIR}/MAIN/*")
	foreach(FILE ${MAIN_FILES})
		file(RELATIVE_PATH REL "${CMAKE_CURRENT_SOURCE_DIR}/MAIN" ${FILE})
		list(APPEND MAIN_FILES_LIST ${REL})
	endforeach()

	# Remove old mod pk3 files from the build directory (useful for the development)
	file(GLOB OLD_PK3_FILES "${CMAKE_CURRENT_BINARY_DIR}/${MODNAME}/${MODNAME}_*.pk3")
	list(REMOVE_ITEM OLD_PK3_FILES)
	add_custom_target(remove_old_pk3_files
		COMMAND ${CMAKE_COMMAND} -E remove -f "${OLD_PK3_FILES}"
		COMMAND_EXPAND_LISTS
	)

	add_custom_command(
		OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/${MODNAME}/${MODNAME}_assets.pk3
		COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_CURRENT_SOURCE_DIR}/MAIN ${CMAKE_CURRENT_BINARY_DIR}/${MODNAME}
		COMMAND ${CMAKE_COMMAND} -E tar c ${CMAKE_CURRENT_BINARY_DIR}/${MODNAME}/${MODNAME}_assets.pk3 --format=zip ${MAIN_FILES_LIST}
		WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/${MODNAME}/
		VERBATIM
	)

	add_custom_command(
		OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/${MODNAME}/${MODNAME}_bin.pk3
		COMMAND ${CMAKE_COMMAND} -E tar c ${CMAKE_CURRENT_BINARY_DIR}/${MODNAME}/${MODNAME}_bin.pk3 --format=zip $<TARGET_FILE_NAME:ui> $<TARGET_FILE_NAME:cgame>
		DEPENDS cgame ui
		WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/${MODNAME}/
		VERBATIM
	)

	add_custom_command(
		OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/${MODNAME}/${MODNAME}_server.pk3
		COMMAND ${CMAKE_COMMAND} -E tar c ${CMAKE_CURRENT_BINARY_DIR}/${MODNAME}/${MODNAME}_server.pk3 --format=zip $<TARGET_FILE_NAME:qagame>
		DEPENDS qagame
		WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/${MODNAME}/
		VERBATIM
	)

    add_custom_target(mod_pk3 ALL DEPENDS ${CMAKE_CURRENT_BINARY_DIR}/${MODNAME}/${MODNAME}_bin.pk3 ${CMAKE_CURRENT_BINARY_DIR}/${MODNAME}/${MODNAME}_server.pk3 ${CMAKE_CURRENT_BINARY_DIR}/${MODNAME}/${MODNAME}_assets.pk3)

	install(FILES 
		${CMAKE_CURRENT_BINARY_DIR}/${MODNAME}/${MODNAME}_bin.pk3
		${CMAKE_CURRENT_BINARY_DIR}/${MODNAME}/${MODNAME}_server.pk3
		${CMAKE_CURRENT_BINARY_DIR}/${MODNAME}/${MODNAME}_assets.pk3
		DESTINATION "${INSTALL_DEFAULT_BASEDIR}/${MODNAME}"
	)
endif()
