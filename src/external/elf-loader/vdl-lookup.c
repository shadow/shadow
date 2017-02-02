#include "vdl-lookup.h"
#include "vdl-log.h"
#include "vdl-utils.h"
#include "vdl-list.h"
#include "vdl-context.h"
#include "vdl-file.h"
#include <stdint.h>

#ifndef STT_GNU_IFUNC
// magic value decided by our glibc maintainer friends.
#define STT_GNU_IFUNC 10
#endif

static uint32_t
vdl_gnu_hash (const char *s)
{
  // Copy/paste from the glibc source code.
  // This function is coming from comp.lang.c and was originally
  // posted by Daniel J Bernstein
  uint32_t h = 5381;
  unsigned char c;
  for (c = *s; c != '\0'; c = *++s)
    {
      h = h * 33 + c;
    }
  return h;
}

static unsigned long
vdl_elf_hash (const char *n)
{
  // Copy/paste from the ELF specification (figure 2-9)
  const unsigned char *name = (const unsigned char *) n;
  unsigned long h = 0, g;
  while (*name)
    {
      h = (h << 4) + *name++;
      if ((g = (h & 0xf0000000)))
        h ^= g >> 24;
      h &= ~g;
    }
  return h;
}

struct VdlFileLookupIterator
{
  const char *name;
  const char *dt_strtab;
    ElfW (Sym) * dt_symtab;
  enum
  {
    ELF_HASH,
    GNU_HASH,
    ELF_SYM,
    NO_SYM
  } type;
  union
  {
    struct
    {
      signed long current;
        ElfW (Word) * chain;
    } elf;
    struct
    {
      uint32_t current;
      uint32_t *cur_hash;
    } gnu;
  } u;
};

static struct VdlFileLookupIterator
vdl_lookup_file_begin (const struct VdlFile *file,
                       const char *name,
                       unsigned long elf_hash, uint32_t gnu_hash)
{
  VDL_LOG_FUNCTION ("name=%s, elf_hash=0x%lx, gnu_hash=0x%x, file=%s",
                    name, elf_hash, gnu_hash, file->filename);
  struct VdlFileLookupIterator i;
  i.name = name;
  // first, gather information needed to look into the hash table
  i.dt_strtab = file->dt_strtab;
  i.dt_symtab = file->dt_symtab;
  ElfW (Word) * dt_hash = file->dt_hash;
  uint32_t *dt_gnu_hash = file->dt_gnu_hash;

  if (i.dt_strtab == 0 || i.dt_symtab == 0)
    {
      i.type = NO_SYM;
    }
  else if (dt_gnu_hash != 0)
    {
      i.type = NO_SYM;          // by default, unless we can find a matching chain
      // read header
      uint32_t nbuckets = dt_gnu_hash[0];
      uint32_t symndx = dt_gnu_hash[1];
      uint32_t maskwords = dt_gnu_hash[2];
      uint32_t shift2 = dt_gnu_hash[3];
      // read other parts of hash table
      ElfW (Addr) * bloom = (ElfW (Addr) *) (dt_gnu_hash + 4);
      uint32_t *buckets =
        (uint32_t *) (((unsigned long) bloom) +
                      maskwords * sizeof (ElfW (Addr)));
      uint32_t *chains = &buckets[nbuckets];

      // test against the Bloom filter
      uint32_t hashbit1 = gnu_hash % __ELF_NATIVE_CLASS;
      uint32_t hashbit2 = (gnu_hash >> shift2) % __ELF_NATIVE_CLASS;
      ElfW (Addr) bitmask1 = 1;
      bitmask1 <<= hashbit1;
      ElfW (Addr) bitmask2 = 1;
      bitmask2 <<= hashbit2;
      ElfW (Addr) bitmask = bitmask1 | bitmask2;
      ElfW (Addr) bitmask_word =
        bloom[(gnu_hash / __ELF_NATIVE_CLASS) % maskwords];
      if ((bitmask_word & bitmask) == bitmask)
        {
          // check bucket
          uint32_t chain = buckets[gnu_hash % nbuckets];
          if (chain != 0)
            {
              // we have the start of the chain !
              i.type = GNU_HASH;
              i.u.gnu.current = chain;
              i.u.gnu.cur_hash = &chains[chain - symndx];
            }
        }
    }
  else if (dt_hash != 0)
    {
      i.type = ELF_HASH;
      // Then, look into the hash table itself.
      // First entry is number of buckets
      // Second entry is number of chains
      ElfW (Word) nbuckets = dt_hash[0];
      i.u.elf.chain = &dt_hash[2 + nbuckets];
      // the code below is tricky: normally, the index of the
      // first entry we want to look at in the hash table is
      // 2+(hash%nbuckets) relative to the dt_hash pointer.
      // what we calculate below is the index in the hash table
      // relative to the chain pointer and the reason we do
      // this is that all other indexes in the hash chain
      // are relative to the chain pointer so, using an index
      // relative to the chain pointer all the time allows us
      // to use the same logic in has_next all the time.
      i.u.elf.current = -(nbuckets - (elf_hash % nbuckets));
    }
  else
    {
      i.type = ELF_SYM;
    }

  return i;
}

