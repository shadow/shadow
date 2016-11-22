#ifndef VDL_RELOC_H
#define VDL_RELOC_H

struct VdlList;
struct VdlFile;

void vdl_reloc (struct VdlList *list, int now);
// offset is in bytes, return value is reloced symbol
// called from machine_resolve_trampoline 
unsigned long vdl_reloc_offset_jmprel (struct VdlFile *file,
                                       unsigned long offset);
// index is an index in the ElfW(Rel/Rela) array
unsigned long vdl_reloc_index_jmprel (struct VdlFile *file,
                                      unsigned long index);

#endif /* VDL_RELOC_H */
