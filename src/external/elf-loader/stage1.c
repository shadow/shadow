#include "dprintf.h"
#include "stage1.h"
#include "stage2.h"
#include "system.h"
#include "vdl.h"
#include "futex.h"
#include "vdl-alloc.h"
#include "vdl-list.h"
#include "vdl-utils.h"
#include "machine.h"
#include "glibc.h"
#include <elf.h>
#include <link.h>


#define READ_LONG(p)				\
  ({long v = *((long*)p);			\
    p+=sizeof(long);				\
    v;})

#define READ_POINTER(p)				\
  ({char * v = *((char**)p);			\
    p+=sizeof(char*);				\
    v;})
static struct Stage2Input
prepare_stage2 (unsigned long entry_point_struct)
{
  struct Stage2Input stage2_input;
  unsigned long tmp = entry_point_struct;
  ElfW (auxv_t) * auxvt, *auxvt_tmp;
  // although the C convention for argc is to be an int (as such, 4 bytes
  // on both i386 and x86_64), the kernel ABI has a long argc (as such,
  // 4 bytes on i386 and 8 bytes on x86_64).
  stage2_input.program_argc = READ_LONG (tmp);  // skip argc
  DPRINTF ("argc=0x%x\n", stage2_input.program_argc);
  stage2_input.program_argv = (char **) tmp;
  tmp += sizeof (char *) * (stage2_input.program_argc + 1);     // skip argv
  stage2_input.program_envp = (char **) tmp;
  while (READ_POINTER (tmp) != 0) {}    // skip envp
  auxvt = (ElfW (auxv_t) *) tmp;        // save aux vector start
  stage2_input.sysinfo = 0;
  stage2_input.program_phdr = 0;
  stage2_input.program_phnum = 0;
  auxvt_tmp = auxvt;
  while (auxvt_tmp->a_type != AT_NULL)
    {
      if (auxvt_tmp->a_type == AT_PHDR)
        {
          stage2_input.program_phdr = (ElfW (Phdr) *) auxvt_tmp->a_un.a_val;
        }
      else if (auxvt_tmp->a_type == AT_PHNUM)
        {
          stage2_input.program_phnum = auxvt_tmp->a_un.a_val;
        }
      else if (auxvt_tmp->a_type == AT_SYSINFO)
        {
          stage2_input.sysinfo = auxvt_tmp->a_un.a_val;
        }
      auxvt_tmp++;
    }
  if (stage2_input.program_phdr == 0 || stage2_input.program_phnum == 0)
    {
      system_exit (-3);
    }
  return stage2_input;
}

static void
global_initialize (unsigned long interpreter_load_base)
{
  // after this call to vdl_alloc_initialize is completed,
  // we are allowed to allocate heap memory.
  vdl_alloc_initialize ();

  struct Vdl *vdl = &g_vdl;
  vdl->version = 1;
  vdl->link_map = 0;
  vdl->breakpoint = 0;
  vdl->state = VDL_CONSISTENT;
  vdl->interpreter_load_base = interpreter_load_base;
  vdl->bind_now = 0;            // by default, do lazy binding
  vdl->finalized = 0;
  vdl->ldso = 0;
  vdl->contexts = vdl_list_new ();
  vdl->search_dirs = vdl_utils_splitpath (machine_get_system_search_dirs ());
  vdl->tls_gen = 1;
  vdl->tls_static_total_size = 0;
  vdl->tls_static_current_size = 0;
  vdl->tls_static_align = 0;
  vdl->tls_n_dtv = 0;
  vdl->tls_next_index = 1;
  vdl->futex = futex_new ();
  vdl->errors = vdl_list_new ();
  vdl->n_added = 0;
  vdl->n_removed = 0;
  vdl->module_map_len = 0;
  vdl->module_map = 0;
}



