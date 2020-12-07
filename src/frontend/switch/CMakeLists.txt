find_program(ELF2NRO elf2nro)
find_program(NACPTOOL nacptool)
find_program(UAM uam)

project(switch)

SET(SOURCES_SWITCH
    main.cpp
    Platform.cpp
    PlatformConfig.cpp
    Gfx.cpp
    BoxGui.cpp
    BackButton.cpp
    Filebrowser.cpp
    KeyExplanations.cpp
    ROMMetaDatabase.cpp
    SettingsDialog.cpp
    StartMenu.cpp
    FaultHandler.cpp

    ../Util_ROM.cpp
    ../Util_Audio.cpp
    ../Util_Video.cpp

    mm_vec/mm_vec.c
    stb_truetype/stb_truetype.c
    stb_image/stb_image.c
)

add_executable(melonDS.elf ${SOURCES_SWITCH})
if (${CMAKE_BUILD_TYPE} STREQUAL Release)
    target_link_libraries(melonDS.elf nx deko3d core)
else()
    if (ENABLE_LTO)
        target_link_libraries(melonDS.elf nxd deko3dd core)
    else()
        target_link_libraries(melonDS.elf core deko3dd nxd)
    endif()
endif()

target_include_directories(melonDS.elf PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_include_directories(melonDS.elf PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/..")
target_include_directories(melonDS.elf PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/../..")

add_custom_command(OUTPUT control.nacp
                   COMMAND ${NACPTOOL} --create "melonDS switch fast alpha" "Arisotura, RSDuck, Hydr8gon" "release 5" ${CMAKE_BINARY_DIR}/control.nacp)

make_directory(${CMAKE_BINARY_DIR}/romfs/shaders)

function(compile_shader stage filename)
    add_custom_command(OUTPUT romfs/shaders/${filename}.dksh
        COMMAND ${UAM} --stage=${stage} --out ${CMAKE_BINARY_DIR}/romfs/shaders/${filename}.dksh ${CMAKE_CURRENT_SOURCE_DIR}/${filename}.glsl
        DEPENDS ${filename}.glsl
    )
endfunction()

compile_shader(vert Default_vsh)
compile_shader(frag Default_fsh)
compile_shader(frag SmoothEdges_fsh)

add_custom_target(melonDS.nro ALL
    ${ELF2NRO} ${CMAKE_BINARY_DIR}/melonDS.elf ${CMAKE_BINARY_DIR}/melonDS.nro --icon=${CMAKE_SOURCE_DIR}/icon/melon_switch.jpg --nacp=${CMAKE_BINARY_DIR}/control.nacp --romfsdir=${CMAKE_BINARY_DIR}/romfs
    DEPENDS melonDS.elf control.nacp romfs/shaders/Default_vsh.dksh romfs/shaders/Default_fsh.dksh romfs/shaders/SmoothEdges_fsh.dksh)