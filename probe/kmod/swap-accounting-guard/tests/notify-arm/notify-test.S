.syntax unified
.arch armv7-a
.arm
.section .text
.global _start
_start:
    ldr r0, =sem
    mov r1, #0
    str r1, [r0]
    ldr r12, =0x0017bc4c
    blx r12
    cmp r0, #1
    bne fail
    ldr r0, =sem
    ldr r1, [r0]
    cmn r1, #1
    bne fail
    ldr r12, =0x0017bc84
    blx r12
    ldr r0, =sem
    ldr r1, [r0]
    cmp r1, #0
    bne fail
    mov r1, #1
    str r1, [r0]
    ldr r12, =0x0017bc4c
    blx r12
    cmp r0, #0
    bne fail
    ldr r0, =sem
    ldr r1, [r0]
    cmp r1, #1
    bne fail
    mvn r1, #0
    str r1, [r0]
    ldr r12, =0x0017bc4c
    blx r12
    cmp r0, #0
    bne fail
    ldr r0, =sem
    ldr r1, [r0]
    cmn r1, #1
    bne fail
    mov r0, #0
    mov r1, #17
    bl run_case
    mov r0, #1
    mov r1, #63
    bl run_case
    mvn r0, #0
    mov r1, #255
    bl run_case
    mov r0, #7
    mov r1, #4096
    bl run_case
    mov r0, #0
    movw r1, #65535
    bl run_case
    mov r0, #1
    ldr r1, =pass_text
    ldr r2, pass_length
    mov r7, #4
    svc #0
    mov r0, #0
    mov r7, #1
    svc #0
run_case:
    push {r3, lr}
    ldr r2, =sem
    str r0, [r2]
    ldr r2, =expected_activity
    str r0, [r2]
    ldr r2, =expected_index
    str r1, [r2]
    mov r3, #0
    cmp r0, #0
    moveq r3, #1
    ldr r2, =expected_free
    str r3, [r2]
    ldr r2, =free_calls
    mov r3, #0
    str r3, [r2]
    mov r4, #0x44
    mov r5, #0x55
    mov r6, #0x66
    mov r7, #0x77
    mov r8, #0x88
    mov r9, #0x99
    mov r10, sp
    mov fp, #0xbb
    ldr r0, =bdev
    ldr r12, =0x004481d8
    blx r12
    cmp r4, #0x44
    bne fail
    cmp r5, #0x55
    bne fail
    cmp r6, #0x66
    bne fail
    cmp r7, #0x77
    bne fail
    cmp r8, #0x88
    bne fail
    cmp r9, #0x99
    bne fail
    cmp sp, r10
    bne fail
    cmp fp, #0xbb
    bne fail
    ldr r0, =free_calls
    ldr r0, [r0]
    ldr r1, =expected_free
    ldr r1, [r1]
    cmp r0, r1
    bne fail
    ldr r0, =sem
    ldr r0, [r0]
    ldr r1, =expected_activity
    ldr r1, [r1]
    cmp r0, r1
    bne fail
    pop {r3, pc}
fail:
    mov r0, #1
    ldr r1, =fail_text
    ldr r2, fail_length
    mov r7, #4
    svc #0
    mov r0, #1
    mov r7, #1
    svc #0
pass_text:
    .ascii "PASS: ARM stock trylock 1/0; callback 5 cases; ABI preserved; free/unlock only on success; no blocking down_write or stats call\n"
pass_end:
fail_text:
    .ascii "FAIL: ARM callback/return/callee argument/state check\n"
fail_end:
.align 2
pass_length: .word pass_end-pass_text
fail_length: .word fail_end-fail_text
.ltorg
.section .data
.align 2
bdev:
    .space 0x5c
    .word disk
disk:
    .space 0x190
    .word zram
zram:
    .space 8
sem:
    .word 0, 0
wait_list:
    .word wait_list, wait_list
free_calls: .word 0
expected_activity: .word 0
expected_index: .word 0
expected_free: .word 0
.section .mcount,"ax"
    add sp, sp, #4
    bx lr
.section .rw_public,"ax"
.ifdef FTRACE_NOP
    .incbin "rw-public-nop.bin"
.else
    .incbin "rw-public.bin"
.endif
.section .rw_inner,"ax"
    .incbin "rw-inner.bin"
.section .zram_code,"ax"
    b fail
    .org 0x40
    ldr r2, =zram
    cmp r0, r2
    bne fail
    ldr r2, =expected_index
    ldr r2, [r2]
    cmp r1, r2
    bne fail
    ldr r2, =sem
    ldr r2, [r2]
    cmn r2, #1
    bne fail
    ldr r2, =free_calls
    ldr r3, [r2]
    add r3, r3, #1
    str r3, [r2]
    bx lr
    .ltorg
    .org 0x130
.ifdef STOCK
.ifdef FTRACE_NOP
    .incbin "notify-stock-nop.bin"
.else
    .incbin "notify-stock.bin"
.endif
.else
.ifdef FTRACE_NOP
    .incbin "notify-trylock-nop.bin"
.else
    .incbin "notify-trylock.bin"
.endif
.endif
.section .blocking_down,"ax"
    b fail
.section .lock_save,"ax"
    mov r0, #0
    bx lr
.section .lock_restore,"ax"
    bx lr
