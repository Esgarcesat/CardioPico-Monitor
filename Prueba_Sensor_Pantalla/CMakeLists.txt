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

project(Avance_ECG C CXX ASM)

pico_sdk_init()

add_executable(Avance_ECG Avance_ECG.c)

pico_set_program_name(Avance_ECG "Avance_ECG")
pico_set_program_version(Avance_ECG "0.1")

pico_enable_stdio_uart(Avance_ECG 0)
pico_enable_stdio_usb(Avance_ECG 1)

# ENLAZAMOS LAS DOS LIBRERIAS DE HARDWARE NECESARIAS
target_link_libraries(Avance_ECG
        pico_stdlib
        hardware_spi
        hardware_adc)

target_include_directories(Avance_ECG PRIVATE
        ${CMAKE_CURRENT_LIST_DIR}
)

pico_add_extra_outputs(Avance_ECG)