set(CMAKE_SYSTEM_NAME Generic)
set(CMAKE_SYSTEM_PROCESSOR ARM)

set(CMAKE_C_COMPILER   arm-none-eabi-gcc)
set(CMAKE_ASM_COMPILER arm-none-eabi-gcc)
set(CMAKE_OBJCOPY      arm-none-eabi-objcopy)
set(CMAKE_SIZE         arm-none-eabi-size)

# 默认 CPU，可在命令行覆盖
if(NOT DEFINED CMAKE_C_FLAGS_INIT)
    set(CMAKE_C_FLAGS_INIT "-mthumb -mcpu=cortex-m4 -O2 -g")
    set(CMAKE_C_FLAGS_INIT "${CMAKE_C_FLAGS_INIT} -ffunction-sections -fdata-sections -Wall")
    set(CMAKE_ASM_FLAGS_INIT "-mthumb -mcpu=cortex-m4")
endif()

# 测试编译器时只生成静态库，避免裸机链接错误
set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)
