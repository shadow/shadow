#ifndef VDL_LINKMAP_H
#define VDL_LINKMAP_H

struct VdlFile;
struct VdlList;

void vdl_linkmap_append (struct VdlFile *file);
void vdl_linkmap_append_list (struct VdlList *list);
void vdl_linkmap_remove (struct VdlFile *file);
void vdl_linkmap_remove_list (struct VdlList *list);
struct VdlList *vdl_linkmap_copy (void);
void vdl_linkmap_abi_update (void);

#endif /* VDL_LINKMAP_H */
