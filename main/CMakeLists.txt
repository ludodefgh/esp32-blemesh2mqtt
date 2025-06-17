set(CMAKE_CXX_STANDARD 23)

set(CMAKE_CXX_FLAGS "-Wno-missing-field-initializers -mlongcalls")


set(srcs "main.cpp" "debugUART.cpp" "debugGPIO.cpp" "provisioning.cpp" "nodesManager.cpp" "debugGPIO.cpp" "modelsConfig.cpp")

set(SOURCES MyFile.h MyFile.cpp)

idf_component_register(SRCS "${srcs}"
                    INCLUDE_DIRS  ".")
