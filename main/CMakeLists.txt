idf_component_register(SRCS "main.c" "wifi.c" "lcd.c" "face.c" "kao.c" "eyes.c" "seq.c" "text.c"
                            "touch.c" "laona.c" "config.c" "llm.c" "web.c"
                       INCLUDE_DIRS "."
                       PRIV_REQUIRES console esp_app_format nvs_flash esp_wifi esp_netif esp_event
                                     esp_driver_spi esp_driver_gpio esp_driver_usb_serial_jtag esp_timer
                                     esp_http_client esp_http_server esp-tls mbedtls json mdns qrcode
                       EMBED_FILES "font.bin"
                       EMBED_TXTFILES "chat.html" "settings.html")
