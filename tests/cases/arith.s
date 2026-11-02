# straight-line integer arithmetic: (5+3)&15 - 1 = 7
	.globl mo_program
mo_program:
	movl $5, %eax
	movl $3, %ebx
	addl %ebx, %eax
	andl $15, %eax
	decl %eax
	ret
