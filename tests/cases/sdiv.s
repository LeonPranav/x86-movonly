# signed divide truncates toward zero: -7 / 2 = -3
	.globl mo_program
mo_program:
	movl $-7, %eax
	cltd
	movl $2, %ebx
	idivl %ebx
	ret
