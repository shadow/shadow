#include "vdl-map.h"
#include "vdl-log.h"
#include "vdl-alloc.h"
#include "vdl-context.h"
#include "vdl-file.h"
#include "vdl-hashmap.h"
#include "vdl-utils.h"
#include "vdl-mem.h"
#include "machine.h"
#include "futex.h"
#include <sys/mman.h>
#include <fcntl.h>

#ifdef DEBUG
static void
debug_print_maps (const char *filename, struct VdlList *maps)
{
  VDL_LOG_DEBUG ("%s", filename);
  void **i;
  for (i = vdl_list_begin (maps);
       i != vdl_list_end (maps);
       i = vdl_list_next (maps, i))
    {
      struct VdlFileMap *map = *i;
      VDL_LOG_DEBUG
        ("r=%u w=%u x=%u file=0x%llx/0x%llx mem=0x%llx/0x%llx zero=0x%llx/0x%llx anon=0x%llx/0x%llx\n",
         (map->mmap_flags & PROT_READ) ? 1 : 0,
         (map->mmap_flags & PROT_WRITE) ? 1 : 0,
         (map->mmap_flags & PROT_EXEC) ? 1 : 0, map->file_start_align,
         map->file_size_align, map->mem_start_align, map->mem_size_align,
         map->mem_zero_start, map->mem_zero_size, map->mem_anon_start_align,
         map->mem_anon_size_align);
    }
}
#else
#define debug_print_maps(filename,maps)
#endif

static void
get_total_mapping_boundaries (struct VdlList *maps,
                              unsigned long *pstart,
                              unsigned long *size, unsigned long *poffset)
{
  unsigned long start = ~0;
  unsigned long end = 0;
  unsigned long offset = ~0;
  void **cur;
  for (cur = vdl_list_begin (maps);
       cur != vdl_list_end (maps);
       cur = vdl_list_next (maps, cur))
    {
      struct VdlFileMap *map = *cur;
      if (start >= map->mem_start_align)
        {
          start = map->mem_start_align;
          offset = map->file_start_align;
        }
      end = vdl_utils_max (end, map->mem_start_align + map->mem_size_align);
    }
  *pstart = start;
  *size = end - start;
  *poffset = offset;
}

static void
file_map_add_load_base (struct VdlFileMap *map, unsigned long load_base)
{
  map->mem_start_align += load_base;
  map->mem_zero_start += load_base;
  map->mem_anon_start_align += load_base;
}

// return -1 if p2 key is below p1's address range, +1 if p2 key is above p1's
// address range, and 0 if it is in that range
int map_address_compare (const void *p1, const void *p2)
{
  // We need this function to compare correctly both in the case that p2 is a
  // valid map (insertions, deletions), and when it is not (queries); p1 should
  // always be a valid map. So we store map->mem_start_align as "key" when
  // map is valid, and the query value as "key" when querying.
  struct VdlFileAddress *p1_addr = (struct VdlFileAddress *) p1;
  struct VdlFileAddress *p2_addr = (struct VdlFileAddress *) p2;
  if (p2_addr->key < p1_addr->key)
    {
      return -1;
    }
  // ">=" instead of ">" because this is page-aligned
  else if (p2_addr->key >= p1_addr->key + p1_addr->map->mem_size_align)
    {
      return 1;
    }
  return 0;
}

static struct VdlFileMap *
pt_load_to_file_map (const ElfW (Phdr) *phdr)
{
  struct VdlFileMap *map = vdl_alloc_new (struct VdlFileMap);
  unsigned long page_size = system_getpagesize ();
  VDL_LOG_ASSERT (phdr->p_type == PT_LOAD, "Invalid program header");
  map->file_start_align = vdl_utils_align_down (phdr->p_offset, page_size);
  map->file_size_align = vdl_utils_align_up (phdr->p_offset + phdr->p_filesz,
                                             page_size) -
    map->file_start_align;
  map->mem_start_align = vdl_utils_align_down (phdr->p_vaddr, page_size);
  map->mem_size_align = vdl_utils_align_up (phdr->p_vaddr + phdr->p_memsz,
                                            page_size) - map->mem_start_align;
  map->mem_anon_start_align =
    vdl_utils_align_up (phdr->p_vaddr + phdr->p_filesz, page_size);
  map->mem_anon_size_align =
    vdl_utils_align_up (phdr->p_vaddr + phdr->p_memsz,
                        page_size) - map->mem_anon_start_align;
  map->mem_zero_start = phdr->p_vaddr + phdr->p_filesz;
  if (map->mem_anon_size_align > 0)
    {
      map->mem_zero_size = map->mem_anon_start_align - map->mem_zero_start;
    }
  else
    {
      map->mem_zero_size = phdr->p_memsz - phdr->p_filesz;
    }
  map->mmap_flags = 0;
  map->mmap_flags |= (phdr->p_flags & PF_X) ? PROT_EXEC : 0;
  map->mmap_flags |= (phdr->p_flags & PF_R) ? PROT_READ : 0;
  map->mmap_flags |= (phdr->p_flags & PF_W) ? PROT_WRITE : 0;
  return map;
}