static int
vdl_lookup_file_has_next (const struct VdlFileLookupIterator *i)
{
  switch (i->type)
    {
    case NO_SYM:
      return 0;
      break;
    case ELF_HASH:
      {
        unsigned long prev = i->u.elf.current;
        unsigned long current = i->u.elf.chain[i->u.elf.current];
        unsigned long found = 0;
        while (current != 0)
          {
            // The values stored in the hash table are
            // an index in the symbol table.
            if (i->dt_symtab[current].st_name != 0 &&
                i->dt_symtab[current].st_shndx != SHN_UNDEF)
              {
                // the symbol name is an index in the string table
                if (vdl_utils_strisequal
                    (i->dt_strtab + i->dt_symtab[current].st_name, i->name))
                  {
                    found = 1;
                    break;
                  }
              }
            prev = current;
            current = i->u.elf.chain[current];
          }
        // as an optimization, to save us from iterating again
        // in the _next function, we set the current position
        // to the previous entry to find the matching entry
        // immediately upon our call to _next.
        ((struct VdlFileLookupIterator *) i)->u.elf.current = prev;
        return found;
      }
      break;
    case GNU_HASH:
      {
        unsigned long current = i->u.gnu.current;
        uint32_t *cur_hash = i->u.gnu.cur_hash;
        unsigned long found = 0;
        while (cur_hash != 0)
          {
            // The values stored in the hash table are
            // an index in the symbol table.
            if (i->dt_symtab[current].st_name != 0 &&
                i->dt_symtab[current].st_shndx != SHN_UNDEF)
              {
                // the symbol name is an index in the string table
                if (vdl_utils_strisequal
                    (i->dt_strtab + i->dt_symtab[current].st_name, i->name))
                  {
                    found = 1;
                    break;
                  }
              }
            if ((*cur_hash & 0x1) == 0x1)
              {
                cur_hash = 0;
                continue;
              }
            cur_hash++;
            current++;
          }
        // as an optimization, to save us from iterating again
        // in the _next function, we set the current position
        // to the previous entry to find the matching entry
        // immediately upon our call to _next.
        struct VdlFileLookupIterator *i_unconst =
          (struct VdlFileLookupIterator *) i;
        i_unconst->u.gnu.current = current;
        i_unconst->u.gnu.cur_hash = cur_hash;
        return found;
      }
      break;
    case ELF_SYM:
      return 0;
      break;
    }
  VDL_LOG_ASSERT (0, "We can't reach here");
  return 0;
}

// return index in dt_symtab
static unsigned long
vdl_lookup_file_next (struct VdlFileLookupIterator *i)
{
  switch (i->type)
    {
    case NO_SYM:
      VDL_LOG_ASSERT (0, "This is a programming error");
      break;
    case ELF_HASH:
      {
        VDL_LOG_ASSERT (vdl_lookup_file_has_next (i),
                        "Next called while no data to read");
        // We return the entry immediately following the
        // 'current' index and update the 'current' index
        // to point to the next entry.
        unsigned long next = i->u.elf.chain[i->u.elf.current];
        i->u.elf.current = next;
        return next;
      }
      break;
    case GNU_HASH:
      VDL_LOG_ASSERT (vdl_lookup_file_has_next (i),
                      "Next called while no data to read");
      unsigned long next = i->u.gnu.current;
      if ((*(i->u.gnu.cur_hash) & 0x1) == 0x1)
        {
          // if we have reached the end of the hash array,
          // we remember about it.
          i->u.gnu.cur_hash = 0;
        }
      else
        {
          // otherwise, goto the next entry
          i->u.gnu.current++;
          i->u.gnu.cur_hash++;
        }
      return next;
      break;
    case ELF_SYM:
      return 0;
      break;
    }
  VDL_LOG_ASSERT (0, "We can't reach here");
  return 0;
}

