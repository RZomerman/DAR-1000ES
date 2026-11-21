function(sony_ota_key component)
    if(DEFINED ENV{SONY_OTA_PUBLIC_KEY} AND NOT "$ENV{SONY_OTA_PUBLIC_KEY}" STREQUAL "")
        configure_file("$ENV{SONY_OTA_PUBLIC_KEY}" "${CMAKE_CURRENT_BINARY_DIR}/sony_ota_public.pem" COPYONLY)
        file(READ "${CMAKE_CURRENT_BINARY_DIR}/sony_ota_public.pem" key_hex HEX)
        string(REGEX REPLACE "(..)" "0x\\1," key_bytes "${key_hex}")
        file(CONFIGURE OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/sony_ota_public.h"
            CONTENT "static const unsigned char ota_key[] = {@key_bytes@ 0};\n" @ONLY)
        target_include_directories(${component} PRIVATE "${CMAKE_CURRENT_BINARY_DIR}")
        target_compile_definitions(${component} PRIVATE SONY_OTA_HAS_KEY=1)
    endif()
endfunction()

if(CONFIG_SONY_UART_AUDIO_NODE)
    idf_component_register(
        SRCS "sony_uart_audio_main.c" "sony_uart_link.c" "sony_uart_protocol.c"
        INCLUDE_DIRS "."
        PRIV_REQUIRES driver esp_timer)
    return()
endif()

if(CONFIG_SONY_CONTROLLER_NODE)
    idf_component_register(
        SRCS "sony_controller_main.c" "sony_controller_radio.c" "sony_controller_menu.c" "sony_search_index.c"
             "settings.c" "network/wifi.c" "network/sony_ota.c"
             "sony_uart_link.c" "sony_uart_protocol.c" "sony_station_wire.c"
             "sony_screen.c" "sony_startup.c" "sony_keypad.c"
             "sony_capture_replay.c" "sony_display_frame.c" "sony_text.c"
             "audio/radio_display_text.c"
             "sony_radio_status.c" "sony_display_modes.c"
             "sony_spectrum.c" "sony_spectrum_dsp.c" "sony_timing.c"
        INCLUDE_DIRS "." "audio" "network"
        PRIV_REQUIRES driver nvs_flash esp_wifi esp_netif esp_event esp_timer
                      esp_http_server esp_http_client app_update json mbedtls
                      esp_lcd spiffs dac
        EMBED_FILES "../tests/fixtures/screen/mono-blink.rle"
                    "../tests/fixtures/screen/frequency.rle")
    sony_ota_key(${COMPONENT_LIB})
    return()
endif()

set( SRC_FILES
    "main.c"
    "alac_magic_cookie.c"
    "settings.c"
    "audio/audio_receiver.c"
    "audio/audio_anchor_checks.cpp"
    "audio/audio_stream.c"
    "audio/audio_stream_realtime.c"
    "audio/audio_stream_buffered.c"
    "audio/audio_decoder.c"
    "audio/radio.c"
    "audio/icy_metadata.c"
    "audio/radio_display_text.c"
    "audio/audio_buffer.c"
    "audio/audio_timing.c"
    "audio/audio_crypto.c"
    "audio/audio_resample.c"
    "rtsp/rtsp_server.c"
    "rtsp/rtsp_conn.c"
    "rtsp/rtsp_message.c"
    "rtsp/rtsp_handlers.c"
    "rtsp/rtsp_fairplay.c"
    "rtsp/rtsp_crypto.c"
    "rtsp/rtsp_rsa.c"
    "rtsp/rtsp_events.c"
    "plist/plist_xml.c"
    "plist/bplist_parser.c"
    "plist/bplist_builder.c"
    "plist/base64.c"
    "hap/hap.c"
    "hap/hap_pair_verify.c"
    "hap/hap_pair_setup.c"
    "hap/hap_crypto.c"
    "hap/srp.c"
    "hap/tlv8.c"
    "network/wifi.c"
    "network/mdns_airplay.c"
    "network/ptp_clock.c"
    "network/ntp_clock.c"
    "network/socket_utils.c"
    "network/web_server.c"
    "network/ota.c"
    "network/dns_server.c"
    "network/log_stream.c"
    "led.c"
    "audio/eq_events.c"
    "buttons.c"
    "playback_control.c"
    "dacp_client.c"
)

# Select audio output implementation at compile time. audio_output_common.c
# carries weak defaults for the optional entry points, so a backend only has
# to implement what it actually supports.
list(APPEND SRC_FILES "audio/audio_output_common.c")
if(CONFIG_SONY_UART_LINK)
    list(APPEND SRC_FILES "sony_uart_link.c" "sony_uart_protocol.c" "sony_station_wire.c")
endif()
if(CONFIG_SONY_SPLIT_AUDIO_NODE)
    list(APPEND SRC_FILES "sony_spectrum.c" "sony_spectrum_dsp.c" "network/sony_ota.c")
endif()
if(CONFIG_SONY_OPTICAL_BENCH)
    list(APPEND SRC_FILES "network/sony_optical_bench.c")
