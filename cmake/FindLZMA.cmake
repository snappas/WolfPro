if(WOLF_64BITS)
	set(DEPS deps64)
endif()

find_path(LZMA_INCLUDE_DIR lzma.h
	${PROJECT_SOURCE_DIR}/${DEPS}/xz/build
	${PROJECT_SOURCE_DIR}/${DEPS}/xz
	DOC "The directory where lzma.h resides"
)

find_library(LZMA_LIBRARY
	NAMES liblzma lzma
	PATHS
	${PROJECT_SOURCE_DIR}/${DEPS}/bin
	${PROJECT_SOURCE_DIR}/${DEPS}/xz/build
	DOC "LZMA library"
)

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(LZMA
	REQUIRED_VARS LZMA_LIBRARY LZMA_INCLUDE_DIR
)

if(LZMA_FOUND)
	set(LZMA_LIBRARIES ${LZMA_LIBRARY})
endif()