enum VdlVersionMatch
{
  VERSION_MATCH_PERFECT,
  VERSION_MATCH_AMBIGUOUS,
  VERSION_MATCH_BAD
};

// we have a matching symbol but we have a version
// requirement so, we must check that the matching
// symbol's version also matches.
// interestingly, we ignore from_ver_filename in
// this function. It's not that it would be horrendously
// hard to handle it but it would make our life harder
// for the symbol replacement policy we use.
// For more information on what this function is doing, see
// elf-loader/doc/references/symbol-versioning
static enum VdlVersionMatch
symbol_version_matches (const struct VdlFile *in,
                        const struct VdlFile *from,
                        const char *from_ver_name,
                        const char *from_ver_filename,
                        unsigned long from_ver_hash, unsigned long in_index)
{
  VDL_LOG_FUNCTION ("%s %s %ld %ld\n", from_ver_name ? from_ver_name : "",
                    from_ver_filename ? from_ver_filename : "", from_ver_hash,
                    in_index);
  ElfW (Half) * in_dt_versym = in->dt_versym;
  if (from_ver_name == 0 || from_ver_filename == 0)
    {
      // we have no version requirement.
      if (in_dt_versym == 0)
        {
          // we have no version requirement and no version definition so,
          // these are the normal symbol matching rules without version information.
          // we match !
          return VERSION_MATCH_PERFECT;
        }
      if (in_dt_versym != 0)
        {
          // we have no version requirement but we do have a version definition.
          // if this is a base definition, we are good.
          uint16_t ver_index = in_dt_versym[in_index];
          if (ver_index == 1)
            {
              return VERSION_MATCH_PERFECT;
            }
          // if this is not a base definition, maybe we will find the base
          // definition later. In the meantime, we report that we have
          // found an ambiguous match.
          return VERSION_MATCH_AMBIGUOUS;
        }
    }
  else
    {
      // ok, so, now, we have version requirements information.
      ElfW (Verdef) * in_dt_verdef = in->dt_verdef;

      if (in_dt_versym == 0 || in_dt_verdef == 0)
        {
          // we have a version requirement but no version definition in this object.
          // before accepting this match, we do a sanity check: we verify that
          // this object ('in') is not explicitly the one required by verneed
          if (from_ver_filename != 0)
            {
              VDL_LOG_ASSERT (!vdl_utils_strisequal
                              (from_ver_filename, in->name),
                              "Required symbol does not exist in required object file");
            }
          // anyway, we do match now
          return VERSION_MATCH_PERFECT;
        }
      uint16_t ver_index = in_dt_versym[in_index];

      // we have version information in both the 'from' and the 'in'
      // objects.
      if (ver_index == 0)
        {
          // this is a symbol with local scope
          // it's ok only if we reference it within the same file.
          if (in == from)
            {
              return VERSION_MATCH_PERFECT;
            }
          else
            {
              return VERSION_MATCH_BAD;
            }
        }
      if (ver_index & 0x8000)
        {
          // if the high bit is set, this means that it is a 'hidden' symbol
          // which means that it can't be referenced from outside of its binary.
          if (in != from)
            {
              // the matching symbol we found is hidden and is located
              // in a different binary. Not ok.
              return VERSION_MATCH_BAD;
            }
        }
      const char *in_dt_strtab = in->dt_strtab;
      // find the corresponding index in the verdef array
      ElfW (Verdef) * cur, *prev;
      for (prev = 0, cur = in_dt_verdef;
           cur != prev && cur->vd_ndx != ver_index;
           prev = cur, cur =
           (ElfW (Verdef) *) (((unsigned long) cur) + cur->vd_next)) { }
      VDL_LOG_ASSERT (cur->vd_version == 1,
                      "version number invalid for Verdef");
      if (cur->vd_hash == from_ver_hash)
        {
          // the hash values of the version names are equal.
          ElfW (Verdaux) * verdaux =
            (ElfW (Verdaux) *) (((unsigned long) cur) + cur->vd_aux);
          if (vdl_utils_strisequal
              (in_dt_strtab + verdaux->vda_name, from_ver_name))
            {
              // the version names are equal.
              return VERSION_MATCH_PERFECT;
            }
        }
      if (cur->vd_hash == 0)
	{
	  return VERSION_MATCH_PERFECT;
	}
    }
  // the versions don't match.
  return VERSION_MATCH_BAD;
}

