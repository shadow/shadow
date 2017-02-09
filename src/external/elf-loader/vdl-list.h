#ifndef VDL_LIST_H
#define VDL_LIST_H

#include "futex.h"
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * This API is based on the std::list API. The name and
 * semantics of all functions is based on the std::list version.
 * Because this is C code, we use void ** for the iterator
 * type and void * for the value type.
 */

struct VdlListItem
{
  void *data;
  struct VdlListItem *next;
  struct VdlListItem *prev;
};

struct VdlList
{
  struct VdlListItem head;
  struct VdlListItem tail;
  uint32_t size;
  struct RWLock *lock;
};
struct VdlListItem;

struct VdlList *vdl_list_new (void);
struct VdlList *vdl_list_copy (struct VdlList *list);
void vdl_list_delete (struct VdlList *list);
void vdl_list_construct (struct VdlList *list);
void vdl_list_destruct (struct VdlList *list);
uint32_t vdl_list_size (struct VdlList *list);
bool vdl_list_empty (struct VdlList *list);

void **vdl_list_begin (struct VdlList *list);
void **vdl_list_end (struct VdlList *list);
void **vdl_list_next (struct VdlList *list, void **i);
void **vdl_list_prev (struct VdlList *list, void **i);

void **vdl_list_insert (struct VdlList *list, void **at, void *value);
void vdl_list_insert_range (struct VdlList *to, void **at,
                            struct VdlList *from, void **start, void **end);
void vdl_list_push_back (struct VdlList *list, void *data);
void vdl_list_push_front (struct VdlList *list, void *data);
void vdl_list_pop_back (struct VdlList *list);
void vdl_list_pop_front (struct VdlList *list);
void *vdl_list_front (struct VdlList *list);
void *vdl_list_back (struct VdlList *list);
void **vdl_list_find (struct VdlList *list, void *data);
void **vdl_list_find_from (struct VdlList *list, void **from, void *data);
void vdl_list_clear (struct VdlList *list);
void **vdl_list_erase (struct VdlList *list, void **i);
void **vdl_list_erase_range (struct VdlList *list,
                             void **start,
                             void **end);
void vdl_list_remove (struct VdlList *list, void *data);
void vdl_list_reverse (struct VdlList *list);
void vdl_list_sort (struct VdlList *list,
                    // true if a < b, false otherwise
                    bool (*is_strictly_lower) (void *a, void *b, void *context),
                    void *context);
void vdl_list_unique (struct VdlList *list);

// Contrary to the std::list::unique method, this function
// does not remove adjacent equal values. It removes equal
// values in the whole list.
void vdl_list_unicize (struct VdlList *list);

// Note: reverse iterators work _only_ with
// these 4 functions. Feeding a reverse iterator
// to any other function will result in strange
// output. It will seem to work most of the time
// but it will not really work correctly.
void **vdl_list_rbegin (struct VdlList *list);
void **vdl_list_rend (struct VdlList *list);
void **vdl_list_rnext (struct VdlList *list, void **i);
void **vdl_list_rprev (struct VdlList *list, void **i);

void vdl_list_iterate (struct VdlList *list,
                       void (*iterator) (void *data));


#ifdef __cplusplus
}
#endif

#endif /* VDL_LIST_H */
