cmake_minimum_required(VERSION 3.3)

find_package(SDL)

set(
    ASS_CUBE_SRCS_PATH "src"
)

set(
    CLIENT_SRCS
        ${ASS_CUBE_SRCS_PATH}/crypto.cpp
        ${ASS_CUBE_SRCS_PATH}/audiomanager.cpp
        ${ASS_CUBE_SRCS_PATH}/client.cpp
        ${ASS_CUBE_SRCS_PATH}/clientgame.cpp
        ${ASS_CUBE_SRCS_PATH}/clients2c.cpp
        ${ASS_CUBE_SRCS_PATH}/command.cpp
        ${ASS_CUBE_SRCS_PATH}/console.cpp
        ${ASS_CUBE_SRCS_PATH}/docs.cpp
        ${ASS_CUBE_SRCS_PATH}/editing.cpp
        ${ASS_CUBE_SRCS_PATH}/entities.cpp
        ${ASS_CUBE_SRCS_PATH}/i18n.cpp
        ${ASS_CUBE_SRCS_PATH}/log.cpp
        ${ASS_CUBE_SRCS_PATH}/main.cpp
        ${ASS_CUBE_SRCS_PATH}/menus.cpp
        ${ASS_CUBE_SRCS_PATH}/physics.cpp
        ${ASS_CUBE_SRCS_PATH}/protocol.cpp
        ${ASS_CUBE_SRCS_PATH}/rendercubes.cpp
        ${ASS_CUBE_SRCS_PATH}/rendergl.cpp
        ${ASS_CUBE_SRCS_PATH}/renderhud.cpp
        ${ASS_CUBE_SRCS_PATH}/rendermodel.cpp
        ${ASS_CUBE_SRCS_PATH}/renderparticles.cpp
        ${ASS_CUBE_SRCS_PATH}/rendertext.cpp
        ${ASS_CUBE_SRCS_PATH}/scoreboard.cpp
        ${ASS_CUBE_SRCS_PATH}/serverms.cpp
        ${ASS_CUBE_SRCS_PATH}/server.cpp
        ${ASS_CUBE_SRCS_PATH}/serverbrowser.cpp
        ${ASS_CUBE_SRCS_PATH}/shadow.cpp
        ${ASS_CUBE_SRCS_PATH}/stream.cpp
        ${ASS_CUBE_SRCS_PATH}/texture.cpp
        ${ASS_CUBE_SRCS_PATH}/tools.cpp
        ${ASS_CUBE_SRCS_PATH}/water.cpp
        ${ASS_CUBE_SRCS_PATH}/weapon.cpp
        ${ASS_CUBE_SRCS_PATH}/wizard.cpp
        ${ASS_CUBE_SRCS_PATH}/world.cpp
        ${ASS_CUBE_SRCS_PATH}/worldio.cpp
        ${ASS_CUBE_SRCS_PATH}/worldlight.cpp
        ${ASS_CUBE_SRCS_PATH}/worldocull.cpp
        ${ASS_CUBE_SRCS_PATH}/worldrender.cpp
        ${ASS_CUBE_SRCS_PATH}/zip.cpp
        ${ASS_CUBE_SRCS_PATH}/bot/bot.cpp
        ${ASS_CUBE_SRCS_PATH}/bot/botmanager.cpp
        ${ASS_CUBE_SRCS_PATH}/bot/bot_ai.cpp
        ${ASS_CUBE_SRCS_PATH}/bot/bot_util.cpp
        ${ASS_CUBE_SRCS_PATH}/bot/bot_waypoint.cpp
        ${ASS_CUBE_SRCS_PATH}/bot/ac_bot.cpp
        ${ASS_CUBE_SRCS_PATH}/bot/ac_bot_ai.cpp
        ${ASS_CUBE_SRCS_PATH}/jpegenc.cpp
        ${ASS_CUBE_SRCS_PATH}/geom.cpp
        
)

set(
    SERVER_SRCS
    ${ASS_CUBE_SRCS_PATH}/crypto.cpp
	${ASS_CUBE_SRCS_PATH}/log.cpp
	${ASS_CUBE_SRCS_PATH}/protocol.cpp
	${ASS_CUBE_SRCS_PATH}/serverms.cpp
	${ASS_CUBE_SRCS_PATH}/server.cpp
	${ASS_CUBE_SRCS_PATH}/stream.cpp
	${ASS_CUBE_SRCS_PATH}/tools.cpp
	${ASS_CUBE_SRCS_PATH}/wizard.cpp
)

add_executable(
    client
    ${CLIENT_SRCS}
)

add_executable(
    server
    ${SERVER_SRCS}
)

target_include_directories(client PUBLIC src src/bot ${SDL_INCLUDE_DIR})
target_compile_options(client PUBLIC -Wall -fomit-frame-pointer -fsigned-char )
target_compile_definitions(client PUBLIC _GNU_SOURCE=1 _REENTRANT)
target_link_libraries(client PUBLIC -lenet -lX11 ${SDL_LIBRARY} -lSDL_image -lz -lGL -lopenal -lvorbisfile -lcurl)


target_include_directories(server PUBLIC src src/bot)
target_compile_definitions(server PUBLIC STANDALONE)
target_compile_options(server PUBLIC -g -O3 -Wall -fomit-frame-pointer -fsigned-char )
target_link_libraries(server PUBLIC -lenet -lz)
