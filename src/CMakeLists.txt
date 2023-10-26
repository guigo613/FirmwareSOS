set(COMPONENT_SRCS voip_app.c sip_service.c timer_wakeup.c gpio_wakeup.c)
set(COMPONENT_ADD_INCLUDEDIRS .)

register_component()

add_prebuilt_library(tracevia_protocols "C:/Users/voip/FirmwareSOS/lib/libtracevia_protocols.a")
target_link_libraries(${COMPONENT_LIB} tracevia_protocols)