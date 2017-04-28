#include "vdl-dl-public.h"
#include "vdl-dl.h"

EXPORT void *
vdl_dlopen_public (const char *filename, int flag)
{
  return vdl_dlopen (filename, flag);
}

EXPORT char *
vdl_dlerror_public (void)
{
  return vdl_dlerror ();
}

EXPORT void *
vdl_dlsym_public (void *handle, const char *symbol, unsigned long caller)
{
  return vdl_dlsym (handle, symbol, caller);
}

EXPORT int
vdl_dlclose_public (void *handle)
{
  return vdl_dlclose (handle);
}

EXPORT int
vdl_dladdr_public (const void *addr, Dl_info * info)
{
  return vdl_dladdr (addr, info);
}

EXPORT void *
vdl_dlvsym_public (void *handle, const char *symbol, const char *version,
                   unsigned long caller)
{
  return vdl_dlvsym (handle, symbol, version, caller);
}

EXPORT int
vdl_dlinfo_public (void *handle, int request, void *p)
{
  return vdl_dlinfo (handle, request, p);
}

EXPORT void *
vdl_dlmopen_public (Lmid_t lmid, const char *filename, int flag)
{
  return vdl_dlmopen (lmid, filename, flag);
}

EXPORT Lmid_t
vdl_dl_lmid_new_public (int argc, char **argv, char **envp)
{
  return vdl_dl_lmid_new (argc, argv, envp);
}

EXPORT void
vdl_dl_lmid_delete_public (Lmid_t lmid)
{
  return vdl_dl_lmid_delete (lmid);
}

EXPORT int
vdl_dl_lmid_add_callback_public (Lmid_t lmid,
                                 void (*cb) (void *handle, int event,
                                             void *context), void *cb_context)
{
  return vdl_dl_lmid_add_callback (lmid, cb, cb_context);
}

EXPORT int
vdl_dl_lmid_add_lib_remap_public (Lmid_t lmid, const char *src,
                                  const char *dst)
{
  return vdl_dl_lmid_add_lib_remap (lmid, src, dst);
}

EXPORT int
vdl_dl_lmid_add_symbol_remap_public (Lmid_t lmid,
                                     const char *src_name,
                                     const char *src_ver_name,
                                     const char *src_ver_filename,
                                     const char *dst_name,
                                     const char *dst_ver_name,
                                     const char *dst_ver_filename)
{
  return vdl_dl_lmid_add_symbol_remap (lmid, src_name, src_ver_name,
                                       src_ver_filename, dst_name,
                                       dst_ver_name, dst_ver_filename);
}

EXPORT int
vdl_dl_lmid_swap_tls_public (Lmid_t lmid, pthread_t *t1, pthread_t *t2)
{
  return vdl_dl_lmid_swap_tls (lmid, t1, t2);
}


EXPORT int
vdl_dl_iterate_phdr_public (int (*callback) (struct dl_phdr_info * info,
                                             size_t size, void *data),
                            void *data)
{
  return vdl_dl_iterate_phdr (callback, data, RETURN_ADDRESS);
}