void
vdl_lookup_symbol_fixup (const struct VdlFile *file, ElfW (Sym) * sym)
{
  if (ELFW_ST_TYPE (sym->st_info) == STT_GNU_IFUNC)
    {
      // We must call the symbol to get the symbol value.
      // This is a glibc extension which appeared in fc12 for
      // the first time. It is used to delegate at runtime
      // the decision of which function to run. Typically, it is
      // used to detect automatically the hardware type and
      // use optimized versions of specified functions such
      // as strlen, etc.
      unsigned long (*ifunc) (void) = (unsigned long (*)(void))
        (sym->st_value + file->load_base);
      sym->st_value = ifunc ();
      // we need to remove the load base such that the relocation
      // code which adds the load_base again generates a valid
      // address
      sym->st_value -= file->load_base;
    }
}

static struct VdlLookupResult
vdl_lookup_with_scope_internal (struct VdlFile *file,
                                const char *name,
                                const char *ver_name,
                                const char *ver_filename,
                                unsigned long elf_hash,
                                uint32_t gnu_hash,
                                unsigned long ver_hash,
                                enum VdlLookupFlag flags,
                                struct VdlList *scope)
{
  VDL_LOG_FUNCTION
    ("name=%s, ver_name=%s, ver_filename=%s, elf_hash=0x%lx, gnu_hash=0x%x, "
     "ver_hash=0x%x, flags=0x%x, scope=%p", name,
     (ver_name != 0) ? ver_name : "", (ver_filename != 0) ? ver_filename : "",
     elf_hash, gnu_hash, ver_hash, flags, scope);

  // then, iterate scope until we find the requested symbol.
  void **cur;
  for (cur = vdl_list_begin (scope); cur != vdl_list_end (scope);
       cur = vdl_list_next (cur))
    {
      struct VdlFile *item = *cur;
      if (flags & VDL_LOOKUP_NO_EXEC && item->is_executable)
        {
          // this flag specifies that we should not lookup symbols
          // in the main executable binary. see the definition of VDL_LOOKUP_NO_EXEC
          continue;
        }
      int n_ambiguous_matches = 0;
      unsigned long last_ambiguous_match = 0, first_ambiguous_match = 0;
      struct VdlFile *first_ambiguous_match_item = 0;
      struct VdlFileLookupIterator i =
        vdl_lookup_file_begin (item, name, elf_hash, gnu_hash);
      while (vdl_lookup_file_has_next (&i))
        {
          unsigned long index = vdl_lookup_file_next (&i);
          enum VdlVersionMatch version_match =
            symbol_version_matches (item, file,
                                    ver_name, ver_filename, ver_hash,
                                    index);
          if (version_match == VERSION_MATCH_PERFECT)
            {
              // We have resolved the symbol
              if (item != file && file != 0)
                {
                  // The symbol has been resolved in another binary. Make note of this.
                  vdl_list_push_front (file->gc_symbols_resolved_in, item);
                }
              struct VdlLookupResult result;
              result.file = item;
              result.symbol = i.dt_symtab[index];
              result.found = true;
              return result;
            }
          else if (version_match == VERSION_MATCH_AMBIGUOUS)
            {
              if (n_ambiguous_matches == 0)
                {
                  first_ambiguous_match = index;
                  first_ambiguous_match_item = item;
                }
              n_ambiguous_matches++;
              last_ambiguous_match = index;
            }
        }

      unsigned long final_match;
      struct VdlFile *final_item;
      if (n_ambiguous_matches == 1)
        {
          // if there is only one ambiguous match, it's not really ambiguous: it's a match !
          final_match = last_ambiguous_match;
          final_item = item;
        }
      else if (n_ambiguous_matches > 1)
        {
          // If we have multiple ambiguous matches, it means that we are doing
          // a lookup for a symbol that has no version information and we found
          // more than one version of this symbol within the current file.
          // In this case, we pick the 'oldest' symbol, that is, the first one
          // we found. This is what I believe glibc is doing.
          final_match = first_ambiguous_match;
          final_item = first_ambiguous_match_item;
        }
      else
        {
          // no match so keep looking
          continue;
        }
      if (final_item != file && file != 0)
        {
          // The symbol has been resolved in another binary. Make note of this.
          vdl_list_push_front (file->gc_symbols_resolved_in, final_item);
        }
      struct VdlLookupResult result;
      result.file = final_item;
      result.symbol = i.dt_symtab[final_match];
      result.found = true;
      return result;
    }
  struct VdlLookupResult result;
  result.found = false;
  return result;
}