static struct VdlList *
vdl_file_get_dt_needed (struct VdlFile *file)
{
  VDL_LOG_FUNCTION ("file=%s", file->name);
  struct VdlList *list = vdl_list_new ();
  const char *dt_strtab = file->dt_strtab;
  if (dt_strtab == 0)
    {
      return list;
    }
  ElfW (Dyn) *dynamic = (ElfW (Dyn) *) file->dynamic;
  ElfW (Dyn) *cur;
  for (cur = dynamic; cur->d_tag != DT_NULL; cur++)
    {
      if (cur->d_tag == DT_NEEDED)
        {
          const char *str = (const char *) (dt_strtab + cur->d_un.d_val);
          VDL_LOG_DEBUG ("needed=%s\n", str);
          vdl_list_push_back (list, vdl_utils_strdup (str));
        }
    }
  return list;
}

static char *
replace_magic (char *filename)
{
  char *lib = vdl_utils_strfind (filename, "$LIB");
  if (lib != 0)
    {
      char saved = lib[0];
      lib[0] = 0;
      char *new_filename = vdl_utils_strconcat (filename,
                                                machine_get_lib (),
                                                lib + 4, 0);
      lib[0] = saved;
      vdl_alloc_free (filename);
      VDL_LOG_DEBUG ("magic %s", new_filename);
      return new_filename;
    }
  return filename;
}

static char *
do_search (const char *name, struct VdlList *list)
{
  void **i;
  for (i = vdl_list_begin (list);
       i != vdl_list_end (list);
       i = vdl_list_next (list, i))
    {
      char *fullname = vdl_utils_strconcat (*i, "/", name, 0);
      fullname = replace_magic (fullname);
      if (vdl_utils_exists (fullname))
        {
          return fullname;
        }
      vdl_alloc_free (fullname);
    }
  return 0;
}

static char *
search_filename (const char *name,
                 struct VdlList *rpath, struct VdlList *runpath)
{
  VDL_LOG_FUNCTION ("name=%s", name);
  if (name[0] != '/')
    {
      // if the filename we are looking for does not start with a '/',
      // it is a relative filename so, we can try to locate it with the
      // search dirs.
      char *fullname = 0;
      if (!vdl_list_empty (runpath))
        {
          fullname = do_search (name, runpath);
        }
      else
        {
          fullname = do_search (name, rpath);
        }
      if (fullname != 0)
        {
          return fullname;
        }
      fullname = do_search (name, g_vdl.search_dirs);
      if (fullname != 0)
        {
          return fullname;
        }
    }
  char *realname = replace_magic (vdl_utils_strdup (name));
  if (vdl_utils_exists (realname))
    {
      return realname;
    }
  vdl_alloc_free (realname);
  return 0;
}

static struct VdlFile *
find_by_name (struct VdlContext *context, const char *name)
{
  if (vdl_utils_strisequal (name, "ldso"))
    {
      // we want to make sure that all contexts
      // reuse the same ldso.
      return g_vdl.ldso;
    }
  void **i;
  for (i = vdl_list_begin (context->loaded);
       i != vdl_list_end (context->loaded);
       i = vdl_list_next (context->loaded, i))
    {
      struct VdlFile *cur = *i;
      if (vdl_utils_strisequal (cur->name, name) ||
          (cur->dt_soname != 0 &&
           vdl_utils_strisequal (cur->dt_soname, name)))
        {
          return cur;
        }
    }
  return 0;
}

static struct VdlFile *
find_by_dev_ino (struct VdlContext *context, dev_t dev, ino_t ino)
{
  void **i;
  for (i = vdl_list_begin (context->loaded);
       i != vdl_list_end (context->loaded);
       i = vdl_list_next (context->loaded, i))
    {
      struct VdlFile *cur = *i;
      if (cur->st_dev == dev && cur->st_ino == ino)
        {
          return cur;
        }
    }
  return 0;
}

