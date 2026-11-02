# if (7 > 3) return 100 else 200
	.globl mo_program
mo_program:
	movl $7, %eax
	movl $3, %ebx
	cmpl %ebx, %eax
	jle .Lelse
	movl $100, %eax
	jmp .Lend
.Lelse:
	movl $200, %eax
.Lend:
	ret
