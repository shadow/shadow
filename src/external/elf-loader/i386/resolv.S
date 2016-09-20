	.text
	.align 16
	.globl machine_resolve_trampoline
	.type  machine_resolve_trampoline,@function
	# This trampoline is really tricky because it can't assume
	# anything about the calling conventions of the caller or
	# the resolved callee. Specifically, it must be really
	# careful not to clobber any caller-saved registers
machine_resolve_trampoline:
	# first, save all caller-saved registers
	pushl %eax
	pushl %ecx
	pushl %edx
	# push reloc offset on stack
	pushl 16(%esp)
	# push GOT[1] on stack
	pushl 16(%esp)
	call vdl_reloc_offset_jmprel
	# was lookup successful ? (position z flag)
	test %eax,%eax
	# save return value in case we have to jump there
	movl %eax,24(%esp)
	# clear the arguments we pushed on the stack
	pop %edx
	pop %edx
	# restore caller-saved registers
	pop %edx
	pop %ecx
	pop %eax
	# test the z flag: must do this before addl
	# because addl will clobber the z flag.
	jz fail
	# cleanup the second argument pushed by PLT0
	addl $4,%esp
	# jump on return value from vdl_reloc_offset_jmprel
	ret
	# cleanup both arguments pushed by PLT0 and PLTX respectively
fail:	addl $8,%esp
	# return to the caller
	ret
