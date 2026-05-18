# Generated Cmake Pico project file
cmake_minimum_required(VERSION 3.13)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(WIN32)
    set(USERHOME $ENV{USERPROFILE})
else()
    set(USERHOME $ENV{HOME})
endif()
set(sdkVersion 2.2.0)
set(toolchainVersion 14_2_Rel1)
set(picotoolVersion 2.2.0-a4)
set(picoVscode ${USERHOME}/.pico-sdk/cmake/pico-vscode.cmake)
if (EXISTS ${picoVscode})
    include(${picoVscode})
endif()

set(PICO_BOARD pico CACHE STRING "Board type")

include(pico_sdk_import.cmake)

project(Prueba_pantalla C CXX ASM)

pico_sdk_init()

add_executable(Prueba_pantalla Prueba_pantalla.c)

pico_set_program_name(Prueba_pantalla "Prueba_pantalla")
pico_set_program_version(Prueba_pantalla "0.1")

pico_enable_stdio_uart(Prueba_pantalla 0)
pico_enable_stdio_usb(Prueba_pantalla 1)

# AQUI AGREGAMOS LA LIBRERIA hardware_spi
target_link_libraries(Prueba_pantalla
        pico_stdlib
        hardware_spi)

target_include_directories(Prueba_pantalla PRIVATE
        ${CMAKE_CURRENT_LIST_DIR}
)

pico_add_extra_outputs(Prueba_pantalla)