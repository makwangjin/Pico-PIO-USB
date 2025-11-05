# Copyright (c) 2021 sekigon-gonnoc
# Original file modified for CH9329 translator project

set(lib_name pico_pio_usb)
add_library(${lib_name} INTERFACE)

set(dir ${CMAKE_CURRENT_LIST_DIR}/src)

pico_generate_pio_header(${lib_name} ${dir}/usb_tx.pio)
pico_generate_pio_header(${lib_name} ${dir}/usb_rx.pio)

target_sources(${lib_name} INTERFACE
    ${dir}/pio_usb.c
    ${dir}/pio_usb_device.c
    ${dir}/pio_usb_host.c
    ${dir}/usb_crc.c
)

target_link_libraries(${lib_name} INTERFACE
    pico_stdlib
    pico_multicore
    hardware_pio
    hardware_dma
)

target_include_directories(${lib_name} INTERFACE ${dir})

# enable all warnings
target_compile_options(${lib_name} INTERFACE -Wall -Wextra)


# ====================================================================
# !!! 최종 수정: examples 폴더를 빌드 목록에 추가합니다 !!!
# 이 명령어가 없었기 때문에 .uf2 파일이 생성되지 않았습니다.
# ====================================================================

add_subdirectory(examples)
