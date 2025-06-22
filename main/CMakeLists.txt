set(CMAKE_CXX_STANDARD 23)

set(CMAKE_CXX_FLAGS "-Wno-missing-field-initializers -Wno-unused-value -mlongcalls")

set(CMAKE_C_FLAGS "-Wno-missing-field-initializers -Wno-implicit-fallthrough -Wno-unused-value -mlongcalls -DSQLITE_OMIT_WAL")

set(srcs "main.cpp" "debugUART.cpp" "debugGPIO.cpp" "provisioning.cpp" "nodesManager.cpp" "debugGPIO.cpp" "modelsConfig.cpp" "wifi/wifiStation.cpp" "ble_mesh.cpp")

idf_component_register(SRCS "sqlite3.c" "${srcs}"
                    PRIV_REQUIRES esp_wifi nvs_flash driver console esp_driver_gpio esp_driver_uart esp_netif mqtt json
                    INCLUDE_DIRS  ".")
