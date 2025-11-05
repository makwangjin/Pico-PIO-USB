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

# [수정 완료] 아래에 있던 add_subdirectory(examples) 명령을 제거했습니다.
# 이 명령이 재귀 호출 오류(Max Recursion Depth Exceeded)의 원인이었습니다.
