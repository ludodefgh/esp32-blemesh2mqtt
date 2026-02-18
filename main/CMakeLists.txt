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

littlefs_create_partition_image(storage ./littlefs FLASH_IN_PROJECT)
