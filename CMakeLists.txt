set (
    ENET_SRC_DIRECTORY "source/enet"
)

execute_process (
    COMMAND shell "uname -s"
    OUTPUT_VARIABLE "PLATFORM"
)

add_custom_target (
    enet
    COMMAND ./configure "--enable-shared=no --enable-static=yes"
    COMMAND make "all"
    WORKING_DIRECTORY ${ENET_SRC_DIRECTORY}
    COMMENT "Building ENET source code..."
)

add_custom_target (
    clean-enet
    COMMAND make "clean"
    WORKING_DIRECTORY ${ENET_SRC_DIRECTORY}
    COMMENT "Building ENET source code..."
)

set(
    ASS_CUBE_SRCS_PATH "source/src"
)

set(
    MASTER_SRCS
    [=[
	${ASS_CUBE_SRCS_PATH}/crypto-standalone.o
	${ASS_CUBE_SRCS_PATH}/stream-standalone.o
	${ASS_CUBE_SRCS_PATH}/command-standalone.o
	${ASS_CUBE_SRCS_PATH}/master-standalone.o
    ]=]
)

set(
    CLIENT_SRCS
    [=[
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
	${ASS_CUBE_SRCS_PATH}/oggstream.cpp
	${ASS_CUBE_SRCS_PATH}/openal.cpp
	${ASS_CUBE_SRCS_PATH}/packetqueue.cpp
	${ASS_CUBE_SRCS_PATH}/physics.cpp
	${ASS_CUBE_SRCS_PATH}/protocol.cpp
	${ASS_CUBE_SRCS_PATH}/rendercubes.cpp
	${ASS_CUBE_SRCS_PATH}/rendergl.cpp
	${ASS_CUBE_SRCS_PATH}/renderhud.cpp
	${ASS_CUBE_SRCS_PATH}/rendermodel.cpp
	${ASS_CUBE_SRCS_PATH}/renderparticles.cpp
	${ASS_CUBE_SRCS_PATH}/rendertext.cpp
	${ASS_CUBE_SRCS_PATH}/rndmap.cpp
	${ASS_CUBE_SRCS_PATH}/scoreboard.cpp
	${ASS_CUBE_SRCS_PATH}/serverms.cpp
	${ASS_CUBE_SRCS_PATH}/server.cpp
	${ASS_CUBE_SRCS_PATH}/serverbrowser.cpp
	${ASS_CUBE_SRCS_PATH}/shadow.cpp
	${ASS_CUBE_SRCS_PATH}/sound.cpp
	${ASS_CUBE_SRCS_PATH}/soundlocation.cpp
	${ASS_CUBE_SRCS_PATH}/soundscheduler.cpp
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
    ]=]
)

set(
    SERVER_SRCS
    [=[
	${ASS_CUBE_SRCS_PATH}/crypto-standalone.o
	${ASS_CUBE_SRCS_PATH}/log-standalone.o
	${ASS_CUBE_SRCS_PATH}/protocol-standalone.o
	${ASS_CUBE_SRCS_PATH}/serverms-standalone.o
	${ASS_CUBE_SRCS_PATH}/server-standalone.o
	${ASS_CUBE_SRCS_PATH}/stream-standalone.o
	${ASS_CUBE_SRCS_PATH}/tools-standalone.o
	${ASS_CUBE_SRCS_PATH}/wizard-standalone.o
    ]=]
)

add_executable(
    client
    ${MASTER_SRCS} ${CLIENT_SRCS}
)
add_dependencies(client  enet)

add_executable(
    server
    ${MASTER_SRCS} ${SERVER_SRCS}
)
add_dependencies(server  enet)