static int
get_file_info (uint32_t phnum, ElfW (Phdr) *phdr,
               unsigned long *pdynamic, struct VdlList **pmaps)
{
  VDL_LOG_FUNCTION ("phnum=%d, phdr=%p", phnum, phdr);
  ElfW (Phdr) *dynamic = 0, *cur;
  struct VdlList *maps = vdl_list_new ();
  uint32_t i;
  unsigned long align = 0;
  for (i = 0, cur = phdr; i < phnum; i++, cur++)
    {
      if (cur->p_type == PT_LOAD)
        {
          struct VdlFileMap *map = pt_load_to_file_map (cur);
          vdl_list_push_back (maps, map);
          if (align != 0 && cur->p_align != align)
            {
              VDL_LOG_ERROR ("Invalid alignment constraints\n");
              goto error;
            }
          align = cur->p_align;
        }
      else if (cur->p_type == PT_DYNAMIC)
        {
          dynamic = cur;
        }
    }
  if (vdl_list_size (maps) < 1 || dynamic == 0)
    {
      VDL_LOG_ERROR ("file is missing a critical program header "
                     "maps=%u, dynamic=0x%x\n",
                     vdl_list_size (maps), dynamic);
      goto error;
    }
  if (dynamic == 0)
    {
      VDL_LOG_ERROR ("No DYNAMIC PHDR !");
      goto error;
    }
  void **j;
  bool included = false;
  for (j = vdl_list_begin (maps);
       j != vdl_list_end (maps);
       j = vdl_list_next (maps, j))
    {
      struct VdlFileMap *map = *j;
      if (dynamic->p_offset >= map->file_start_align &&
          dynamic->p_offset + dynamic->p_filesz <=
          map->file_start_align + map->file_size_align)
        {
          included = true;
        }
    }
  if (!included)
    {
      VDL_LOG_ERROR ("dynamic not included in any load map\n", 1);
      goto error;
    }

  *pmaps = maps;
  *pdynamic = dynamic->p_vaddr;

  return 1;
error:
  if (maps != 0)
    {
      vdl_list_delete (maps);
    }
  return 0;
}