// relocate entries in DT_REL
static void
relocate_dt_rel (ElfW (Dyn) * dynamic, unsigned long load_base)
{
  ElfW (Dyn) * tmp = dynamic;
  ElfW (Rel) * dt_rel = 0;
  unsigned long dt_relsz = 0;
  unsigned long dt_relent = 0;
  ElfW (Rela) * dt_rela = 0;
  unsigned long dt_relasz = 0;
  unsigned long dt_relaent = 0;
  // search DT_REL, DT_RELSZ, DT_RELENT, DT_RELA, DT_RELASZ, DT_RELAENT
  while (tmp->d_tag != DT_NULL &&
         (dt_rel == 0 || dt_relsz == 0 || dt_relent == 0 ||
          dt_rela == 0 || dt_relasz == 0 || dt_relaent == 0))
    {
      //DEBUG_HEX(tmp->d_tag);
      if (tmp->d_tag == DT_REL)
        {
          dt_rel = (ElfW (Rel) *) (load_base + tmp->d_un.d_ptr);
        }
      else if (tmp->d_tag == DT_RELSZ)
        {
          dt_relsz = tmp->d_un.d_val;
        }
      else if (tmp->d_tag == DT_RELENT)
        {
          dt_relent = tmp->d_un.d_val;
        }
      else if (tmp->d_tag == DT_RELA)
        {
          dt_rela = (ElfW (Rela) *) (load_base + tmp->d_un.d_ptr);
        }
      else if (tmp->d_tag == DT_RELASZ)
        {
          dt_relasz = tmp->d_un.d_val;
        }
      else if (tmp->d_tag == DT_RELAENT)
        {
          dt_relaent = tmp->d_un.d_val;
        }
      tmp++;
    }
  DPRINTF
    ("dt_rel=0x%x, dt_relsz=%d, dt_relent=%d, dt_rela=0x%x, dt_relasz=%d, dt_relaent=%d\n",
     dt_rel, dt_relsz, dt_relent, dt_rela, dt_relasz, dt_relaent);

  // relocate entries in dt_rel and dt_rela.
  // since we are relocating the dynamic loader itself here,
  // the entries will always be of type R_XXX_RELATIVE.
  uint32_t i;
  for (i = 0; i < dt_relsz; i += dt_relent)
    {
      ElfW (Rel) * tmp = (ElfW (Rel) *) (((uint8_t *) dt_rel) + i);
      ElfW (Addr) * reloc_addr = (void *) (load_base + tmp->r_offset);
      if (!machine_reloc_is_relative (ELFW_R_TYPE (tmp->r_info)))
        {
          DPRINTF ("Invalid reloc entry type: %s\n",
                   machine_reloc_type_to_str (ELFW_R_TYPE (tmp->r_info)));
          goto error;
        }
      *reloc_addr += (ElfW (Addr)) load_base;
    }
  for (i = 0; i < dt_relasz; i += dt_relaent)
    {
      ElfW (Rela) * tmp = (ElfW (Rela) *) (((uint8_t *) dt_rela) + i);
      ElfW (Addr) * reloc_addr = (void *) (load_base + tmp->r_offset);
      if (!machine_reloc_is_relative (ELFW_R_TYPE (tmp->r_info)))
        {
          DPRINTF ("Invalid reloc entry type: %s\n",
                   machine_reloc_type_to_str (ELFW_R_TYPE (tmp->r_info)));
          goto error;
        }
      *reloc_addr = (ElfW (Addr)) load_base + tmp->r_addend;
    }

  // Note that, technically, we could also relocate DT_JMPREL entries but
  // this would be fairly complex so, it's easier to just make sure that
  // our generated ldso binary does not contain any.
error:
  return;
}

void
stage1_finalize (void)
{
  stage2_finalize ();
  g_vdl.finalized = 1;
}

void
stage1_freeres (void)
{
  // We are called by libc_freeres_interceptor which is our wrapper
  // around __libc_freeres.
  // If stage1_freeres is called while g_vdl.finalized is set to 1, it
  // means that stage1_finalize has already run which means that
  // we are called by valgrind. If this is so, we do perform final shutdown
  // of everything here. We are allowed to do so because
  // this function will return to vgpreload_core and the process
  // will terminate immediately after.
  if (!g_vdl.finalized)
    {
      return;
    }
  stage2_freeres ();
  vdl_utils_str_list_delete (g_vdl.search_dirs);
  vdl_list_delete (g_vdl.contexts);
  futex_delete (g_vdl.futex);
  {
    void **i;
    for (i = vdl_list_begin (g_vdl.errors);
         i != vdl_list_end (g_vdl.errors); i = vdl_list_next (i))
      {
        struct VdlError *error = *i;
        vdl_alloc_free (error->prev_error);
        vdl_alloc_free (error->error);
        vdl_alloc_delete (error);
      }
    vdl_list_delete (g_vdl.errors);
  }

  // After this call, we can't do any malloc/free anymore.
  vdl_alloc_destroy ();

  g_vdl.search_dirs = 0;
  g_vdl.contexts = 0;
  g_vdl.futex = 0;
  g_vdl.errors = 0;
}

// Called from stage0 entry point asm code.
void
stage1 (struct Stage1InputOutput *input_output)
{
  // The linker defines the symbol _DYNAMIC to give you the offset from
  // the load base to the start of the PT_DYNAMIC area which has been
  // mapped by the OS loader as part of the rw PT_LOAD segment.
  void *dynamic = _DYNAMIC;
  dynamic += input_output->load_base;
  relocate_dt_rel (dynamic, input_output->load_base);

  // Now that access to global variables is possible, we initialize
  // our main global variable. After this function call completes,
  // we are allowed to do memory allocations.
  global_initialize (input_output->load_base);

  // Set the "end of the stack" variable to the frame address of this function,
  // which appears to be close enough (within a page) to the end as far as
  // glibc and libpthreads are concerned. See glibc.c for more information.
  glibc_set_stack_end (__builtin_frame_address (0));

  struct Stage2Input stage2_input =
    prepare_stage2 (input_output->entry_point_struct);
  stage2_input.interpreter_load_base = input_output->load_base;

  // Now that we have relocated this binary, we can access global variables
  // so, we switch to stage2 to complete the loader initialization.
  struct Stage2Output stage2_output = stage2_initialize (stage2_input);

  // We are all done, so we update the caller's data structure to be able
  // jump in the program's entry point.
  input_output->entry_point = stage2_output.entry_point;
  input_output->dl_fini = 0;
  input_output->dl_fini = (unsigned long) stage1_finalize;
}
