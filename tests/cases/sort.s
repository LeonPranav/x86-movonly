# Bubble-sort a global array of 8 ints ascending, then return a[0]+a[7]
# (minimum + maximum after sorting). Exercises nested loops, scaled array
# indexing (disp(,index,4)), signed compares, and in-place swaps — all lowered
# to mov-only.
	.globl mo_program
mo_program:
	movl $0, %esi              # i = 0
.Louter:
	movl $7, %eax              # N-1 = 7
	cmpl %eax, %esi
	jge .Ldone                 # i >= 7 -> sorted
	movl $0, %edi              # j = 0
.Linner:
	movl $7, %eax
	subl %esi, %eax            # eax = 7 - i
	cmpl %eax, %edi
	jge .LinnerEnd             # j >= 7 - i -> next outer
	movl a(,%edi,4), %eax      # x = a[j]
	movl %edi, %ecx
	incl %ecx                  # k = j + 1
	movl a(,%ecx,4), %edx      # y = a[j+1]
	cmpl %edx, %eax            # x - y
	jle .Lnoswap               # x <= y -> no swap
	movl %edx, a(,%edi,4)      # a[j]   = y
	movl %eax, a(,%ecx,4)      # a[j+1] = x
.Lnoswap:
	incl %edi
	jmp .Linner
.LinnerEnd:
	incl %esi
	jmp .Louter
.Ldone:
	movl a+0, %eax             # a[0] = min
	movl a+28, %edx            # a[7] = max
	addl %edx, %eax            # min + max
	ret
	.data
	.globl a
	.align 16
a:
	.long 5, 2, 9, 1, 7, 3, 8, 4
