#ifndef VDL_LOOKUP_H
#define VDL_LOOKUP_H

#include <elf.h>
#include <link.h>
#include <stdbool.h>

struct VdlContext;
struct VdlFile;
struct VdlList;

struct VdlLookupResult
{
  bool found;
  const struct VdlFile *file;
  const ElfW(Sym) *symbol;
};
enum VdlLookupFlag {
  // indicates whether the symbol lookup is allowed to 
  // find a matching symbol in the main binary. This is
  // typically used to perform the lookup associated
  // with a R_*_COPY relocation.
  VDL_LOOKUP_NO_EXEC = 1,
  // indicates that no symbol remap should be performed
  // This can be used to get the original symbol back.
  VDL_LOOKUP_NO_REMAP = 2
};
struct VdlLookupResult vdl_lookup (struct VdlFile *from_file,
				   const char *name, 
				   const char *ver_name,
				   const char *ver_filename,
				   enum VdlLookupFlag flags);
struct VdlLookupResult vdl_lookup_local (const struct VdlFile *file, 
					 const char *name);
struct VdlLookupResult vdl_lookup_with_scope (const struct VdlContext *from_context,
					      const char *name, 
					      const char *ver_name,
					      const char *ver_filename,
					      enum VdlLookupFlag flags,
					      struct VdlList *scope);

#endif /* VDL_LOOKUP_H */
