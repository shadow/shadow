	.text
	.align 16
	.globl stage0
	.type  stage0,@function
stage0:
	# ABI says we need to set this to 0 to mark the deepest frame
	xorl %ebp, %ebp
	movl %esp,%esi
	# set dl_fini field of TrampolineInformation
	pushl %esi
	# set entry_point field of TrampolineInformation
	pushl %esi
	# set entry_point_struct field of TrampolineInformation
	pushl %esi
	# set load_base field of TrampolineInformation
	call __i686.get_pc_thunk.bx
L1:	leal -L1(%ebx),%ebx
	pushl %ebx
	# push pointer to struct TrampolineInformation
	# allocated on the stack
	pushl %esp
	call stage1
	# read dl_fini field of TrampolineInformation
	movl 16(%esp),%edx
	# read entry_point field of TrampolineInformation
	movl 12(%esp),%esi
	# read entry_point_struct of TrampolineInformation
	movl 8(%esp),%esp
	xorl %eax, %eax
	jmp *%esi
	