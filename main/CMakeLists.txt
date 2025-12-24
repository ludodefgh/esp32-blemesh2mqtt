set(CMAKE_CXX_STANDARD 23)

set(CMAKE_CXX_FLAGS "-Wno-missing-field-initializers -Wno-unused-value -mlongcalls")

set(CMAKE_C_FLAGS "-Wno-missing-field-initializers -Wno-implicit-fallthrough -Wno-unused-value -mlongcalls")

file(GLOB_RECURSE SRC_FILES "web_server/*.cpp" "debug/*.cpp" "ble_mesh/*.cpp" "wifi/*.cpp" "sig_models/*.cpp" "sig_companies/*.cpp" "mqtt/*.cpp" "ota/*.cpp" "security/*.cpp")


idf_component_register(SRCS "main.cpp" ${SRC_FILES}
                    PRIV_REQUIRES esp_wifi nvs_flash driver console esp_driver_uart esp_netif mqtt json esp_http_server app_update bt mbedtls bootloader_support
                    INCLUDE_DIRS  ".")

# Pass build date and time as compile definitions
target_compile_definitions(${COMPONENT_LIB} PRIVATE
    BUILD_DATE="${BUILD_DATE}"
    BUILD_TIME="${BUILD_TIME}"
)

littlefs_create_partition_image(storage ./littlefs FLASH_IN_PROJECT)