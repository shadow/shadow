#include "vdl-list.h"
#include "vdl-alloc.h"

struct VdlList *
vdl_list_new (void)
{
  struct VdlList *list = vdl_alloc_new (struct VdlList);
  vdl_list_construct (list);
  return list;
}

struct VdlList *
vdl_list_copy (struct VdlList *list)
{
  struct VdlList *copy = vdl_list_new ();
  vdl_list_append_list (copy, list);
  return copy;
}

void
vdl_list_delete (struct VdlList *list)
{
  vdl_list_destruct (list);
  vdl_alloc_delete (list);
}

void
vdl_list_construct (struct VdlList *list)
{
  list->lock = rwlock_new();
  list->size = 0;
  list->head.data = 0;
  list->head.next = &list->tail;
  list->head.prev = 0;
  list->tail.data = 0;
  list->tail.next = 0;
  list->tail.prev = &list->head;
}

void
vdl_list_destruct (struct VdlList *list)
{
  vdl_list_clear (list);
  rwlock_delete (list->lock);
}

uint32_t
vdl_list_size (struct VdlList *list)
{
  read_lock (list->lock);
  uint32_t size = list->size;
  read_unlock (list->lock);
  return size;
}

bool
vdl_list_empty (struct VdlList * list)
{
  read_lock (list->lock);
  bool empty = (list->size == 0);
  read_unlock (list->lock);
  return empty;
}

void **
vdl_list_begin (struct VdlList *list)
{
  read_lock (list->lock);
  void **begin = (void **) list->head.next;
  read_unlock (list->lock);
  return begin;
}

void **
vdl_list_end (struct VdlList *list)
{
  // no lock needed, this is a constant offset into the struct
  void **end = (void **) &list->tail;
  return end;
}

void **
vdl_list_next (struct VdlList *list, void **i)
{
  struct VdlListItem *item = (struct VdlListItem *) i;
  read_lock (list->lock);
  void **next = (void **) item->next;
  read_unlock (list->lock);
  return next;
}

void **
vdl_list_prev (struct VdlList *list, void **i)
{
  struct VdlListItem *item = (struct VdlListItem *) i;
  read_lock (list->lock);
  void **prev = (void **) item->prev;
  read_unlock (list->lock);
  return prev;
}

void **
vdl_list_rbegin (struct VdlList *list)
{
  read_lock (list->lock);
  void **rbegin = (void **) list->tail.prev;
  read_unlock (list->lock);
  return rbegin;
}

void **
vdl_list_rend (struct VdlList *list)
{
  read_lock (list->lock);
  void **rend = (void **) &list->head;
  read_unlock (list->lock);
  return rend;
}

/* The compiler aliases these regardless, we just annotate to help it
   pick the more commonly used symbol names.
*/
void **
vdl_list_rnext (struct VdlList *list, void **i)
  __attribute__ ((weak, alias ("vdl_list_prev")));

void **
vdl_list_rprev (struct VdlList *list, void **i)
  __attribute__ ((weak, alias ("vdl_list_next")));

static void **
vdl_list_insert_internal (struct VdlList *list, void **at, void *value)
{
  struct VdlListItem *after = (struct VdlListItem *) at;
  struct VdlListItem *item = vdl_alloc_new (struct VdlListItem);
  item->data = value;
  item->next = after;
  item->prev = after->prev;
  after->prev = item;
  item->prev->next = item;
  list->size++;
  return (void **) item;
}

void **
vdl_list_insert (struct VdlList *list, void **at, void *value)
{
  write_lock (list->lock);
  void **ret = vdl_list_insert_internal (list, at, value);
  write_unlock (list->lock);
  return ret;
}

void
vdl_list_insert_range (struct VdlList *to, void **at,
                       struct VdlList *from, void **start, void **end)
{
  write_lock (to->lock);
  if (to != from)
    {
      read_lock (from->lock);
    }
  struct VdlListItem *i;
  for (i = (struct VdlListItem *)start; i != (struct VdlListItem *)end; i = i->next)
    {
      vdl_list_insert_internal (to, at, i->data);
    }
  if (to != from)
    {
      read_unlock (from->lock);
    }
  write_unlock (to->lock);
}

void
vdl_list_append_list (struct VdlList *a, struct VdlList *b)
{
  write_lock (a->lock);
  if (a != b)
    {
      read_lock (b->lock);
    }
  struct VdlListItem *i;
  for (i = b->head.next; i != &b->tail; i = i->next)
    {
      vdl_list_insert_internal (a, (void **) &a->tail, i->data);
    }
  if (a != b)
    {
      read_unlock (b->lock);
    }
  write_unlock (a->lock);
}

