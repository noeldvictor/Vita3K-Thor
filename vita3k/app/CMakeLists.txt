add_library(
	app
	STATIC
	include/app/functions.h
	include/app/discord.h
	src/app_init.cpp
	src/app.cpp
	src/discord.cpp
)

target_include_directories(app PUBLIC include)
target_link_libraries(app PUBLIC emuenv mem)
if(USE_DISCORD_RICH_PRESENCE)
  target_link_libraries(app PUBLIC discord-rpc)
endif()
target_link_libraries(app PRIVATE audio camera config ctrl dialog display gdbstub gui gxm ime io mem modules motion net ngs np patch regmgr renderer touch SDL3::SDL3 pugixml::pugixml compat packages http)
if(WIN32)
	target_link_libraries(app PRIVATE dwmapi)
elseif(ANDROID)
	target_link_libraries(app PRIVATE android adrenotools host::dialog miniz)
endif()

if (LINUX)
    find_package(X11 REQUIRED)
    target_link_libraries(app PRIVATE ${X11_LIBRARIES})
endif()