static struct VdlFile *
file_new (unsigned long load_base, unsigned long dynamic, struct VdlList *maps,
          const char *filename, const char *name, struct VdlContext *context)
{
  struct VdlFile *file = vdl_alloc_new (struct VdlFile);

  vdl_context_add_file (context, file);

  file->lock = rwlock_new ();
  file->load_base = load_base;
  file->filename = vdl_utils_strdup (filename);
  file->dynamic = dynamic + load_base;
  file->next = 0;
  file->prev = 0;
  file->is_not_main_namespace = (context != g_vdl.main_context);
  file->count = 0;
  file->context = context;
  file->st_dev = 0;
  file->st_ino = 0;
  file->maps = maps;
  void **i;
  for (i = vdl_list_begin (maps);
       i != vdl_list_end (maps);
       i = vdl_list_next (maps, i))
    {
      struct VdlFileMap *map = *i;
      file_map_add_load_base (map, load_base);
      map->file = file;
      struct VdlFileAddress *address = vdl_alloc_new (struct VdlFileAddress);
      address->map = map;
      address->key = map->mem_start_align;
      vdl_rbinsert (g_vdl.address_ranges, address);
    }
  file->deps_initialized = 0;
  file->tls_initialized = 0;
  file->init_called = 0;
  file->fini_call_lock = 0;
  file->fini_called = 0;
  file->reloced = 0;
  file->patched = 0;
  file->in_linkmap = 0;
  file->in_shadow_linkmap = 0;
  file->is_executable = 0;
  file->is_interposer = 0;
  // no need to initialize gc_color because it is always
  // initialized when needed by vdl_gc
  file->gc_symbols_resolved_in = vdl_list_new ();
  file->lookup_type = FILE_LOOKUP_GLOBAL_LOCAL;
  file->local_scope = vdl_list_new ();
  file->deps = vdl_list_new ();
  file->name = vdl_utils_strdup (name);
  file->depth = 0;

  // Note: we could theoretically access the content of the DYNAMIC section
  // through the file->dynamic field. However, some platforms (say, i386)
  // are totally braindead: despite the fact that they have no explicit
  // relocation entries to mark the content of the DYNAMIC section as needing
  // relocations, they do perform relocations on some of the entries of this
  // section and _some_ glibc/gcc code relies on the fact that these entries
  // which are mapped rw in the address space of each process are relocated.
  // This is pure madness so, to avoid having to always remember which entries
  // are potentially relocated and when they are relocated (on which platform),
  // we make a copy of all the entries we need here and let machine_reloc_dynamic
  // do its crazy work.
  file->dt_relent = 0;
  file->dt_relsz = 0;
  file->dt_rel = 0;

  file->dt_relaent = 0;
  file->dt_relasz = 0;
  file->dt_rela = 0;

  file->dt_pltgot = 0;
  file->dt_jmprel = 0;
  file->dt_pltrel = 0;
  file->dt_pltrelsz = 0;

  file->dt_strtab = 0;
  file->dt_symtab = 0;
  file->dt_flags = 0;

  file->dt_hash = 0;
  file->dt_gnu_hash = 0;

  file->dt_fini = 0;
  file->dt_fini_array = 0;
  file->dt_fini_arraysz = 0;

  file->dt_init = 0;
  file->dt_init_array = 0;
  file->dt_init_arraysz = 0;

  file->dt_versym = 0;
  file->dt_verdef = 0;
  file->dt_verdefnum = 0;
  file->dt_verneed = 0;
  file->dt_verneednum = 0;

  file->dt_rpath = 0;
  file->dt_runpath = 0;
  file->dt_soname = 0;

  ElfW (Dyn) *dyn = (ElfW (Dyn) *) file->dynamic;
  // do a first pass to get dt_strtab
  while (dyn->d_tag != DT_NULL)
    {
      switch (dyn->d_tag)
        {
        case DT_STRTAB:
          dyn->d_un.d_ptr += file->load_base;
          file->dt_strtab = (const char *) dyn->d_un.d_ptr;
          break;
        }
      dyn++;
    }
  dyn = (ElfW (Dyn) *) file->dynamic;
  while (dyn->d_tag != DT_NULL)
    {
      switch (dyn->d_tag)
        {
        case DT_RELENT:
          file->dt_relent = dyn->d_un.d_val;
          break;
        case DT_RELSZ:
          file->dt_relsz = dyn->d_un.d_val;
          break;
        case DT_REL:
          dyn->d_un.d_ptr += file->load_base;
          file->dt_rel = (ElfW (Rel) *) dyn->d_un.d_ptr;
          break;

        case DT_RELAENT:
          file->dt_relaent = dyn->d_un.d_val;
          break;
        case DT_RELASZ:
          file->dt_relasz = dyn->d_un.d_val;
          break;
        case DT_RELA:
          dyn->d_un.d_ptr += file->load_base;
          file->dt_rela = (ElfW (Rela) *) dyn->d_un.d_ptr;
          break;

        case DT_PLTGOT:
          dyn->d_un.d_ptr += file->load_base;
          file->dt_pltgot = dyn->d_un.d_ptr;
          break;
        case DT_JMPREL:
          dyn->d_un.d_ptr += file->load_base;
          file->dt_jmprel = dyn->d_un.d_ptr;
          break;
        case DT_PLTREL:
          file->dt_pltrel = dyn->d_un.d_val;
          break;
        case DT_PLTRELSZ:
          file->dt_pltrelsz = dyn->d_un.d_val;
          break;

        case DT_SYMTAB:
          dyn->d_un.d_ptr += file->load_base;
          file->dt_symtab = (ElfW (Sym) *) dyn->d_un.d_ptr;
          break;
        case DT_FLAGS:
          file->dt_flags |= dyn->d_un.d_val;
          break;

        case DT_HASH:
          dyn->d_un.d_ptr += file->load_base;
          file->dt_hash = (ElfW (Word) *) dyn->d_un.d_ptr;
          break;
        case DT_GNU_HASH:
          // XXX: In the glibc code that fixes the other dynamic section
          // pointers, something I don't understand is done for DT_GNU_HASH.
          // It's not exposed in the elf documentation for _DYNAMIC, so until
          // I find code that needs it, I'm treating it like everything else.
          // See elf/get-dynamic-info.h in glibc's code if something breaks.
          dyn->d_un.d_ptr += file->load_base;
          file->dt_gnu_hash = (uint32_t *) dyn->d_un.d_ptr;
          break;

        case DT_FINI:
          file->dt_fini = dyn->d_un.d_ptr;
          break;
        case DT_FINI_ARRAY:
          file->dt_fini_array = dyn->d_un.d_ptr;
          break;
        case DT_FINI_ARRAYSZ:
          file->dt_fini_arraysz = dyn->d_un.d_val;
          break;

        case DT_INIT:
          file->dt_init = dyn->d_un.d_ptr;
          break;
        case DT_INIT_ARRAY:
          file->dt_init_array = dyn->d_un.d_ptr;
          break;
        case DT_INIT_ARRAYSZ:
          file->dt_init_arraysz = dyn->d_un.d_val;
          break;

        case DT_VERSYM:
          // XXX: comment for DT_GNU_HASH applies here as well.
          dyn->d_un.d_ptr += file->load_base;
          file->dt_versym = (ElfW (Half) *) dyn->d_un.d_ptr;
          break;
        // glibc doesn't fix dynamic section pointers for verdef or verneed
        case DT_VERDEF:
          file->dt_verdef =
            (ElfW (Verdef) *) (file->load_base + dyn->d_un.d_ptr);
          break;
        case DT_VERDEFNUM:
          file->dt_verdefnum = dyn->d_un.d_val;
          break;
        case DT_VERNEED:
          file->dt_verneed =
            (ElfW (Verneed) *) (file->load_base + dyn->d_un.d_ptr);
          break;
        case DT_VERNEEDNUM:
          file->dt_verneednum = dyn->d_un.d_val;
          break;
        case DT_RPATH:
          VDL_LOG_ASSERT (file->dt_strtab != 0, "no strtab for RPATH");
          file->dt_rpath = file->dt_strtab + dyn->d_un.d_val;
          break;
        case DT_RUNPATH:
          VDL_LOG_ASSERT (file->dt_strtab != 0, "no strtab for RUNPATH");
          file->dt_runpath = file->dt_strtab + dyn->d_un.d_val;
          break;
        case DT_TEXTREL:
          // transfor DT_TEXTREL in equivalent DF_TEXTREL
          file->dt_flags |= DF_TEXTREL;
          break;
        case DT_SONAME:
          file->dt_soname = file->dt_strtab + dyn->d_un.d_val;
          break;
        }
      dyn++;
    }

  // Now, relocate the dynamic section
  machine_reloc_dynamic ((ElfW (Dyn) *) file->dynamic, file->load_base);

  return file;
}