static void **
vdl_list_erase_internal (struct VdlList *list, void **i)
{
  // Note: it's a programming error to call _erase if i = _end () or i = _rend ()
  // this will trigger a crash in vdl_alloc_delete (i)
  list->size--;
  struct VdlListItem *item = (struct VdlListItem *) i;
  item->prev->next = item->next;
  item->next->prev = item->prev;
  struct VdlListItem *next = item->next;
  item->next = 0;
  item->prev = 0;
  item->data = 0;
  vdl_alloc_delete (item);
  return (void **) next;
}

void **
vdl_list_erase (struct VdlList *list, void **i)
{
  // Note: it's a programming error to call _erase if i = _end () or i = _rend ()
  // this will trigger a crash in vdl_alloc_delete (i)
  write_lock (list->lock);
  void **ret = vdl_list_erase_internal (list, i);
  write_unlock (list->lock);
  return ret;
}

static void **
vdl_list_erase_range_internal (struct VdlList *list, void **s, void **e)
{
  // Note: it's a programming error to call _erase if s = _end () or s = _rend ()
  // this will trigger a crash in vdl_alloc_delete (s)
  struct VdlListItem *start = (struct VdlListItem *) s;
  struct VdlListItem *end = (struct VdlListItem *) e;
  start->prev->next = end;
  end->prev = start->prev;

  // now, delete the intermediate items
  struct VdlListItem *item, *next;
  uint32_t deleted = 0;
  for (item = start; item != end; item = next)
    {
      next = item->next;
      deleted++;
      vdl_alloc_delete (item);
    }
  list->size -= deleted;
  return (void **) end;
}

void **
vdl_list_erase_range (struct VdlList *list, void **s, void **e)
{
  // Note: it's a programming error to call _erase if s = _end () or s = _rend ()
  // this will trigger a crash in vdl_alloc_delete (s)
  write_lock (list->lock);
  void **ret = vdl_list_erase_range_internal (list, s, e);
  write_unlock (list->lock);
  return ret;
}

void
vdl_list_clear (struct VdlList *list)
{
  write_lock (list->lock);
  vdl_list_erase_range_internal (list, (void **) list->head.next, (void **) &list->tail);
  write_unlock (list->lock);
}

void
vdl_list_push_back (struct VdlList *list, void *data)
{
  write_lock (list->lock);
  vdl_list_insert_internal (list, (void **) &list->tail, data);
  write_unlock (list->lock);
}

void
vdl_list_global_push_back (struct VdlList *list, void *data)
{
  write_lock (list->lock);
  struct VdlListItem *after = &list->tail;
  struct VdlListItem *item = vdl_alloc_global (sizeof(struct VdlListItem));
  item->data = data;
  item->next = after;
  item->prev = after->prev;
  after->prev = item;
  item->prev->next = item;
  list->size++;
  write_unlock (list->lock);
}

void
vdl_list_push_front (struct VdlList *list, void *data)
{
  write_lock (list->lock);
  vdl_list_insert_internal (list, (void **) list->head.next, data);
  write_unlock (list->lock);
}

void
vdl_list_pop_back (struct VdlList *list)
{
  write_lock (list->lock);
  vdl_list_erase_internal (list, (void **) list->tail.prev);
  write_unlock (list->lock);
}

void
vdl_list_pop_front (struct VdlList *list)
{
  write_lock (list->lock);
  vdl_list_erase_internal (list, (void **) list->head.next);
  write_unlock (list->lock);
}

void *
vdl_list_front (struct VdlList *list)
{
  return *vdl_list_begin (list);
}

void *
vdl_list_back (struct VdlList *list)
{
  return *vdl_list_rbegin (list);
}


static struct VdlListItem *
vdl_list_find_from_internal (struct VdlList *list, struct VdlListItem *from,
                             void *data)
{
  struct VdlListItem *i;
  for (i = from; i != &list->tail; i = i->next)
    {
      if (i->data == data)
        {
          break;
        }
    }
  return i;
}

void **
vdl_list_find (struct VdlList *list, void *data)
{
  read_lock (list->lock);
  struct VdlListItem *item = vdl_list_find_from_internal (list, list->head.next, data);
  read_unlock (list->lock);
  return (void **) item;
}

void **
vdl_list_find_from (struct VdlList *list, void **from, void *data)
{
  read_lock (list->lock);
  struct VdlListItem *item = vdl_list_find_from_internal (list, (struct VdlListItem *) from,
                                            data);
  read_unlock (list->lock);
  return (void **) item;
}

void
vdl_list_remove (struct VdlList *list, void *data)
{
  write_lock (list->lock);
  struct VdlListItem *i;
  for (i = vdl_list_find_from_internal (list, list->head.next, data);
       i != &list->tail;
       i = vdl_list_find_from_internal (list, i, data))
    {
      i = (struct VdlListItem *) vdl_list_erase_internal (list, (void **) i);
    }
  write_unlock (list->lock);
}

