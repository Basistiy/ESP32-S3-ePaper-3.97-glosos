cmake_minimum_required(VERSION 3.16)

add_compile_definitions(
    CFG_TUD_CDC=1
    CFG_TUD_CDC_RX_BUFSIZE=512
    CFG_TUD_CDC_TX_BUFSIZE=512
)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(audio_uac_firmware)
