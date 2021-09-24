
#define POINTER (0x0000000002743cdd)

.section .rodata
str1: .asciz "Fixing memory permission...\n"
str2: .asciz "Patching instructions...\n"
end_str:

.section .text
.global gnu_inject

gnu_inject:
	pushq	%rbp
	movq	%rsp, %rbp

	leaq	str1(%rip), %rsi
	movl	$(str2 - str1), %edx
	callq	write

	movl	$10, %eax
	movq	$POINTER, %rdi
	andq	$-4096, %rdi
	movl	$(4096 << 2), %esi
	movl	$0x7, %edx
	syscall

	leaq	str2(%rip), %rsi
	movl	$(end_str - str2), %edx
	callq	write

	mfence
	movl	$0x90, %eax
	movl	$87, %ecx
	movq	$POINTER, %rdi
	cld
	rep	stosb

	movq	%rbp, %rsp
	popq	%rbp
	retq

write:
	pushq	%rbp
	movq	%rsp, %rbp

	movl	$1, %eax
	movl	$1, %edi
	syscall

	movq	%rbp, %rsp
	popq	%rbp
	retq
