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
	# set zero at the top of the stack for gdb
	mov $0,%rdi
	push %rdi
	# pass pointer to TrampolineInformation to stage1
	lea 8(%rsp),%rdi
        # save the stack pointer for when we come back
        # (r12 is preserved by functions and syscalls)
        movq %rsp, %r12
        # align the stack to 16 bytes
        andq $-16, %rsp
	call stage1
        # get the stack pointer back
        movq %r12, %rsp
	# read dl_fini field of TrampolineInformation
	mov 24(%rsp),%rdx
	# read entry_point field of TrampolineInformation
	mov 16(%rsp),%rsi
	# read entry_point_struct of TrampolineInformation
	mov 8(%rsp),%rsp
	# clear again for the callee
	xor %rbp, %rbp
	jmp *%rsi
	# trick from glibc: crash if we return here. Should not
	# happen ever
	hlt
