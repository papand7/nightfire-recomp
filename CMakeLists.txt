# 007: Agent Under Fire (original Xbox), statically recompiled

cmake_minimum_required(VERSION 3.25)
project(game LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
# x86 builds assume SSE4.2 (NightFire's way: no flag for Apple Silicon or other ARM); a build folder configured when the
# presets passed it in CMAKE_CXX_FLAGS keeps that
if(CMAKE_SYSTEM_PROCESSOR MATCHES "^(x86_64|AMD64|amd64)$" AND NOT CMAKE_CXX_FLAGS MATCHES "-march=")
    add_compile_options(-march=x86-64-v2)
endif()

# the disc in assets chooses the programs: the USA disc is default.xbe, bond.xbe and Driving.xbe, each the same game at its own
# addresses (src/*_usa and config/*_usa*.toml, written by tools/map_bond.py)
file(SHA1 ${CMAKE_CURRENT_SOURCE_DIR}/assets/default.xbe AUF_DISC)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/assets/default.xbe)
if(AUF_DISC STREQUAL "dff80de4f889ef5ee70ac748a0391f2ed220d0e9")
    set(AUF_USA ON)
    set(AUF_CONFIGS auf_usa game_usa_game bond_usa bond_usa_game driving_usa driving_usa_game)
else()
    set(AUF_CONFIGS auf game bond bond_game bond_fre bond_fre_game bond_ger bond_ger_game driving driving_game)
endif()
# on Windows the game's code is a DLL per program, built on the player's PC (sdk's cmake/XbeRecomp.cmake); the harness links it in.
# The package carries this disc's configs
if(WIN32)
    set(XBE_GAME_DLL ON)
    list(TRANSFORM AUF_CONFIGS PREPEND ${CMAKE_CURRENT_SOURCE_DIR}/config/ OUTPUT_VARIABLE XBE_GAME_CONFIGS)
    list(TRANSFORM XBE_GAME_CONFIGS APPEND .toml)
endif()

# the SDK: sdk/ here, two folders up inside the SDK's templates, a checkout beside this repository, else downloaded
if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/sdk/cmake/XbeRecomp.cmake)
    set(SDK ${CMAKE_CURRENT_SOURCE_DIR}/sdk)
elseif(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/../../cmake/XbeRecomp.cmake)
    set(SDK ${CMAKE_CURRENT_SOURCE_DIR}/../..)
elseif(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/../XboxXBERecomp/cmake/XbeRecomp.cmake)
    set(SDK ${CMAKE_CURRENT_SOURCE_DIR}/../XboxXBERecomp)
elseif(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/../XboxXBE-Recomp/cmake/XbeRecomp.cmake)
    set(SDK ${CMAKE_CURRENT_SOURCE_DIR}/../XboxXBE-Recomp)
else()
    include(FetchContent)
    FetchContent_Declare(xbe_sdk GIT_REPOSITORY https://github.com/hk21x/XboxXBERecomp.git GIT_TAG main)
    FetchContent_MakeAvailable(xbe_sdk)
    set(SDK ${xbe_sdk_SOURCE_DIR})
endif()
include(${SDK}/cmake/XbeRecomp.cmake)

file(GLOB XBE_HOST_SOURCES CONFIGURE_DEPENDS src/*/*.cpp)
list(FILTER XBE_HOST_SOURCES EXCLUDE REGEX "/src/(game|game_usa|bond|bond_fre|bond_ger|bond_usa|driving|driving_usa)/")

# partitions as xbe_symbols wrote them into each scan file (none yet: build.bat writes them, the change reconfigures)
function(xbe_partitions scan out)
    set(n 8)
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS config/${scan})
    if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/config/${scan})
        file(STRINGS config/${scan} line REGEX "^partitions *=")
        string(REGEX MATCH "[0-9]+" found "${line}")
        if(found)
            set(n ${found})
        endif()
    endif()
    set(${out} ${n} PARENT_SCOPE)
endfunction()
if(AUF_USA)
    xbe_partitions(game_usa_game.toml GAME_PARTITIONS)
    xbe_partitions(bond_usa_game.toml BOND_PARTITIONS)
    xbe_partitions(driving_usa_game.toml DRIVING_PARTITIONS)
    add_xbe_image(AgentUnderFire default.xbe auf_usa.toml ${GAME_PARTITIONS} game_usa)
    add_xbe_image(AgentUnderFireBond bond.xbe bond_usa.toml ${BOND_PARTITIONS} bond_usa)
    add_xbe_image(AgentUnderFireDriving Driving.xbe driving_usa.toml ${DRIVING_PARTITIONS} driving_usa)
else()
    xbe_partitions(game.toml GAME_PARTITIONS)
    xbe_partitions(bond_game.toml BOND_PARTITIONS)
    xbe_partitions(driving_game.toml DRIVING_PARTITIONS)
    xbe_partitions(bond_fre_game.toml BOND_FRE_PARTITIONS)
    xbe_partitions(bond_ger_game.toml BOND_GER_PARTITIONS)

    # target name = GAME_EXE in build.bat, xbe = GAME_XBE: default.xbe is the disc's launcher and movie player
    add_xbe_image(AgentUnderFire default.xbe auf.toml ${GAME_PARTITIONS} game)
    # each language's game is an xbe default.xbe launches (XLaunchNewImage): an exe of its own. src/bond_fre and src/bond_ger are
    # src/bond again, at those builds' addresses (tools/map_bond.py)
    add_xbe_image(AgentUnderFireBond bond_eng.xbe bond.toml ${BOND_PARTITIONS} bond)
    add_xbe_image(AgentUnderFireBondFre bond_fre.xbe bond_fre.toml ${BOND_FRE_PARTITIONS} bond_fre)
    add_xbe_image(AgentUnderFireBondGer bond_ger.xbe bond_ger.toml ${BOND_GER_PARTITIONS} bond_ger)
    # the driving levels are another
    add_xbe_image(AgentUnderFireDriving DRIVING.XBE driving.toml ${DRIVING_PARTITIONS} driving)
endif()

# launcher/icon.ico, when there is one: the game's icon too (resource 1, which the window class loads)
if(WIN32 AND EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/launcher/icon.ico)
    file(WRITE ${CMAKE_CURRENT_BINARY_DIR}/game_icon.rc "1 ICON \"${CMAKE_CURRENT_SOURCE_DIR}/launcher/icon.ico\"\n")
    foreach(t AgentUnderFire AgentUnderFireBond AgentUnderFireBondFre AgentUnderFireBondGer AgentUnderFireDriving)
        if(TARGET ${t})  # the USA disc has no French or German program
            target_sources(${t} PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/game_icon.rc)
        endif()
    endforeach()
endif()
