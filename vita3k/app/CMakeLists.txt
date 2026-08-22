add_library(
	app
	STATIC
	include/app/functions.h
	include/app/sdl_frame_host.h
	include/app/virtual_cartridge.h
	include/app/session_controller.h
	include/app/discord.h
	include/app/memory_search.h
	include/app/state.h
	src/app_init.cpp
	src/sdl_frame_host.cpp
	src/virtual_cartridge.cpp
	src/app.cpp
	src/apps_list.cpp
	src/memory_search.cpp
	src/discord.cpp
	src/session_controller.cpp
	src/user.cpp
)

target_include_directories(app PUBLIC include)
target_include_directories(app PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_link_libraries(app PUBLIC emuenv mem)
if(USE_DISCORD_RICH_PRESENCE)
  target_link_libraries(app PUBLIC discord-rpc)
endif()
target_link_libraries(app PRIVATE audio camera config ctrl dialog display gdbstub gxm ime io mem modules motion net ngs np patch regmgr renderer touch SDL3::SDL3 pugixml::pugixml compat packages http)
if(WIN32)
	target_link_libraries(app PRIVATE dwmapi)
endif()

if (LINUX)
    find_package(X11 REQUIRED)
    target_link_libraries(app PRIVATE ${X11_LIBRARIES})
endif()

if(ANDROID)
	target_link_libraries(app PRIVATE host::dialog miniz)
endif()
