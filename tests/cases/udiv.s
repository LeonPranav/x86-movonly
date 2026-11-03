# unsigned divide: 255 / 4 = 63
	.globl mo_program
mo_program:
	movl $255, %eax
	movl $0, %edx
	movl $4, %ebx
	divl %ebx
	ret