struct VdlMapCacheItem
{
  char *filename;
  unsigned long section;
  int fd;
};

struct VdlMapCacheItem *
map_cache_item_new (const char *filename, const struct VdlFileMap *map,
                    int fd)
{
  struct VdlMapCacheItem *item = vdl_alloc_new (struct VdlMapCacheItem);
  item->filename = vdl_utils_strdup (filename);
  item->section = map->file_start_align;
  item->fd = fd;
  return item;
}

#ifndef DEBUG
static void
readonly_cache_insert (const char *filename, const struct VdlFileMap *map,
                       int fd, unsigned long hash)
{
  struct VdlMapCacheItem *item = map_cache_item_new (filename, map, fd);
  vdl_hashmap_insert (g_vdl.readonly_cache, hash, item);
}

int
readonly_cache_compare (const void *query_void, const void *cached_void)
{
  const struct VdlMapCacheItem *query =
    (const struct VdlMapCacheItem *) query_void;
  const struct VdlMapCacheItem *cached =
    (const struct VdlMapCacheItem *) cached_void;
  return (query->section == cached->section
          && vdl_utils_strisequal (query->filename, cached->filename));
}

static int
readonly_cache_find (const char *filename, const struct VdlFileMap *map,
                     unsigned long hash)
{
  struct VdlMapCacheItem *tmp = map_cache_item_new (filename, map, 0);
  struct VdlMapCacheItem *item =
    (struct VdlMapCacheItem *) vdl_hashmap_get (g_vdl.readonly_cache, hash,
                                                tmp, readonly_cache_compare);
  if (item)
    {
      return item->fd;
    }
  return -1;
}

static unsigned long
readonly_cache_map (const char *filename, const struct VdlFileMap *map,
                    int fd, int prot, unsigned long load_base)
{
  // With enough clever hacking around the locks here, we could reduce
  // contention around insertions, since the ro_cache_futex is really only
  // neccessary on a per-filename basis (we don't want to map the same section
  // twice). E.g., we could use the locks in the linked lists in the hashmap.
  // I'm not doing this now, because it would pretty significantly increase the
  // code complexity, and I can't think of any usecases that involve opening
  // a significant number of *unique* shared objects, meaning insertions are
  // infrequent anyway.
  // Alternatively, we could just remove the lock entirely, since the hashmap
  // API we wrote is itself thread-safe. This would mean that sections could be
  // cached multiple times if there were a race in this function, which would
  // add a memory overhead but not a stability problem (assuming you also added
  // a means to make shm_path unique on each call, not just each process).

  char *section = vdl_utils_itoa (map->file_start_align);
  char *hashname = vdl_utils_strconcat (filename, section);
  unsigned long hash = vdl_gnu_hash (hashname);
  vdl_alloc_free (section);
  vdl_alloc_free (hashname);
  int cfd = readonly_cache_find (filename, map, hash);
  if (cfd < 0)
    {
      futex_lock (g_vdl.ro_cache_futex);
      // double check that the section wasn't cached before we had the lock
      cfd = readonly_cache_find (filename, map, hash);
      if (cfd >= 0)
        {
          futex_unlock (g_vdl.ro_cache_futex);
          goto found;
        }
      // open shared memory to use for all instances of this file section
      cfd =
        system_open (g_vdl.shm_path, O_RDWR | O_CREAT | O_EXCL | O_NOFOLLOW,
                     0700);
      if (cfd < 0)
        {
          VDL_LOG_ERROR ("Could not shmopen cache map file %s: %d\n",
                         g_vdl.shm_path, cfd);
          futex_unlock (g_vdl.ro_cache_futex);
          return 0;
        }
      // Delete the file from the filesystem immediately.
      // We don't need it any more, we just store the file descriptor (which
      // remains valid until closed). This is why it doesn't matter if we
      // re-use the same filename, so long as it's unique to each process.
      system_unlink (g_vdl.shm_path);
      off_t offset = map->file_start_align;

      // copy the section from the original file to the new file descriptor
      int result = system_sendfile (cfd, fd, &offset, map->mem_size_align);
      if (result < 0)
        {
          VDL_LOG_ERROR ("Could not sendfile from %s to %s: %d\n", filename,
                         g_vdl.shm_path, result);
          futex_unlock (g_vdl.ro_cache_futex);
          return 0;
        }

      readonly_cache_insert (filename, map, cfd, hash);
      futex_unlock (g_vdl.ro_cache_futex);
    }
found:
  return (unsigned long) system_mmap ((void *) load_base +
                                      map->mem_start_align,
                                      map->mem_size_align, prot,
                                      MAP_SHARED | MAP_FIXED, cfd, 0);
}
#endif

