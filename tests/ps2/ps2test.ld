ENTRY(_start)
SECTIONS
{
    . = 0x00100000;
    .text : { *(.text*) }
    .rodata : { *(.rodata*) }
    .data : { *(.data*) *(.sdata*) }
    .bss : { *(.sbss*) *(.bss*) *(COMMON) }
    /DISCARD/ : { *(.MIPS.abiflags) *(.reginfo) *(.pdr) *(.comment) }
}
