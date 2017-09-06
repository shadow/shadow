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
	# (we don't bother with the mmx+sse+fp registers because we
	# can assume that the dynamic loader will not touch them)
	push %rax
	push %rcx
	push %rdx
	push %rsi
	push %rdi
	push %r8
	push %r9
	push %r10
	push %r11
	# setup 1st arg: struct VdlFile *file
	mov 0x48(%rsp), %rdi
	# setup 2nd arg: unsigned long index
	mov 0x50(%rsp), %rsi
	call vdl_reloc_index_jmprel
	# was lookup successful ? (position z flag)
	test %rax,%rax
	# save return value in case we have to jump there
	mov %rax,0x50(%rsp)
	# restore caller-saved registers
	pop %r11
	pop %r10
	pop %r9
	pop %r8
	pop %rdi
	pop %rsi
	pop %rdx
	pop %rcx
	pop %rax
	# test the z flag: must do this before addl
	# because add will clobber the z flag.
	jz fail
	# cleanup argument pushed by PLT0
	add $8,%rsp
	# jump on return value from vdl_reloc_index_jmprel
	ret
	# cleanup both arguments pushed by PLT0 and PLTX
fail:	add $16,%rsp
	# return to the caller
	ret
	