set(CMAKE_CXX_STANDARD 23)

# Base compiler flags for all targets
set(CMAKE_CXX_FLAGS "-Wno-missing-field-initializers -Wno-unused-value")
set(CMAKE_C_FLAGS "-Wno-missing-field-initializers -Wno-implicit-fallthrough -Wno-unused-value")

# Add -mlongcalls for Xtensa targets (ESP32, ESP32-S3) - needed for large binaries
# RISC-V targets (C3, C6) don't support this flag
if(CONFIG_IDF_TARGET_ESP32 OR CONFIG_IDF_TARGET_ESP32S3)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -mlongcalls")
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -mlongcalls")
endif()

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