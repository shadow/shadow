#ifndef VDL_CONTEXT_H
#define VDL_CONTEXT_H

#include <stdint.h>
#include <stdbool.h>

struct VdlList;
struct VdlFile;

struct VdlContextSymbolRemapEntry
{
  char *src_name;
  char *src_ver_name;
  char *src_ver_filename;
  char *dst_name;
  char *dst_ver_name;
  char *dst_ver_filename;
};
struct VdlContextLibRemapEntry
{
  char *src;
  char *dst;
};

enum VdlEvent
{
  VDL_EVENT_MAPPED,
  VDL_EVENT_UNMAPPED,
  VDL_EVENT_CONSTRUCTED,
  VDL_EVENT_DESTROYED
};
struct VdlContextEventCallbackEntry
{
  void (*fn) (void *handle, enum VdlEvent event, void *context);
  void *context;
};

struct VdlContext
{
  // the list of files loaded in this context
  struct VdlList *loaded;
  // whether this file has a main object in the global scope
  uint32_t has_main:1;
  // the list of files which are part of the global scope of this context
  // this set is necessarily a subset of the set of loaded files
  struct VdlList *global_scope;
  // describe which symbols should be remapped to which 
  // other symbols during symbol resolution
  struct VdlList *symbol_remaps;
  // describe which libraries should be remapped to which 
  // other libraries during loading
  struct VdlList *lib_remaps;
  // report events within this context
  struct VdlList *event_callbacks;
  // These variables are used by all .init functions
  // _some_ libc .init functions make use of these
  // 3 arguments so, even though no one else uses them, 
  // we have to pass them around.
  int argc;
  char **argv;
  char **envp;
};

struct VdlContext *vdl_context_new (int argc, char **argv, char **envp);
void vdl_context_delete (struct VdlContext *context);
void vdl_context_add_file (struct VdlContext *context, struct VdlFile *file);
void vdl_context_remove_file (struct VdlContext *context,
                              struct VdlFile *file);
void vdl_context_add_lib_remap (struct VdlContext *context, const char *src,
                                const char *dst);
void vdl_context_add_symbol_remap (struct VdlContext *context,
                                   const char *src_name,
                                   const char *src_ver_name,
                                   const char *src_ver_filename,
                                   const char *dst_name,
                                   const char *dst_ver_name,
                                   const char *dst_ver_filename);
void vdl_context_add_callback (struct VdlContext *context,
                               void (*cb) (void *handle, enum VdlEvent event,
                                           void *context), void *cb_context);
void vdl_context_notify (struct VdlContext *context, struct VdlFile *file,
                         enum VdlEvent event);
const char *vdl_context_lib_remap (const struct VdlContext *context,
                                   const char *name);
void vdl_context_symbol_remap (const struct VdlContext *context,
                               const char **name, const char **ver_name,
                               const char **ver_filename);

bool vdl_context_empty (const struct VdlContext *context);

#endif /* VDL_CONTEXT_H */
