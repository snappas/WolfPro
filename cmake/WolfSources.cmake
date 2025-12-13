#-----------------------------------------------------------------
# Sources
#-----------------------------------------------------------------

FILE(GLOB COMMON_SRC
	"src/qcommon/*.c"
	"src/qcommon/*.h"
)

FILE(GLOB QCOMMON
	"src/game/q_shared.c"
	"src/game/q_shared.h"
	"src/game/q_math.c"
)
FILE(GLOB COMMON_SRC_REMOVE
		"src/qcommon/vm_x86.c"
	)
if(UNIX)
	FILE(GLOB SDL_SRC
		"src/unix/sdl_*.c"
	)
	LIST(REMOVE_ITEM SDL_SRC
		"${CMAKE_CURRENT_SOURCE_DIR}/src/unix/sdl_glimp.c"
		"${CMAKE_CURRENT_SOURCE_DIR}/src/unix/sdl_vkimp.c"
		)
endif()



LIST(REMOVE_ITEM COMMON_SRC ${COMMON_SRC_REMOVE})

# Platform specific code for server and client
if(UNIX)
	LIST(APPEND COMMON_SRC "src/unix/unix_main.c")
	LIST(APPEND COMMON_SRC "src/unix/unix_net.c")
	LIST(APPEND COMMON_SRC "src/unix/unix_shared.c")
	LIST(APPEND COMMON_SRC "src/unix/linux_common.c")
	LIST(APPEND COMMON_SRC "src/unix/linux_signals.c")
	LIST(APPEND COMMON_SRC "src/unix/linux_threads.c")
elseif(WIN32)
	LIST(APPEND COMMON_SRC "src/win32/win_syscon.c")
	LIST(APPEND COMMON_SRC "src/win32/win_shared.c")
	LIST(APPEND COMMON_SRC "src/win32/win_main.c")
	LIST(APPEND COMMON_SRC "src/win32/win_net.c")
	LIST(APPEND COMMON_SRC "src/win32/win_wndproc.c")
	LIST(APPEND CLIENT_SRC "src/win32/win_snd.c")
	LIST(APPEND CLIENT_SRC "src/win32/win_input.c")
	LIST(APPEND COMMON_SRC "src/win32/win_exception.c")
	LIST(APPEND COMMON_SRC "src/win32/winquake.rc")
	LIST(APPEND COMMON_SRC "src/win32/win_threads.c")
	#LIST(APPEND COMMON_SRC "src/win32/client.manifest")
endif()



FILE(GLOB SERVER_SRC
	"src/server/*.c"
	"src/server/*.h"
	"src/client/cl_cvarrestrict.c"
	"src/null/*.c"
	"src/null/*.h"
	"src/botlib/be*.c"
	"src/botlib/be*.h"
	"src/botlib/l_*.c"
	"src/botlib/l_*.h"
)

LIST(APPEND SERVER_SRC ${QCOMMON})

FILE(GLOB CLIENT_COMMON_SRC
	"src/server/*.c"
	"src/server/*.h"
	"src/client/*.c"
	"src/client/*.h"
	"src/botlib/be*.c"
	"src/botlib/be*.h"
	"src/botlib/l_*.c"
	"src/botlib/l_*.h"
)

LIST(APPEND CLIENT_SRC ${CLIENT_COMMON_SRC})




LIST(APPEND CLIENT_SRC ${QCOMMON})

set(CLIENT_SRC_VK ${CLIENT_SRC})
set(CLIENT_SRC_GL ${CLIENT_SRC})
if(WIN32)
	LIST(APPEND CLIENT_SRC_GL
		"src/win32/win_glimp.c"
		"src/win32/win_qgl.c"
		"src/win32/win_gamma.c"
		
	)
	
	LIST(APPEND CLIENT_SRC_VK
		"src/win32/win_vkimp.c"
	)
else()
	LIST(APPEND CLIENT_SRC_GL
		"src/unix/sdl_glimp.c"
		"src/unix/linux_qgl.c"
		 ${SDL_SRC}
	)
	LIST(APPEND CLIENT_SRC_VK
		"src/unix/sdl_vkimp.c"
		 ${SDL_SRC}
	)
endif()



message(STATUS ${CLIENT_SRC_GL})
LIST(REMOVE_ITEM CLIENT_SRC_GL
	"${CMAKE_CURRENT_SOURCE_DIR}/src/client/cl_imgui.c"
	"${CMAKE_CURRENT_SOURCE_DIR}/src/client/cl_imgui_helpers.c"
)

message(STATUS ${CLIENT_SRC_GL})

# These files are shared with the CGAME from the UI library
FILE(GLOB UI_SHARED
	"src/ui/ui_shared.c"
	"src/ui/ui_parse.c"
	"src/ui/ui_script.c"
	"src/ui/ui_menu.c"
	"src/ui/ui_menuitem.c"
)

FILE(GLOB CGAME_SRC
	"src/cgame/*.c"
	"src/cgame/*.h"
	"src/game/q_math.c"
	"src/game/q_shared.c"
	"src/game/bg_*.c"
	"src/client/cl_imgui_helpers.c"
	"src/cgame/cgame.def"
)

LIST(APPEND CGAME_SRC ${UI_SHARED})

FILE(GLOB QAGAME_SRC
	"src/game/*.c"
	"src/game/*.h"
	"src/game/q_math.c"
	"src/game/q_shared.c"
	"src/botai/*.c"
	"src/botai/*.h"
	"src/game/game.def"
)


FILE(GLOB UI_SRC
	"src/ui/*.c"
	"src/ui/*.h"
    "src/ui/lib/*.c"
    "src/ui/lib/*.h"
	"src/game/q_math.c"
	"src/game/q_shared.c"
	"src/game/bg_classes.c"
	"src/game/bg_misc.c"
	"src/ui/ui.def"
)

FILE(GLOB CLIENT_FILES
	"src/client/*.c"
)

FILE(GLOB SERVER_FILES
	"src/server/*.c"
)

FILE(GLOB SYSTEM_FILES
	"src/sys/sys_main.c"
	"src/sys/con_log.c"
)

FILE(GLOB BOTLIB_FILES
	"src/botlib/be*.c"
	"src/botlib/l_*.c"
)

FILE(GLOB SPLINES_FILES
	"src/splines/*.cpp"
	"src/splines/*.h"
)

FILE(GLOB JPEG_FILES
	"src/jpeg-6/*.c"
	"src/jpeg-6/*.h"
)

FILE(GLOB RENDERER_COMMON
	"src/game/q_shared.h"
	"src/renderer_common/*.h"
	"src/qcommon/model.c"
	"src/qcommon/qcommon.h"
)

FILE(GLOB RENDERER_FILES
	"src/renderer_gl/*.c"
	"src/renderer_gl/*.h"
)

FILE(GLOB RENDERER_VK_FILES
	"src/renderer_vk/*.c"
	"src/renderer_vk/*.h"
)

FILE(GLOB RENDERER_VK_VMA_FILES
	"src/renderer_vk/vk_vma_alloc.cpp"
)

FILE(GLOB RENDERER_CIMGUI_FILES
	"src/cimgui/*.cpp"
	"src/cimgui/*.h"
	"src/cimgui/imgui/*.cpp"
	"src/cimgui/imgui/*.h"

)

