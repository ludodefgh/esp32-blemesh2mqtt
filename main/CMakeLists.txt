set(CMAKE_CXX_STANDARD 23)

set(CMAKE_CXX_FLAGS "-Wno-missing-field-initializers -Wno-unused-value -mlongcalls")

set(CMAKE_C_FLAGS "-Wno-missing-field-initializers -Wno-implicit-fallthrough -Wno-unused-value -mlongcalls -DSQLITE_OMIT_WAL")

set(srcs "main.cpp" "debug/debugUART.cpp" "debug/debugGPIO.cpp" "ble_mesh_node.cpp" "wifi/wifiStation.cpp")

idf_component_register(SRCS "mqtt_control.cpp" "ble_mesh_control.cpp" "ble_mesh_provisioning.cpp" "${srcs}"
                    PRIV_REQUIRES esp_wifi nvs_flash driver console esp_driver_uart esp_netif mqtt json esp_http_server
                    INCLUDE_DIRS  ".")
