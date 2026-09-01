set(CMAKE_CXX_STANDARD 23)

file(GLOB_RECURSE SRC_FILES "web_server/*.cpp" "debug/*.cpp" "ble_mesh/*.cpp" "wifi/*.cpp" "sig_models/*.cpp" "sig_companies/*.cpp" "mqtt/*.cpp" "ota/*.cpp" "security/*.cpp")


idf_component_register(SRCS "main.cpp" ${SRC_FILES}
                    PRIV_REQUIRES esp_wifi nvs_flash driver console esp_driver_uart esp_netif mqtt json esp_http_server app_update bt mbedtls bootloader_support
                    INCLUDE_DIRS  ".")

# Warning suppression flags — use target_compile_options, NOT set(CMAKE_*_FLAGS),
# which would overwrite the toolchain -march flag and break RISC-V (C3) builds.
target_compile_options(${COMPONENT_LIB} PRIVATE
    -Wno-missing-field-initializers
    -Wno-unused-value
    $<$<COMPILE_LANGUAGE:C>:-Wno-implicit-fallthrough>
    # -mlongcalls is only needed for Xtensa (ESP32, ESP32-S3) large binaries
    $<$<BOOL:${CONFIG_IDF_TARGET_ESP32}>:-mlongcalls>
    $<$<BOOL:${CONFIG_IDF_TARGET_ESP32S3}>:-mlongcalls>
)

# Pass build date and time as compile definitions
target_compile_definitions(${COMPONENT_LIB} PRIVATE
    BUILD_DATE="${BUILD_DATE}"
    BUILD_TIME="${BUILD_TIME}"
)

# Stage the web assets and pre-gzip the large text files, then pack THAT directory
# into the LittleFS image. style.css / main.js are served with Content-Encoding: gzip
# (see static_handler in web_server.cpp); this keeps the image inside the 224K
# storage partition so the bridge can be updated fully over OTA. company_map.bin is
# left raw (random-access lookup) and the HTML is small enough to leave raw.
set(WEB_SRC ${CMAKE_CURRENT_SOURCE_DIR}/littlefs)
set(WEB_PACKED ${CMAKE_CURRENT_BINARY_DIR}/littlefs_packed)
set(WEB_PACKED_STAMP ${CMAKE_CURRENT_BINARY_DIR}/littlefs_packed.stamp)

add_custom_command(
    OUTPUT ${WEB_PACKED_STAMP}
    COMMAND ${CMAKE_COMMAND} -E rm -rf ${WEB_PACKED}
    COMMAND ${CMAKE_COMMAND} -E copy_directory ${WEB_SRC} ${WEB_PACKED}
    COMMAND gzip -9 -n -f ${WEB_PACKED}/css/style.css ${WEB_PACKED}/js/main.js
    COMMAND ${CMAKE_COMMAND} -E touch ${WEB_PACKED_STAMP}
    DEPENDS
        ${WEB_SRC}/index.html
        ${WEB_SRC}/setup.html
        ${WEB_SRC}/company_map.bin
        ${WEB_SRC}/css/style.css
        ${WEB_SRC}/js/main.js
    VERBATIM
)
add_custom_target(webassets_pack DEPENDS ${WEB_PACKED_STAMP})

littlefs_create_partition_image(storage ${WEB_PACKED} FLASH_IN_PROJECT DEPENDS ${WEB_PACKED_STAMP})
