cmake_minimum_required(VERSION 3.13)
project(LinRTOS C ASM)

# ============================================================
# 🎯 工具链检测与配置
# ============================================================

if(NOT DEFINED CMAKE_C_COMPILER)
    find_program(ARM_CC arm-none-eabi-gcc)
    if(ARM_CC)
        set(CMAKE_C_COMPILER ${ARM_CC})
        set(CMAKE_ASM_COMPILER ${ARM_CC})
    else()
        message(WARNING "arm-none-eabi-gcc not found, falling back to system gcc (syntax check only)")
    endif()
endif()

set(CMAKE_C_STANDARD 99)

# ============================================================
# ⚙️ 编译选项
# ============================================================

if(CMAKE_C_COMPILER MATCHES "arm-none-eabi")
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -mthumb -mcpu=cortex-m4 -O2 -g")
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -ffunction-sections -fdata-sections")
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wall -Wextra -Werror=return-type")
    set(CMAKE_ASM_FLAGS "${CMAKE_ASM_FLAGS} -mthumb -mcpu=cortex-m4")
else()
    # PC 语法检查模式：定义裸机宏以通过编译
    add_definitions(-D__thumb__ -DSyntaxCheckOnly)
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wall -Wextra")
endif()

# ============================================================
# 📂 头文件
# ============================================================

include_directories(include)

# ============================================================
# 📦 源文件
# ============================================================

set(LINRTOS_SRC
    src/sched.c
    src/task.c
    src/tick.c
    src/port/cortex_m/port.c
    src/port/cortex_m/port_asm.S
)

# ============================================================
# 🔗 库目标
# ============================================================

add_library(linrtos STATIC ${LINRTOS_SRC})
