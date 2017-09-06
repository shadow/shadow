	.text
	.align 16
	.globl stage0
	.type  stage0,@function
stage0:
	# the ABI suggests to clear this register to mark
	# the deepest stack frame
	xor %rbp, %rbp
	mov %rsp,%rdi
	# set the dl_fini field of TrampolineInformation
	push %rdi
	# set entry_point field of TrampolineInformation
	push %rdi
	# set entry_point_struct field of TrampolineInformation
	push %rdi
	# set load_base field of TrampolineInformation
	lea 0x0(%rip),%rdi
L1:	lea -L1(%rdi),%rdi
	push %rdi
	# set zero at the top of the stack for gdb
	mov $0,%rdi
	push %rdi
	# pass pointer to TrampolineInformation to stage1
	lea 8(%rsp),%rdi
	call stage1
	# read dl_fini field of TrampolineInformation
	mov 32(%rsp),%rdx
	# read entry_point field of TrampolineInformation
	mov 24(%rsp),%rsi
	# read entry_point_struct of TrampolineInformation
	mov 16(%rsp),%rsp
	# clear again for the callee
	xor %rbp, %rbp
	jmp *%rsi
	# trick from glibc: crash if we return here. Should not
	# happen ever
	hlt
	