void
vdl_list_reverse (struct VdlList *list)
{
  write_lock (list->lock);
  if (list->size == 0)
    {
      write_unlock (list->lock);
      return;
    }
  struct VdlListItem *cur, *next, *prev;
  struct VdlListItem *begin, *end, *last;
  begin = list->head.next;
  end = &list->tail;
  last = list->tail.prev;
  for (cur = begin; cur != end; cur = next)
    {
      next = cur->next;
      prev = cur->prev;
      cur->next = prev;
      cur->prev = next;
    }
  begin->next = &list->tail;
  list->tail.prev = begin;
  last->prev = &list->head;
  list->head.next = last;
  write_unlock (list->lock);
}

void
vdl_list_sort (struct VdlList *list,
               bool (*is_strictly_lower) (void *a, void *b, void *context),
               void *context)
{
  if (vdl_list_empty (list))
    {
      return;
    }
  write_lock (list->lock);
  // XXX insertion sort.
  struct VdlList sorted;
  vdl_list_construct (&sorted);
  struct VdlListItem *i;
  for (i = list->head.next;
       i != &list->tail;
       i = i->next)
    {
      void **j;
      void **insertion = vdl_list_end (&sorted);
      for (j = vdl_list_begin (&sorted);
           j != vdl_list_end (&sorted);
           j = vdl_list_next (&sorted, j))
        {
          if (!is_strictly_lower (*j, i->data, context))
            {
              insertion = j;
              break;
            }
        }
      vdl_list_insert_internal (&sorted, insertion, i->data);
    }
  // cut and paste the sorted list into the original list
  vdl_list_erase_range_internal (list, (void **) list->head.next, (void **) &list->tail);
  list->head.next = sorted.head.next;
  list->head.next->prev = &list->head;
  list->tail.prev = sorted.tail.prev;
  list->tail.prev->next = &list->tail;
  list->size = sorted.size;
  write_unlock (list->lock);

  sorted.head.next = &sorted.tail;
  sorted.tail.prev = &sorted.head;
  vdl_list_destruct (&sorted);
}

void
vdl_list_sorted_insert (struct VdlList *list, void *value)
{
  write_lock (list->lock);
  struct VdlListItem *i = list->head.next;
  while (value < i->data && i != &list->tail)
    {
      i = i->next;
    }
  if (value != i->data)
    {
      vdl_list_insert_internal (list, (void **) i, value);
    }
  write_unlock (list->lock);
}

void
vdl_list_unique (struct VdlList *list)
{
  write_lock (list->lock);
  struct VdlListItem *i = list->head.next;
  while (i != &list->tail)
    {
      struct VdlListItem *prev = i->prev;
      if (prev == &list->tail || prev->data != i->data)
        {
          i = i->next;
        }
      else
        {
          i = (struct VdlListItem *) vdl_list_erase_internal (list, (void **) i);
        }
    }
  write_unlock (list->lock);
}

void
vdl_list_unicize (struct VdlList *list)
{
  write_lock (list->lock);
  struct VdlListItem *i;
  for (i = list->head.next;
       i != &list->tail;
       i = i->next)
    {
      struct VdlListItem *next = vdl_list_find_from_internal (list, i->next, i->data);
      while (next != &list->tail)
        {
          next = (struct VdlListItem *) vdl_list_erase_internal (list, (void **) next);
          next = vdl_list_find_from_internal (list, next, i->data);
        }
    }
  write_unlock (list->lock);
}

void
vdl_list_iterate (struct VdlList *list, void (*iterator) (void *data))
{
  read_lock (list->lock);
  struct VdlListItem *i;
  for (i = list->head.next;
       i != &list->tail;
       i = i->next)
    {
      (*iterator) (i->data);
    }
  read_unlock (list->lock);
}

/* runs "iterator" on each element of "list" until it returns a non-null value
   returns said non-null value, or null if every iteration returned null
   read locks, runs, then unlocks "list"
   be sure that "iterator" doesn't access or modify the list structure
*/
void *
vdl_list_search_on (struct VdlList *list, void *aux,
                    void *(*iterator) (void **data, void *aux))
{
  read_lock (list->lock);
  void *ret;
  struct VdlListItem *i;
  for (i = list->head.next;
       i != &list->tail;
       i = i->next)
    {
      ret = (*iterator) ((void **)i, aux);
      if (ret)
        {
          read_unlock (list->lock);
          return ret;
        }
    }
  read_unlock (list->lock);
  return NULL;
}

struct VdlList *vdl_list_get_all (struct VdlList *list,
                                  int (*iterator) (void *data))
{
  struct VdlList *ret = vdl_list_new ();
  int is;
  struct VdlListItem *i;
  read_lock (list->lock);
  for (i = list->head.next;
       i != &list->tail;
       i = i->next)
    {
      is = (*iterator) (i->data);
      if (is)
        {
          vdl_list_insert_internal (ret, (void **) &ret->tail, i->data);
        }
    }
  read_unlock (list->lock);
  return ret;
}
