# multiply (2-op and 3-op) + signed divide with cltd
# 7*6=42; 42*3=126; 126/5 -> q=25 r=1; q+r=26
	.globl mo_program
mo_program:
	movl $7, %eax
	movl $6, %ebx
	imull %ebx, %eax
	imull $3, %eax, %eax
	cltd
	movl $5, %ebx
	idivl %ebx
	addl %edx, %eax
	ret
