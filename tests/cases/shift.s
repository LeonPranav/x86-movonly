# shifts: logical/arith, by immediate and by %cl
# 5<<4=80; 80>>2(arith)=20; -64>>3(arith)=-8; 20+(-8)=12; 12>>1(logical)=6
	.globl mo_program
mo_program:
	movl $5, %eax
	sall $4, %eax
	movl $2, %ecx
	sarl %cl, %eax
	movl $-64, %ebx
	sarl $3, %ebx
	addl %ebx, %eax
	shrl $1, %eax
	ret
