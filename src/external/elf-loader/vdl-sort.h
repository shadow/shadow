#ifndef VDL_SORT_H
#define VDL_SORT_H

struct VdlList;
struct VdlFile;

struct VdlList *vdl_sort_increasing_depth (struct VdlList *files);
struct VdlList *vdl_sort_deps_breadth_first (struct VdlFile *file);
struct VdlList *vdl_sort_call_init (struct VdlList *files);
struct VdlList *vdl_sort_call_fini (struct VdlList *files);

#endif /* VDL_SORT_H */
