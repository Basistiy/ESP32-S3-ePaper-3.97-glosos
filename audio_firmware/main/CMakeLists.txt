idf_component_register(WHOLE_ARCHIVE
                       SRCS "main.cpp"
                            "usb_descriptors.c"
                            "DEV_Config.c"
                            "EPD_3in97.cpp"
                            "GUI_Paint.cpp"
                            "fonts.cpp"
                            "chat_display.cpp"
                       INCLUDE_DIRS "."
                       REQUIRES usb_device_uac esp_codec_dev tinyusb driver esp_timer esp_driver_gpio)

# Provide tusb_config.h and uac headers to tinyusb
idf_component_get_property(uac_dir espressif__usb_device_uac COMPONENT_DIR)
idf_build_get_property(build_components BUILD_COMPONENTS)
if(tinyusb IN_LIST build_components)
    set(tinyusb_name tinyusb)
else()
    set(tinyusb_name espressif__tinyusb)
endif()

idf_component_get_property(tusb_lib ${tinyusb_name} COMPONENT_LIB)
target_include_directories(${tusb_lib} PUBLIC 
    "${CMAKE_CURRENT_SOURCE_DIR}" 
    "${uac_dir}/tusb_uac"
    "${uac_dir}/include"
)