struct VdlLookupResult
vdl_lookup (struct VdlFile *file,
            const char *name,
            const char *ver_name,
            const char *ver_filename, enum VdlLookupFlag flags)
{
  if (!(flags & VDL_LOOKUP_NO_REMAP))
    {
      vdl_context_symbol_remap (file->context, &name, &ver_name,
                                &ver_filename);
    }
  // calculate the hash here to avoid calculating
  // it twice in both calls to symbol_lookup
  unsigned long elf_hash = vdl_elf_hash (name);
  uint32_t gnu_hash = vdl_gnu_hash (name);
  unsigned long ver_hash = 0;
  if (ver_name != 0)
    {
      ver_hash = vdl_elf_hash (ver_name);
    }

  struct VdlList *first = 0;
  struct VdlList *second = 0;
  switch (file->lookup_type)
    {
    case FILE_LOOKUP_LOCAL_GLOBAL:
      first = file->local_scope;
      second = file->context->global_scope;
      break;
    case FILE_LOOKUP_GLOBAL_LOCAL:
      first = file->context->global_scope;
      second = file->local_scope;
      break;
    case FILE_LOOKUP_GLOBAL_ONLY:
      first = file->context->global_scope;
      second = 0;
      break;
    case FILE_LOOKUP_LOCAL_ONLY:
      first = file->local_scope;
      second = 0;
      break;
    }
  struct VdlLookupResult result;
  result = vdl_lookup_with_scope_internal (file, name, ver_name, ver_filename,
                                           elf_hash, gnu_hash, ver_hash,
                                           flags, first);
  if (!result.found)
    {
      result =
        vdl_lookup_with_scope_internal (file, name, ver_name, ver_filename,
                                        elf_hash, gnu_hash, ver_hash, flags,
                                        second);
    }
  return result;
}

struct VdlLookupResult
vdl_lookup_local (const struct VdlFile *file, const char *name)
{
  vdl_context_symbol_remap (file->context, &name, 0, 0);
  unsigned long elf_hash = vdl_elf_hash (name);
  uint32_t gnu_hash = vdl_gnu_hash (name);
  struct VdlFileLookupIterator i =
    vdl_lookup_file_begin (file, name, elf_hash, gnu_hash);
  struct VdlLookupResult result;
  result.file = file;
  if (vdl_lookup_file_has_next (&i))
    {
      unsigned long index = vdl_lookup_file_next (&i);
      result.symbol = i.dt_symtab[index];
      result.found = true;
      vdl_lookup_symbol_fixup (result.file, &result.symbol);
    }
  else
    {
      result.found = false;
    }
  return result;
}


struct VdlLookupResult
vdl_lookup_with_scope (const struct VdlContext *from_context,
                       const char *name,
                       const char *ver_name,
                       const char *ver_filename,
                       enum VdlLookupFlag flags, struct VdlList *scope)
{
  if (!(flags & VDL_LOOKUP_NO_REMAP))
    {
      vdl_context_symbol_remap (from_context, &name, &ver_name,
                                &ver_filename);
    }
  unsigned long elf_hash = vdl_elf_hash (name);
  uint32_t gnu_hash = vdl_gnu_hash (name);
  unsigned long ver_hash = 0;
  if (ver_name != 0)
    {
      ver_hash = vdl_elf_hash (ver_name);
    }
  struct VdlLookupResult result;
  result = vdl_lookup_with_scope_internal (0, name, ver_name, ver_filename,
                                           elf_hash, gnu_hash, ver_hash,
                                           flags, scope);
  return result;
}
