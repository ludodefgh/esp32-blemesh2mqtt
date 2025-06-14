set(srcs "main.c" "debugUART.c" "debugGPIO.c")

idf_component_register(SRCS "debugGPIO.c" "${srcs}"
                    INCLUDE_DIRS  ".")
