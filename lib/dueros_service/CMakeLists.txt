set(COMPONENT_ADD_INCLUDEDIRS include)

# Edit following two lines to set component requirements (see docs)
set(COMPONENT_REQUIRES )
set(COMPONENT_PRIV_REQUIRES clouds mbedtls audio_board audio_hal esp-adf-libs audio_sal esp_peripherals audio_recorder)

set(COMPONENT_SRCS ./dueros_service.c ./esp_audio_device_info.c)

set(COMPONENT_EMBED_FILES duer_profile)

register_component()
