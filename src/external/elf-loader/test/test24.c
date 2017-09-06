#include <dlfcn.h>
#include <pthread.h>
#include <stdio.h>
#include <semaphore.h>
#include "test.h"
LIB(test24)

static void *thread (__attribute__((unused)) void*ctx)
{
  void *handle1 = dlopen ("libstdc++.so.6", RTLD_LAZY | RTLD_DEEPBIND | RTLD_LOCAL);
  /* It seems loading libstdc++.so is easier reproduce than loading libc
   * on Ubuntu 10.04, x86_64. 
   */
  //  void *handle1 = dlopen ("libc.so.6", RTLD_LAZY | RTLD_DEEPBIND | RTLD_LOCAL);
  dlclose (handle1);
  return 0;
}

/* 
 * typical crash is:
 * #0  0x00007f79ae584972 in alloc_do_malloc (alloc=0x7f79ae798180, size=32)
 *    at /home/tazaki/hgworks/ns-3-dce-thehajime/elf-loader-outgoing/alloc.c:104
 * 104               struct AllocAvailable *next = avail->next;
 * (gdb) bt
 * #0  0x00007f79ae584972 in alloc_do_malloc (alloc=0x7f79ae798180, size=32)
 *     at /home/tazaki/hgworks/ns-3-dce-thehajime/elf-loader-outgoing/alloc.c:104
 * #1  0x00007f79ae584f6b in alloc_malloc (alloc=0x7f79ae798180, size=24)
 *     at /home/tazaki/hgworks/ns-3-dce-thehajime/elf-loader-outgoing/alloc.c:185
 * #2  0x00007f79ae58b1aa in vdl_alloc_malloc (size=24)
 *     at /home/tazaki/hgworks/ns-3-dce-thehajime/elf-loader-outgoing/vdl-alloc.c:17
 * #3  0x00007f79ae58a080 in vdl_list_insert (list=0x7f79ae78e7d8, at=0x7f79ae78e7f0, 
 *     value=0x7f79ae78e2e8)
 *     at /home/tazaki/hgworks/ns-3-dce-thehajime/elf-loader-outgoing/vdl-list.c:91
 * #4  0x00007f79ae58a19c in vdl_list_push_front (list=0x7f79ae78e7d8, data=0x7f79ae78e2e8)
 *     at /home/tazaki/hgworks/ns-3-dce-thehajime/elf-loader-outgoing/vdl-list.c:115
 * #5  0x00007f79ae58609c in vdl_gc_white_list_new (list=0x7f79ae78e858)
 *     at /home/tazaki/hgworks/ns-3-dce-thehajime/elf-loader-outgoing/vdl-gc.c:31
 * #6  0x00007f79ae5862d3 in vdl_gc_run ()
 *     at /home/tazaki/hgworks/ns-3-dce-thehajime/elf-loader-outgoing/vdl-gc.c:99
 * #7  0x00007f79ae59046d in vdl_dlclose (handle=0x7f79ae7919e8)
 *     at /home/tazaki/hgworks/ns-3-dce-thehajime/elf-loader-outgoing/vdl-dl.c:342
 * #8  0x00007f79ae5914fa in vdl_dlclose_public (handle=0x7f79ae7919e8)
 *     at /home/tazaki/hgworks/ns-3-dce-thehajime/elf-loader-outgoing/vdl-dl-public.c:21
 * #9  0x00007f79ae161c49 in dlclose (handle=0x7f79ae7919e8)
 *     at /home/tazaki/hgworks/ns-3-dce-thehajime/elf-loader-outgoing/libvdl.c:30
 * #10 0x000000000040086c in thread (ctx=0x0) at test24.c:13
 * #11 0x00007f79ae36a9ca in start_thread (arg=<value optimized out>) at pthread_create.c:300
 * #12 0x00007f79adec421d in clone () at ../sysdeps/unix/sysv/linux/x86_64/clone.S:112
 * #13 0x0000000000000000 in ?? ()
 */
int main (__attribute__((unused)) int argc,
	  __attribute__((unused)) char *argv[])
{
  printf ("enter main\n");

  pthread_attr_t attr;
  pthread_attr_init (&attr);
  pthread_t th;
  pthread_create (&th, &attr, thread, 0);
  pthread_create (&th, &attr, thread, 0);
  pthread_create (&th, &attr, thread, 0);

  thread (NULL);
  printf ("leave main\n");
  return 0;
}
