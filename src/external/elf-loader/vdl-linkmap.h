#ifndef VDL_LINKMAP_H
#define VDL_LINKMAP_H

struct VdlFile;
struct VdlList;

void vdl_linkmap_append (struct VdlFile *file);
void vdl_linkmap_append_range (struct VdlList *list, void **begin, void **end);
void vdl_linkmap_remove_range (struct VdlList *list, void **begin, void **end);
struct VdlList *vdl_linkmap_copy (void);
void vdl_linkmap_print (void);

#endif /* VDL_LINKMAP_H */
