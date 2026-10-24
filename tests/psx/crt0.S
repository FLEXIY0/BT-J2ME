# Вход программы и вызовы BIOS (таблица B по адресу 0xB0).
    .set noreorder
    .set noat
    .text
    .global _start
_start:
    la    $gp, _gp
    la    $t0, __bss_start
    la    $t1, _end
1:  sw    $zero, 0($t0)
    sltu  $at, $t0, $t1
    bnez  $at, 1b
    addiu $t0, $t0, 4
    jal   main
    nop
2:  b     2b
    nop

    .global InitPad
InitPad:
    li    $t2, 0xB0
    jr    $t2
    li    $t1, 0x12

    .global StartPad
StartPad:
    li    $t2, 0xB0
    jr    $t2
    li    $t1, 0x13
