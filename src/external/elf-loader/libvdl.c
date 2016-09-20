#include "macros.h"
#include "vdl-dl-public.h"

/* We provide these wrappers to be able to export a libvdl.so
 * with the _exact_ version definitions matching the system's
 * libdl.so. Yes, we could do the same thing directly in our
 * ldso but doing this would require that we build the ldso
 * version definition file from a merge of both /lib/ld-linux.so.2 
 * and /lib/libdl.so.2 which is more complicated than writing
 * these trivial wrappers.
 */

EXPORT void *dlopen(const char *filename, int flag)
{
  return vdl_dlopen_public (filename, flag);
}

EXPORT char *dlerror(void)
{
  return vdl_dlerror_public ();
}

EXPORT void *dlsym(void *handle, const char *symbol)
{
  return vdl_dlsym_public (handle, symbol, RETURN_ADDRESS);
}

EXPORT int dlclose(void *handle)
{
  return vdl_dlclose_public (handle);
}
EXPORT int dladdr (const void *addr, Dl_info *info)
{
  return vdl_dladdr_public (addr, info);
}
EXPORT void *dlvsym (void *handle, const char *symbol, const char *version)
{
  return vdl_dlvsym_public (handle, symbol, version, RETURN_ADDRESS);
}
EXPORT int dlinfo (void *handle, int request, void *p)
{
  return vdl_dlinfo_public (handle, request, p);
}
EXPORT void *dlmopen (Lmid_t lmid, const char *filename, int flag)
{
  return vdl_dlmopen_public(lmid, filename, flag);
}
EXPORT Lmid_t dl_lmid_new (int argc, char **argv, char **envp)
{
  return vdl_dl_lmid_new_public (argc, argv, envp);
}
EXPORT void dl_lmid_delete (Lmid_t lmid)
{
  return vdl_dl_lmid_delete_public (lmid);
}
EXPORT int dl_lmid_add_callback (Lmid_t lmid, 
				 void (*cb) (void *handle, int event, void *context),
				 void *cb_context)
{
  return vdl_dl_lmid_add_callback_public (lmid, cb, cb_context);
}
EXPORT int dl_lmid_add_lib_remap (Lmid_t lmid, const char *src, const char *dst)
{
  return vdl_dl_lmid_add_lib_remap_public (lmid, src, dst);
}
EXPORT int dl_lmid_add_symbol_remap (Lmid_t lmid,
				     const char *src_name, 
				     const char *src_ver_name, 
				     const char *src_ver_filename, 
				     const char *dst_name,
				     const char *dst_ver_name,
				     const char *dst_ver_filename)
{
  return vdl_dl_lmid_add_symbol_remap_public (lmid, 
					      src_name, src_ver_name, src_ver_filename,
					      dst_name, dst_ver_name, dst_ver_filename);
}

