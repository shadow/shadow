#ifndef VDL_DL_H
#define VDL_DL_H

#include <dlfcn.h> // for Dl_info
#include <link.h> // for struct dl_phdr_info

// new info type we've added
#define RTLD_DI_TLS_SIZE 127

// the 'private' version is called from ldso itself
// to avoid the pain of calling these functions through
// a PLT indirection which would require ldso to be able
// to relocate its own JMP_SLOT entries which would be a
// bit painful to do.
void *vdl_dlopen (const char *filename, int flag);
char *vdl_dlerror (void);
void *vdl_dlsym (void *handle, const char *symbol, unsigned long caller);
int vdl_dlclose (void *handle);
int vdl_dladdr (const void *addr, Dl_info *info);
int vdl_dladdr1 (const void *addr, Dl_info *info, 
		 void **extra_info, int flags);
void *vdl_dlvsym (void *handle, const char *symbol, const char *version, unsigned long caller);
void *vdl_dlvsym_with_flags (void *handle, const char *symbol, const char *version, 
			     unsigned long flags,
			     unsigned long caller);
int vdl_dlinfo (void *handle, int request, void *p);
void *vdl_dlmopen (Lmid_t lmid, const char *filename, int flag);
// create a new linkmap
Lmid_t vdl_dl_lmid_new (int argc, char **argv, char **envp);
void vdl_dl_lmid_delete (Lmid_t lmid);
int vdl_dl_lmid_add_callback (Lmid_t lmid, 
			      void (*cb) (void *handle, int event, void *context),
			      void *cb_context);
int vdl_dl_lmid_add_lib_remap (Lmid_t lmid, const char *src, const char *dst);
int vdl_dl_lmid_add_symbol_remap (Lmid_t lmid,
				  const char *src_name, 
				  const char *src_ver_name, 
				  const char *src_ver_filename, 
				  const char *dst_name,
				  const char *dst_ver_name,
				  const char *dst_ver_filename);

// This function is special: it is not called from ldso: it is
// used by vdl itself as the target of a redirection from every call to 
// dl_iterate_phdr
int vdl_dl_iterate_phdr (int (*callback) (struct dl_phdr_info *info,
					  size_t size, void *data),
			 void *data,
			 unsigned long caller);

#endif /* VDL_DL_H */
