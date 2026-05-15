MEMORY
{
    RAM (rwx) : ORIGIN = 0x00000000, LENGTH = 4M
}

SECTIONS
{
    . = ORIGIN(RAM);

    .vectors : {
        KEEP(*(.vectors))
    }

    .text : {
        *(.text*)
        *(.rodata*)
    }

    .data : {
        *(.data*)
    }

    .bss : {
        *(.bss*)
        *(COMMON)
    }

    __stack_bottom = ORIGIN(RAM) + LENGTH(RAM);
}
