# Enlazado del cargador: sin crt0 de la libc (el arranque está en AbBootloader.cpp)
# y con las variantes pequeñas de newlib (memcpy, memset, memcmp).
Import("env")
env.Append(LINKFLAGS=["-nostartfiles", "--specs=nano.specs", "--specs=nosys.specs",
                      "-Wl,--gc-sections", "-Wl,-Map=${BUILD_DIR}/bootloader.map"])
