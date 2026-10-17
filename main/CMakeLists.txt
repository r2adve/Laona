idf_component_register(SRCS "main.c" "wifi.c" "lcd.c" "face.c" "kao.c" "eyes.c" "seq.c" "text.c" "icon.c"
                            "touch.c" "laona.c" "config.c" "llm.c" "web.c" "wifi_setup.c"
                       INCLUDE_DIRS "."
                       PRIV_REQUIRES console esp_app_format nvs_flash esp_wifi esp_netif esp_event
                                     esp_driver_spi esp_driver_gpio esp_driver_usb_serial_jtag esp_timer
                                     esp_http_client esp_http_server esp-tls mbedtls json mdns qrcode
                       EMBED_FILES "font.bin"
                       EMBED_TXTFILES "setup.html")

# 휴대폰 페이지는 gzip 으로 줄여서 넣는다 (와이파이 신호가 약해도 패킷 수가 적어 빨리 뜸).
# web.c 는 Content-Encoding: gzip 으로 그대로 보냄
foreach(page chat settings)
    set(gz "${CMAKE_CURRENT_BINARY_DIR}/${page}.html.gz")
    add_custom_command(OUTPUT ${gz}
                       COMMAND gzip -9 -n -c "${CMAKE_CURRENT_SOURCE_DIR}/${page}.html" > ${gz}
                       DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/${page}.html"
                       VERBATIM)
    add_custom_target(${page}_html_gz DEPENDS ${gz})
    add_dependencies(${COMPONENT_LIB} ${page}_html_gz)
    target_add_binary_data(${COMPONENT_LIB} ${gz} BINARY)
endforeach()
