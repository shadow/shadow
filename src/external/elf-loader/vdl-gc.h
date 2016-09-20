#ifndef VDL_GC_H
#define VDL_GC_H

struct VdlList;

/* Perform a mark and sweep garbage tri-colour collection 
 * of all VdlFile objects and returns the list of objects 
 * which can be freed. These objects are already
 * removed from all global lists so, it should be safe
 * to just delete them here
 */
struct VdlGcResult
{
  struct VdlList *unload;
  struct VdlList *not_unload;
};
struct VdlGcResult vdl_gc_run (void);


#endif /* VDL_GC_H */
