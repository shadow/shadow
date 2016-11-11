#ifndef VDL_MAP_H
#define VDL_MAP_H

#include <stdint.h>
#include <elf.h>
#include <link.h>

struct VdlContext;

struct VdlMapResult
{
  // should be non-null if success, null otherwise.
  struct VdlFile *requested;
  // the list of files which were brought into memory
  // by this map request. allocated by callee. caller must free.
  struct VdlList *newly_mapped;
  // if the mapping fails, a human-readable string
  // which indicates what went wrong.
  // allocated by callee, caller must free.
  char *error_string;
};


struct VdlMapResult vdl_map_from_memory (unsigned long load_base,
					 uint32_t phnum,
					 ElfW(Phdr) *phdr,
					 // a fully-qualified path to the file
					 // represented by the phdr
					 const char *path, 
					 // a non-fully-qualified filename
					 const char *filename,
					 struct VdlContext *context);
struct VdlMapResult vdl_map_from_filename (struct VdlContext *context, 
					   const char *filename);
struct VdlList *vdl_map_from_preload (struct VdlContext *context,
				      struct VdlList *filenames);


#endif /* VDL_MAP_H */