static void
file_map_do (const char *filename, const struct VdlFileMap *map,
             int fd, int prot, unsigned long load_base)
{
  // Now, map again the area at the right location.
  VDL_LOG_FUNCTION ("file=%s, fd=0x%x, prot=0x%x, load_base=0x%lx", filename,
                    fd, prot, load_base);
  int int_result;
  unsigned long address;
  // shared pages for code breaks gdb, so just don't use them in debug builds
#ifndef DEBUG
  if (!(prot & PROT_WRITE))
    {
      // this area is read-only, so we only load it once
      address = readonly_cache_map (filename, map, fd, prot, load_base);
    }
  else
#endif
    {
      address =
        (unsigned long) system_mmap ((void *) load_base +
                                     map->mem_start_align,
                                     map->mem_size_align, prot,
                                     MAP_PRIVATE | MAP_FIXED, fd,
                                     map->file_start_align);
    }

  VDL_LOG_ASSERT (address == load_base + map->mem_start_align,
                  "Unable to perform remapping");
  if (map->mem_zero_size != 0)
    {
      // make sure that the last partly zero page is PROT_WRITE
      if (!(prot & PROT_WRITE))
        {
          int_result =
            system_mprotect ((void *)
                             vdl_utils_align_down (load_base +
                                                   map->mem_zero_start,
                                                   system_getpagesize ()),
                             system_getpagesize (), prot | PROT_WRITE);
          VDL_LOG_ASSERT (int_result == 0,
                          "Unable to change protection to zeroify last page");
        }
      // zero the end of map
      vdl_memset ((void *) (load_base + map->mem_zero_start), 0,
                  map->mem_zero_size);
      // now, restore the previous protection if needed
      if (!(prot & PROT_WRITE))
        {
          int_result =
            system_mprotect ((void *)
                             vdl_utils_align_down (load_base +
                                                   map->mem_zero_start,
                                                   system_getpagesize ()),
                             system_getpagesize (), prot);
          VDL_LOG_ASSERT (int_result == 0,
                          "Unable to restore protection from last page of mapping");
        }
    }

  if (map->mem_anon_size_align > 0)
    {
      // then, map zero pages.
      address =
        (unsigned long) system_mmap ((void *) load_base +
                                     map->mem_anon_start_align,
                                     map->mem_anon_size_align, prot,
                                     MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED,
                                     -1, 0);
      VDL_LOG_ASSERT (address != (unsigned long) -1,
                      "Unable to map zero pages\n");
    }
}

static struct VdlFile *
vdl_file_map_single (struct VdlContext *context,
                     const char *filename, const char *name)
{
  VDL_LOG_FUNCTION ("context=%p, filename=%s, name=%s", context, filename,
                    name);
  ElfW (Ehdr) header;
  ElfW (Phdr) *phdr = 0;
  ssize_t bytes_read;
  unsigned long mapping_start = 0;
  unsigned long mapping_size = 0;
  unsigned long offset_start = 0;
  int fd = -1;
  struct VdlList *maps;
  unsigned long dynamic;

  fd = system_open_ro (filename);
  if (fd == -1)
    {
      VDL_LOG_ERROR ("Could not open ro target file: %s\n", filename);
      goto error;
    }

  bytes_read = system_read (fd, &header, sizeof (header));
  if (bytes_read == -1 || bytes_read != sizeof (header))
    {
      VDL_LOG_ERROR ("Could not read header read=%d\n", bytes_read);
      goto error;
    }
  // check that the header size is correct
  if (header.e_ehsize != sizeof (header))
    {
      VDL_LOG_ERROR ("header size invalid, %d!=%d\n", header.e_ehsize,
                     sizeof (header));
      goto error;
    }
  if (header.e_type != ET_EXEC && header.e_type != ET_DYN)
    {
      VDL_LOG_ERROR ("header type unsupported, type=0x%x\n", header.e_type);
      goto error;
    }

  phdr = vdl_alloc_malloc (header.e_phnum * header.e_phentsize);
  if (system_lseek (fd, header.e_phoff, SEEK_SET) == -1)
    {
      VDL_LOG_ERROR ("lseek failed to go to off=0x%x\n", header.e_phoff);
      goto error;
    }
  bytes_read = system_read (fd, phdr, header.e_phnum * header.e_phentsize);
  if (bytes_read == -1 || bytes_read != header.e_phnum * header.e_phentsize)
    {
      VDL_LOG_ERROR ("read failed: read=%d\n", bytes_read);
      goto error;
    }

  if (!get_file_info (header.e_phnum, phdr, &dynamic, &maps))
    {
      VDL_LOG_ERROR ("unable to read data structure for %s\n", filename);
      goto error;
    }

  debug_print_maps (filename, maps);

  unsigned long requested_mapping_start;
  get_total_mapping_boundaries (maps, &requested_mapping_start,
                                &mapping_size, &offset_start);

  // If this is an executable, we try to map it exactly at its base address
  int fixed = (header.e_type == ET_EXEC) ? MAP_FIXED : 0;
  // We perform a single initial mmap to reserve all the virtual space we need
  // and, then, we map again portions of the space to make sure we get
  // the mappings we need
  mapping_start =
    (unsigned long) system_mmap ((void *) requested_mapping_start,
                                 mapping_size, PROT_NONE, MAP_PRIVATE | fixed,
                                 fd, offset_start);
  if (mapping_start == (unsigned long) -1)
    {
      VDL_LOG_ERROR ("Unable to allocate complete mapping for %s\n",
                     filename);
      goto error;
    }
  VDL_LOG_ASSERT (!fixed
                  || (fixed
                      && mapping_start == requested_mapping_start),
                  "We need a fixed address and we did not get it but this should have failed mmap");
  // calculate the offset between the start address we asked for and the one we got
  unsigned long load_base = mapping_start - requested_mapping_start;

  // remap the portions we want.
  // To prevent concurrency problems, we don't munmap the mmap at mapping_start.
  // We can do this because the remaps use MAP_FIXED. (see man mmap)
  void **i;
  for (i = vdl_list_begin (maps);
       i != vdl_list_end (maps);
       i = vdl_list_next (maps, i))
    {
      struct VdlFileMap *map = *i;
      file_map_do (filename, map, fd, map->mmap_flags, load_base);
    }

  struct stat st_buf;
  if (system_fstat (filename, &st_buf) == -1)
    {
      VDL_LOG_ERROR ("Unable to stat file %s\n", filename);
      goto error;
    }

  struct VdlFile *file = file_new (load_base, dynamic, maps,
                                   filename, name, context);
  file->st_dev = st_buf.st_dev;
  file->st_ino = st_buf.st_ino;

  file->phdr = phdr;
  file->phnum = header.e_phnum;
  file->e_type = header.e_type;

  system_close (fd);

  vdl_context_notify (context, file, VDL_EVENT_MAPPED);

  return file;
error:
  if (fd >= 0)
    {
      system_close (fd);
    }
  vdl_alloc_free (phdr);
  if (mapping_start != 0)
    {
      system_munmap ((void *) mapping_start, mapping_size);
    }
  return 0;
}

