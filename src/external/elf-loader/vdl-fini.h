#ifndef VDL_FINI_H
#define VDL_FINI_H

struct VdlList;

struct VdlList *vdl_fini_lock (struct VdlList *files);
void vdl_fini_call (struct VdlList *files);

#endif /* VDL_FINI_H */
