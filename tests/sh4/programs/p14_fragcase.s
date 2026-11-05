! A bounded braf switch inside a function fragment whose range ends before one of its cases, as
! discovery leaves when a branch-found fragment is cut at the next entry. Case 0 lies past the
! fragment's end and case 1 inside it. r4 = case index (0..1); returns r0 = 10/20, or -1 for out
! of range. Linked at 0x8c040000 (programs.cmake): the out-of-range case is reached through the
! function table by address.
	.text
	.global _frag
_frag:
	mov	#2,r1
	cmp/hs	r1,r4		! r4 >= 2 ?
	bt	.Lbad
	mov	r4,r0
	shll	r0		! index * 2
	mov	r0,r1
	mova	.Ltab,r0
	mov.w	@(r0,r1),r0	! offset
	braf	r0
	nop
.Lbase:
.Ltab:
	.word	_far - .Lbase	! case 0: outside the fragment
	.word	.Lnear - .Lbase	! case 1: inside it
.Lnear:	rts
	mov	#20,r0
.Lbad:	rts
	mov	#-1,r0
	.global _far
_far:	rts
	mov	#10,r0