struct SingleMapResult
{
  struct VdlFile *file;
  char *error_string;
  bool newly_mapped;
};

static struct SingleMapResult
vdl_file_map_single_maybe (struct VdlContext *context,
                           const char *requested_filename,
                           struct VdlList *rpath, struct VdlList *runpath)
{
  struct SingleMapResult result;
  result.error_string = 0;
  result.newly_mapped = false;
  result.file = 0;
  // Try to see if we don't have a hardcoded name conversion
  const char *name = vdl_context_lib_remap (context, requested_filename);
  // if the file is already mapped within this context,
  // get it and add it to deps
  result.file = find_by_name (context, name);
  if (result.file != 0)
    {
      return result;
    }
  // Search the file in the filesystem
  char *filename = search_filename (name, rpath, runpath);
  if (filename == 0)
    {
      result.error_string = vdl_utils_sprintf ("Could not find %s", name);
      return result;
    }
  // get information about file.
  struct stat buf;
  if (system_fstat (filename, &buf) == -1)
    {
      result.error_string = vdl_utils_sprintf ("Could not stat %s as %s\n",
                                               name, filename);
      vdl_alloc_free (filename);
      return result;
    }
  // If you create a symlink to a binary and link to the
  // symlinks rather than the underlying binary, the DT_NEEDED
  // entries record different names for the same binary so,
  // the search by name above will fail. So, here, we stat
  // the file we found and check that none of the files
  // already mapped in the same context have the same ino/dev
  // pair. If they do, we don't need to re-map the file
  // and can re-use the previous map.
  result.file = find_by_dev_ino (context, buf.st_dev, buf.st_ino);
  if (result.file != 0)
    {
      vdl_alloc_free (filename);
      return result;
    }
  // The file is really not yet mapped so, we have to map it
  result.file = vdl_file_map_single (context, filename, name);
  VDL_LOG_ASSERT (result.file != 0,
                  "Attempted mapping failed. Try adjusting your system's max_map_count.");
  result.newly_mapped = true;

  vdl_alloc_free (filename);

  return result;
}

void
vdl_file_map_update_depths (struct VdlFile *item)
{
  // we don't want to loop infinitely for circular deps,
  // so mark item as an ancestor before recursing
  struct VdlList *tmp_deps = item->deps;
  item->deps = 0;

  void **cur;
  for (cur = vdl_list_begin (tmp_deps);
       cur != vdl_list_end (tmp_deps);
       cur = vdl_list_next (tmp_deps, cur))
    {
      struct VdlFile *dependency = (struct VdlFile *) *cur;
      if (item->depth + 1 > dependency->depth && dependency->deps)
        {
          dependency->depth = item->depth + 1;
          vdl_file_map_update_depths (dependency);
        }
    }

  item->deps = tmp_deps;
}