endif()
if(CONFIG_SONY_SCREEN_BENCH)
    list(APPEND SRC_FILES "sony_screen.c" "sony_startup.c")
    list(APPEND SRC_FILES "sony_keypad.c")
    list(APPEND SRC_FILES "sony_capture_replay.c")
    list(APPEND SRC_FILES "sony_display_frame.c")
    list(APPEND SRC_FILES "sony_text.c")
    list(APPEND SRC_FILES "sony_radio_status.c")
    list(APPEND SRC_FILES "sony_display_modes.c")
    list(APPEND SRC_FILES "sony_spectrum.c" "sony_spectrum_dsp.c")
    list(APPEND SRC_FILES "sony_timing.c")
    set(SCREEN_EMBED_FILES "../tests/fixtures/screen/mono-blink.rle"
                           "../tests/fixtures/screen/frequency.rle")
endif()
if(CONFIG_SONY_MANUAL_CONTROLS)
    list(APPEND SRC_FILES "audio/sony_manual_controls.c")
endif()
if(CONFIG_AUDIO_OUTPUT_SPDIF)
    list(APPEND SRC_FILES "audio/audio_output_spdif.c")
    list(APPEND SRC_FILES "audio/spdif_encoder_checks.cpp")
elseif(CONFIG_AUDIO_OUTPUT_USB)
    list(APPEND SRC_FILES "audio/audio_output_usb.c")
else()
    list(APPEND SRC_FILES "audio/audio_output.c")
endif()

# Bluetooth A2DP sink (only available on ESP32 with Classic BT)
if(CONFIG_BT_A2DP_ENABLE)
    list(APPEND SRC_FILES "audio/a2dp_sink.c")
    list(APPEND SRC_FILES "bt_coex.c")
endif()

# W5500 SPI Ethernet
if(CONFIG_ETH_W5500_ENABLED)
    list(APPEND SRC_FILES "network/ethernet.c")
endif()

set( INC_DIRS
    "." 
    "audio" 
    "rtsp" 
    "plist" 
    "hap" 
    "network"
)

set( DEPS
    driver
    nvs_flash
    esp_wifi
    esp_netif
    esp_event
    esp_ringbuf
    esp_driver_i2s
    esp_timer
    esp_http_server
    esp_http_client
    app_update
    json
    mbedtls
    esp_driver_ledc
    boards
    dac
    dac_tas58xx
    dac_tas57xx
    display
    audio-resampler
    esp_eth
    spiffs_storage
)
if(CONFIG_SONY_SCREEN_BENCH)
    list(APPEND DEPS esp_lcd)
endif()

# Bluetooth component only needed when A2DP is enabled
#if(CONFIG_BT_A2DP_ENABLE)
    list(APPEND DEPS bt)
#endif()

# Ethernet component only needed when W5500 is enabled
#if(CONFIG_ETH_W5500_ENABLED)
    list(APPEND DEPS esp_eth)
#endif()

idf_component_register(
    SRCS ${SRC_FILES}
    INCLUDE_DIRS ${INC_DIRS}
    PRIV_REQUIRES ${DEPS}
    EMBED_FILES ${SCREEN_EMBED_FILES}
)
if(CONFIG_SONY_SPLIT_AUDIO_NODE)
    sony_ota_key(${COMPONENT_LIB})
endif()
if(CONFIG_SONY_OPTICAL_ISOLATED_PLAYBACK)
    if(CONFIG_SONY_I2S_MIRROR)
        set(radio_html "${CMAKE_CURRENT_SOURCE_DIR}/../data/www/radio.html")
        set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${radio_html}")
        file(READ "${radio_html}" radio_html_hex HEX)
        string(REGEX REPLACE "(..)" "0x\\1," radio_html_bytes "${radio_html_hex}")
        file(CONFIGURE OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/dar_radio_html.h"
             CONTENT "static const char dar_radio_html[] = {@radio_html_bytes@0};\n" @ONLY)
    endif()
    set(startup_wav "${CMAKE_CURRENT_SOURCE_DIR}/../../media/dar-startup-sound-v1.wav")
    set(startup_script "${CMAKE_CURRENT_SOURCE_DIR}/../scripts/prepare_startup_pcm.py")
    set(startup_pcm "${CMAKE_CURRENT_BINARY_DIR}/dar_startup_pcm.h")
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
                 "${startup_wav}" "${startup_script}")
    execute_process(COMMAND "${PYTHON}" "${startup_script}" "${startup_wav}" "${startup_pcm}"
                    COMMAND_ERROR_IS_FATAL ANY)
    target_include_directories(${COMPONENT_LIB} PRIVATE "${CMAKE_CURRENT_BINARY_DIR}")
endif()

# Enable function wrapping for abort() to intercept panics and mute GPIO
if(CONFIG_BOARD_SQUEEZEAMP_COMMON)
    target_link_libraries(${COMPONENT_LIB} INTERFACE "-Wl,--wrap=abort")
endif()
