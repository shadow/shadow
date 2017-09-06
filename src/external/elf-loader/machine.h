#ifndef MACHINE_H
#define MACHINE_H

#include <elf.h>
#include <link.h>
#include <stdbool.h>
#include <sys/syscall.h>
#include "vdl.h"

struct VdlLookupResult;

// returns whether the type of reloc is a R_XXX_RELATIVE relocation
// entry. The input to this function is the output of the ELFXX_TYPE macro.
bool machine_reloc_is_relative (unsigned long reloc_type);
// returns whether the type of reloc is a R_XXX_COPY relocation entry
// the input to this function is the output of the ELFXX_TYPE macro.
bool machine_reloc_is_copy (unsigned long reloc_type);
void machine_reloc (const struct VdlFile *file,
                    unsigned long *reloc_addr,
                    unsigned long reloc_type,
                    unsigned long reloc_addend,
                    unsigned long symbol_value);
const char *machine_reloc_type_to_str (unsigned long reloc_type);
void machine_reloc_dynamic (ElfW (Dyn) * dyn, unsigned long load_base);
bool machine_insert_trampoline (unsigned long from, unsigned long to,
                                unsigned long from_size);
void machine_reloc_irelative (struct VdlFile *file);
void machine_lazy_reloc (struct VdlFile *file);
// return old value
uint32_t machine_atomic_compare_and_exchange (uint32_t * val, uint32_t old,
                                              uint32_t new_value);
// return old value
uint32_t machine_atomic_dec (uint32_t * val);
const char *machine_get_system_search_dirs (void);
const char *machine_get_lib (void);
void *machine_system_mmap (void *start, size_t length, int prot, int flags,
                           int fd, off_t offset);
void machine_thread_pointer_set (unsigned long tp);
unsigned long machine_thread_pointer_get (void);

long int machine_syscall0 (int name);
long int machine_syscall1 (int name, unsigned long int a1);
long int machine_syscall2 (int name,
                           unsigned long int a1, unsigned long int a2);
long int machine_syscall3 (int name,
                           unsigned long int a1, unsigned long int a2,
                           unsigned long int a3);
long int machine_syscall4 (int name,
                           unsigned long int a1, unsigned long int a2,
                           unsigned long int a3, unsigned long int a4);
long int machine_syscall6 (int name,
                           unsigned long int a1, unsigned long int a2,
                           unsigned long int a3, unsigned long int a4,
                           unsigned long int a5, unsigned long int a6);

#define MACHINE_SYSCALL0(name)			\
  machine_syscall0 (__NR_##name)
#define MACHINE_SYSCALL1(name,a1)		\
  machine_syscall1 (__NR_##name, (unsigned long)a1)
#define MACHINE_SYSCALL2(name,a1,a2)		\
  machine_syscall2 (__NR_##name, (unsigned long)a1, (unsigned long)a2)
#define MACHINE_SYSCALL3(name,a1,a2,a3)		\
  machine_syscall3 (__NR_##name, (unsigned long)a1, (unsigned long)a2, (unsigned long)a3)
#define MACHINE_SYSCALL4(name,a1,a2,a3,a4)				\
  machine_syscall4 (__NR_##name, (unsigned long)a1, (unsigned long)a2, (unsigned long)a3, \
		    (unsigned long)a4)
#define MACHINE_SYSCALL6(name,a1,a2,a3,a4,a5,a6)	\
  machine_syscall6 (__NR_##name, (unsigned long)a1, (unsigned long)a2, (unsigned long)a3, \
		    (unsigned long)a4, (unsigned long)a5, (unsigned long)a6)

#endif /* MACHINE_H */