char *
vdl_file_map_deps_recursive (struct VdlFile *item,
                             struct VdlList *caller_rpath,
                             struct VdlList *newly_mapped)
{
  VDL_LOG_FUNCTION ("file=%s", item->name);
  char *error = 0;

  if (item == g_vdl.ldso)
    {
      return error;
    }
  if (item->deps_initialized)
    {
      // an additional file added "item" as a dependency after it was loaded
      // we don't need to reload anything, but should update dependecy depths
      vdl_file_map_update_depths (item);
      return error;
    }
  item->deps_initialized = 1;

  struct VdlList *rpath = vdl_utils_splitpath (item->dt_rpath);
  struct VdlList *runpath = vdl_utils_splitpath (item->dt_runpath);
  struct VdlList *current_rpath = vdl_list_copy (rpath);
  vdl_list_insert_range (current_rpath,
                         vdl_list_end (current_rpath),
                         caller_rpath,
                         vdl_list_begin (caller_rpath),
                         vdl_list_end (caller_rpath));

  // get list of deps for the input file.
  struct VdlList *dt_needed = vdl_file_get_dt_needed (item);

  // first, map each dep and accumulate them in deps variable
  void **cur;
  for (cur = vdl_list_begin (dt_needed);
       cur != vdl_list_end (dt_needed);
       cur = vdl_list_next (dt_needed, cur))
    {
      struct SingleMapResult tmp_result;
      tmp_result = vdl_file_map_single_maybe (item->context, *cur,
                                              current_rpath, runpath);
      if (tmp_result.file == 0)
        {
          // oops, failed to find the requested dt_needed
          error = tmp_result.error_string;
          goto out;
        }
      if (tmp_result.newly_mapped)
        {
          vdl_list_push_back (newly_mapped, tmp_result.file);
        }
      tmp_result.file->depth =
        vdl_utils_max (tmp_result.file->depth, item->depth + 1);
      // add the new file to the list of dependencies
      vdl_list_push_back (item->deps, tmp_result.file);
    }

  // then, recursively map the deps of each dep.
  for (cur = vdl_list_begin (item->deps);
       cur != vdl_list_end (item->deps);
       cur = vdl_list_next (item->deps, cur))
    {
      error = vdl_file_map_deps_recursive (*cur, current_rpath, newly_mapped);
      if (error != 0)
        {
          goto out;
        }
    }

out:
  vdl_utils_str_list_delete (rpath);
  vdl_list_delete (current_rpath);
  vdl_utils_str_list_delete (runpath);
  vdl_utils_str_list_delete (dt_needed);
  return error;
}

struct VdlMapResult
vdl_map_from_memory (unsigned long load_base,
                     uint32_t phnum, ElfW (Phdr) *phdr,
                     // a fully-qualified path to the file
                     // represented by the phdr
                     const char *path,
                     // a non-fully-qualified filename
                     const char *filename, struct VdlContext *context)
{
  struct VdlMapResult result;
  unsigned long dynamic;
  struct VdlList *maps;
  int status;

  result.requested = 0;
  result.newly_mapped = vdl_list_new ();
  result.error_string = 0;
  status = get_file_info (phnum, phdr, &dynamic, &maps);
  if (status == 0)
    {
      result.error_string =
        vdl_utils_sprintf ("Unable to obtain mapping information for %s/%s",
                           path, filename);
      goto out;
    }
  struct VdlFile *file = file_new (load_base, dynamic, maps,
                                   path, filename, context);
  file->phdr = vdl_alloc_malloc (phnum * sizeof (ElfW (Phdr)));
  vdl_memcpy (file->phdr, phdr, phnum * sizeof (ElfW (Phdr)));
  file->phnum = phnum;
  vdl_list_push_back (result.newly_mapped, file);

  struct VdlList *empty = vdl_list_new ();
  result.error_string = vdl_file_map_deps_recursive (file, empty,
                                                     result.newly_mapped);
  if (result.error_string == 0)
    {
      result.requested = file;
    }
  vdl_list_delete (empty);

out:
  return result;
}

struct VdlMapResult
vdl_map_from_filename (struct VdlContext *context, const char *filename)
{
  struct VdlMapResult result;
  struct SingleMapResult single;
  struct VdlList *empty = vdl_list_new ();
  result.requested = 0;
  result.error_string = 0;
  result.newly_mapped = vdl_list_new ();
  single = vdl_file_map_single_maybe (context, filename, empty, empty);
  if (single.file == 0)
    {
      result.error_string = single.error_string;
      vdl_list_delete (empty);
      return result;
    }
  if (single.newly_mapped)
    {
      vdl_list_push_back (result.newly_mapped, single.file);
    }
  result.error_string = vdl_file_map_deps_recursive (single.file, empty,
                                                     result.newly_mapped);
  if (result.error_string == 0)
    {
      result.requested = single.file;
    }
  vdl_list_delete (empty);
  return result;
}
