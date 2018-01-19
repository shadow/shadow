#!/bin/sh
# The AMD64 System V ABI (i.e. x86-64 ELF standard) lists a small set of
# registers preserved across function calls -- also known as "callee-saved
# registers", because it's the job of the called function to save them before
# using them. The rest of the registers ("caller-saved registers") implictly
# have no guarantees. Except they do: They can't be modified by a PLT lookup.
# This means we need to make sure our ld.so binary doesn't modify these
# registers (either pushing them to stack first, or simply not using them).
# The original elf-loader people assumed this would be easy, but compilers are
# getting smarter and making use of more obscure registers as time goes on,
# especially with -O3 enabled. So this script tests that all the registers in
# the ld.so executable are calle-saved, or one of the few we push ourselves in
# x86_64/resolv.S

if [ ! -z "$1" ]; then
   ldso=$1
else
   ldso='../ldso'
fi

objdump -d $ldso |
    grep -v "nop" | # nops are just used for data priming, no need to worry
    egrep -o "%[[:alnum:]][[:alnum:]][[:alnum:]]?" | # get all the registers
    # remove everything pushed:
    grep -v "\
%ah\|%al\|%ax\|%eax\|%rax\|\
%ch\|%cl\|%cx\|%ecx\|%rcx\|\
%dh\|%dl\|%dx\|%edx\|%rdx\|\
%si\|%esi\|%rsi\|\
%di\|%edi\|%rdi\|\
%r8\|%r9\|%r10\|%r11\
"| # remove everything callee-saved:
    grep -v "\
%bh\|%bl\|%bx\|%ebx\|%rbx\|\
%bpl\|bp\|%ebp\|%rbp\|\
%r12\|%r13\|%r14\|%r15\|\
%rsp\
"| # these are neither, but we (presumably) know what we're doing:
    grep -qv "%rip\|%fs"
# if grep found anything else, the test has failed
if test $? -eq 1; then
    echo "***** PASS registers *****"
    exit 0
else
    echo "***** FAIL registers *****"
    exit 1
fi
