#-----------------------------------------------------------------
# Embed hudchars.ttf so the vector font system works even when
# MAIN/fonts/hudchars.ttf / wolfpro_assets.pk3 isn't mounted at runtime.
# HUDCHARS_TTF_SRC/HUDCHARS_TTF_HEADER are set in WolfSources.cmake, which
# also lists the header in RENDERER_COMMON -- that's what makes it a real
# source of both targets below, and gives it the build-order dependency on
# this custom command automatically (matching OUTPUT), no add_dependencies needed.
#-----------------------------------------------------------------

add_custom_command(
	OUTPUT ${HUDCHARS_TTF_HEADER}
	COMMAND ${CMAKE_COMMAND} -DINPUT=${HUDCHARS_TTF_SRC} -DOUTPUT=${HUDCHARS_TTF_HEADER}
	        -DVARNAME=font_hudchars_embedded_ttf -P ${CMAKE_SOURCE_DIR}/cmake/EmbedFont.cmake
	DEPENDS ${HUDCHARS_TTF_SRC} ${CMAKE_SOURCE_DIR}/cmake/EmbedFont.cmake
	COMMENT "Embedding hudchars.ttf into generated header"
	VERBATIM
)

#-----------------------------------------------------------------
# Build Renderer
#-----------------------------------------------------------------

add_library(renderer STATIC ${RENDERER_FILES} ${RENDERER_COMMON})

target_link_libraries(renderer renderer_gl1_libraries renderer_libraries)
target_include_directories(renderer PRIVATE src/renderer "${CMAKE_BINARY_DIR}/generated")

if(NOT MSVC)
	target_link_libraries(renderer m)
endif()

target_link_libraries(client_libraries_gl INTERFACE renderer)


#-----------------------------------------------------------------
# Build Vulkan Renderer
#-----------------------------------------------------------------

add_library(renderer_vk STATIC ${RENDERER_VK_FILES} ${RENDERER_COMMON})
add_library(vk_vma_alloc STATIC ${RENDERER_VK_VMA_FILES})
target_include_directories(vk_vma_alloc PRIVATE ${Vulkan_INCLUDE_DIR})


target_link_libraries(renderer_vk renderer_vk_libraries renderer_libraries vk_vma_alloc)
target_include_directories(renderer_vk PRIVATE src/renderer_vk "${CMAKE_BINARY_DIR}/generated")
if(WIN32)
LIST(APPEND WOLF_COMPILE_DEF "VK_USE_PLATFORM_WIN32_KHR")
else()
LIST(APPEND WOLF_COMPILE_DEF "VK_USE_PLATFORM_XLIB_KHR")
endif()
message(STATUS "Renderer Compile defs: " ${WOLF_COMPILE_DEF})
set_target_properties(renderer_vk PROPERTIES
	COMPILE_DEFINITIONS "${WOLF_COMPILE_DEF}"
	RUNTIME_OUTPUT_DIRECTORY "${WOLF_OUTPUT_DIR}"
	RUNTIME_OUTPUT_DIRECTORY_DEBUG "${WOLF_OUTPUT_DIR}"
	RUNTIME_OUTPUT_DIRECTORY_RELEASE "${WOLF_OUTPUT_DIR}"
)
set_target_properties(vk_vma_alloc PROPERTIES
	CXX_STANDARD 17
	COMPILE_DEFINITIONS "${WOLF_COMPILE_DEF}"
	RUNTIME_OUTPUT_DIRECTORY "${WOLF_OUTPUT_DIR}"
	RUNTIME_OUTPUT_DIRECTORY_DEBUG "${WOLF_OUTPUT_DIR}"
	RUNTIME_OUTPUT_DIRECTORY_RELEASE "${WOLF_OUTPUT_DIR}"
)
message(STATUS "Compile defs: " ${WOLF_COMPILE_DEF})



if(NOT MSVC)
	target_link_libraries(renderer_vk m)
endif()

target_link_libraries(client_libraries_vk INTERFACE renderer_vk)


#-----------------------------------------------------------------
# Build JPEG
#-----------------------------------------------------------------

find_package(JPEGTURBO)
if(JPEGTURBO_FOUND)
	target_link_libraries(renderer_libraries INTERFACE ${JPEG_LIBRARIES})
	target_include_directories(renderer_libraries INTERFACE ${JPEG_INCLUDE_DIR})
	# Check for libjpeg-turbo v1.3
	include(CheckFunctionExists)
	set(CMAKE_REQUIRED_INCLUDES ${JPEG_INCLUDE_DIR})
	set(CMAKE_REQUIRED_LIBRARIES ${JPEG_LIBRARY})
	# FIXME: function is checked, but HAVE_JPEG_MEM_SRC is empty. Why?
	check_function_exists("jpeg_mem_src" HAVE_JPEG_MEM_SRC)
	message(STATUS "JPEG Include dir: " ${JPEG_INCLUDE_DIR})
	message(STATUS "JPEG library: " ${JPEG_LIBRARY})
endif()

