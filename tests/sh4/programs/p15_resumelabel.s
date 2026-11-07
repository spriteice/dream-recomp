! A non-local return into the middle of a function whose range overlaps another's, as discovery
! leaves around a pointer-found entry: Jet Grind Radio's return address 0x0c01bd94, after a `jsr`
! in the function at 0x0c01bc94, also lies in the ranges of nine functions that start inside it,
! and the nearest of them, 0x0c01bd76, has no resume label there.
!
! `_main` is p10_longjmp's: it saves its context with `_save`, then `_deep` restores it and returns
! into `_main` right after the `bsr _save`, with r0 = 1. `_stub` is a small function inside
! `_main`'s code; programs.cmake gives it a range to the end of `_main`, so it is the nearest
! function start below the return address and its range contains it, but it has no label there.
! `_restore` ANDs the saved PR with r5 before it returns: r5 = -1 keeps it, and r5 = 0x1fffffff
! returns to the address's P0 alias, as an interpreted caller running from another alias leaves it.
! Hand-assembled (like p14_fragcase) and checked with `dream-translate disasm`; linked at 0x8c050000
! (programs.cmake) because the return resumes through the function table by address.
	.text
	.global _main
	.global _stub
	.global _save
	.global _deep
	.global _restore
_main:
	sts.l	pr,@-r15
	mov	#5,r9
	mov.l	Lctx,r4
	bra	Lcall
	nop
_stub:
	rts
	mov	#42,r0
Lcall:
	bsr	_save
	nop
Lret:				! the return address: a label of _main, inside _stub's range
	tst	r0,r0
	bf	Lresumed
	mov	#1,r10
	bsr	_deep
	nop
	mov	#99,r11		! never executed: _deep does not return here
Lresumed:
	add	#10,r9		! r9 comes back from the block: 15
	lds.l	@r15+,pr
	rts
	nop
	.align 2
Lctx:	.long	0x8c00f000
_save:
	mov.l	r15,@r4
	mov.l	r14,@(4,r4)
	mov.l	r13,@(8,r4)
	mov.l	r12,@(12,r4)
	mov.l	r11,@(16,r4)
	mov.l	r10,@(20,r4)
	mov.l	r9,@(24,r4)
	mov.l	r8,@(28,r4)
	sts	pr,r0
	mov.l	r0,@(32,r4)
	rts
	mov	#0,r0
_deep:
	sts.l	pr,@-r15
	add	#-16,r15
	mov	#3,r3		! survives the switch: r3 is not in the block
	mov	#7,r9		! does not survive: r9 is restored from the block
	mov.l	Lctx2,r4
	bsr	_restore
	nop
	mov	#77,r12		! never executed
	add	#16,r15
	lds.l	@r15+,pr
	rts
	nop
	.align 2
Lctx2:	.long	0x8c00f000
_restore:
	mov.l	@r4,r15
	mov.l	@(4,r4),r14
	mov.l	@(8,r4),r13
	mov.l	@(12,r4),r12
	mov.l	@(16,r4),r11
	mov.l	@(20,r4),r10
	mov.l	@(24,r4),r9
	mov.l	@(28,r4),r8
	mov.l	@(32,r4),r0
	and	r5,r0		! r5 = -1: the same address; 0x1fffffff: its P0 alias
	lds	r0,pr
	rts
	mov	#1,r0
