# ESP-IDF/PlatformIO component for the Minecraft server.
# Enumerate C sources only; do not pass YAML or build files to the compiler.
file(GLOB app_sources "${CMAKE_CURRENT_LIST_DIR}/*.c")

idf_component_register(
  SRCS ${app_sources}
  INCLUDE_DIRS "." "../include"
  PRIV_REQUIRES esp_wifi esp_event nvs_flash esp_timer lwip
)

# Pre-existing warnings in the upstream C implementation are not firmware
# build blockers; true compiler errors still fail CI.
target_compile_options(${COMPONENT_LIB} PRIVATE -Wno-error)
