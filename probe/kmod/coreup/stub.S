/*
 * Runs on the woken core straight from the bootrom: MMU and caches off, ARM
 * state, physical addresses. MPIDR is written to mailbox[0] first, before the
 * under-test check, so a run leaves proof even when the core is wrong and falls
 * through to the kernel's own secondary entry (a cpu1 hotplug that races the
 * test still boots). The page is mapped non-cacheable on the kernel side.
 */
	.text
	.arm
	.globl	coreup_stub_start, coreup_stub_end
	.globl	coreup_stub_want, coreup_stub_fallback, coreup_stub_mailbox
coreup_stub_start:
	mrc	p15, 0, r0, c0, c0, 5
	adr	r2, coreup_stub_mailbox
	str	r0, [r2, #0]
	dsb
	and	r1, r0, #3
	ldr	r6, coreup_stub_want
	cmp	r1, r6
	ldrne	pc, coreup_stub_fallback
	mrc	p15, 0, r3, c0, c0, 0
	str	r3, [r2, #4]
	mov	r4, #0
1:	add	r4, r4, #1
	cmp	r4, #0x100000
	bne	1b
	str	r4, [r2, #8]
	ldr	r5, =0xc0dec0de
	str	r5, [r2, #12]
	dsb
2:	wfi
	b	2b
	.ltorg
coreup_stub_want:
	.word	0
coreup_stub_fallback:
	.word	0
coreup_stub_mailbox:
	.word	0, 0, 0, 0
coreup_stub_end:
