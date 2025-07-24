set(CMAKE_CXX_STANDARD 23)

set(CMAKE_CXX_FLAGS "-Wno-missing-field-initializers -Wno-unused-value -mlongcalls")

set(CMAKE_C_FLAGS "-Wno-missing-field-initializers -Wno-implicit-fallthrough -Wno-unused-value -mlongcalls")

file(GLOB_RECURSE SRC_FILES "web_server/*.cpp" "debug/*.cpp" "ble_mesh/*.cpp" "wifi/*.cpp" "sig_models/*.cpp" "sig_companies/*.cpp" "mqtt/*.cpp")


idf_component_register(SRCS "main.cpp" ${SRC_FILES}
                    PRIV_REQUIRES esp_wifi nvs_flash driver console esp_driver_uart esp_netif mqtt json esp_http_server
                    INCLUDE_DIRS  ".")

littlefs_create_partition_image(storage ./littlefs FLASH_IN_PROJECT)