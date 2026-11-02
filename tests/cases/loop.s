# sum 1..5 = 15
	.globl mo_program
mo_program:
	movl $0, %eax
	movl $1, %ecx
.Lloop:
	cmpl $5, %ecx
	jg .Ldone
	addl %ecx, %eax
	incl %ecx
	jmp .Lloop
.Ldone:
	ret
