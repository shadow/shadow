#include "vdl-list.h"
#include "vdl-alloc.h"



struct VdlList *
vdl_list_new (void)
{
  struct VdlList *list = vdl_alloc_new (struct VdlList);
  vdl_list_construct (list);
  return list;
}
struct VdlList *vdl_list_copy (struct VdlList *list)
{
  struct VdlList *copy = vdl_list_new ();
  vdl_list_insert_range (copy,
			 vdl_list_begin (copy),
			 vdl_list_begin (list),
			 vdl_list_end (list));
  return copy;
}
void vdl_list_delete (struct VdlList *list)
{
  vdl_list_destruct (list);
  vdl_alloc_delete (list);
}
void vdl_list_construct (struct VdlList *list)
{
  list->size = 0;
  list->head.data = 0;
  list->head.next = &list->tail;
  list->head.prev = 0;
  list->tail.data = 0;
  list->tail.next = 0;
  list->tail.prev = &list->head;
}
void vdl_list_destruct (struct VdlList *list)
{
  vdl_list_clear (list);
}

uint32_t vdl_list_size (struct VdlList *list)
{
  return list->size;
}
bool vdl_list_empty (struct VdlList *list)
{
  return list->size == 0;
}
void **vdl_list_begin (struct VdlList *list)
{
  return (void**)list->head.next;
}
void **vdl_list_end (struct VdlList *list)
{
  return (void**)&list->tail;
}
void **vdl_list_next (void **i)
{
  struct VdlListItem *item = (struct VdlListItem *)i;
  return (void**)item->next;
}
void **vdl_list_prev (void **i)
{
  struct VdlListItem *item = (struct VdlListItem *)i;
  return (void**)item->prev;
}

void **vdl_list_rbegin (struct VdlList *list)
{
  return (void**)list->tail.prev;
}
void **vdl_list_rend (struct VdlList *list)
{
  return (void**)&list->head;
}
void **vdl_list_rnext (void **i)
{
  struct VdlListItem *item = (struct VdlListItem *)i;
  return (void**)item->prev;
}
void **vdl_list_rprev (void **i)
{
  struct VdlListItem *item = (struct VdlListItem *)i;
  return (void**)item->next;
}

void **vdl_list_insert (struct VdlList *list, void **at, void *value)
{
  struct VdlListItem *after = (struct VdlListItem *)at;
  struct VdlListItem *item = vdl_alloc_new (struct VdlListItem);
  item->data = value;
  item->next = after;
  item->prev = after->prev;
  after->prev = item;
  item->prev->next = item;
  list->size++;
  return (void**)item;
}
void vdl_list_insert_range (struct VdlList *list, void **at,
			    void **start, void **end)
{
  void **i;
  for (i = start; i != end; i = vdl_list_next (i))
    {
      vdl_list_insert (list, at, *i);
    }
}
void vdl_list_push_back (struct VdlList *list, void *data)
{
  vdl_list_insert (list, vdl_list_end (list), data);
}
void vdl_list_push_front (struct VdlList *list, void *data)
{
  vdl_list_insert (list, vdl_list_begin (list), data);
}
void vdl_list_pop_back (struct VdlList *list)
{
  vdl_list_erase (list, vdl_list_rbegin (list));
}
void vdl_list_pop_front (struct VdlList *list)
{
  vdl_list_erase (list, vdl_list_begin (list));
}
void *vdl_list_front (struct VdlList *list)
{
  return *vdl_list_begin (list);
}
void *vdl_list_back (struct VdlList *list)
{
  return *vdl_list_rbegin (list);
}
void **vdl_list_find (struct VdlList *list, void *data)
{
  return vdl_list_find_from (list, 
			     vdl_list_begin (list), 
			     data);
}
void **vdl_list_find_from (struct VdlList *list, void **from, void *data)
{
  void **i;
  for (i = from; i != vdl_list_end (list); i = vdl_list_next (i))
    {
      if (*i == data)
	{
	  return i;
	}
    }
  return vdl_list_end (list);
}
void vdl_list_clear (struct VdlList *list)
{
  vdl_list_erase_range (list, 
			vdl_list_begin (list),
			vdl_list_end (list));
}
void **vdl_list_erase (struct VdlList *list, void **i)
{
  // Note: it's a programming error to call _erase if i = _end () or i = _rend ()
  // this will trigger a crash in vdl_alloc_delete (i)
  list->size--;
  struct VdlListItem *item = (struct VdlListItem *)i;
  item->prev->next = item->next;
  item->next->prev = item->prev;
  struct VdlListItem *next = item->next;
  item->next = 0;
  item->prev = 0;
  item->data = 0;
  vdl_alloc_delete (item);
  return (void**)next;
}
void **vdl_list_erase_range (struct VdlList *list, 
			     void **s,
			     void **e)
{
  // Note: it's a programming error to call _erase if s = _end () or s = _rend ()
  // this will trigger a crash in vdl_alloc_delete (s)
  struct VdlListItem *start = (struct VdlListItem *)s;
  struct VdlListItem *end = (struct VdlListItem *)e;
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
  return (void**)end;
}
void vdl_list_remove (struct VdlList *list, void *data)
{
  void **i;
  for  (i = vdl_list_find (list, data); 
	i != vdl_list_end (list); 
	i = vdl_list_find_from (list, i, data))
    {
      i = vdl_list_erase (list, i);
    }
}
void vdl_list_reverse (struct VdlList *list)
{
  if (vdl_list_empty (list))
    {
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
}
void vdl_list_sort (struct VdlList *list, 
		    bool (*is_strictly_lower) (void *a, void *b, void *context),
		    void *context)
{
  // insertion sort.
  struct VdlList sorted;
  vdl_list_construct (&sorted);
  void **i;
  for (i = vdl_list_begin (list);
       i != vdl_list_end (list);
       i = vdl_list_next (i))
    {
      void **j;
      void **insertion = vdl_list_end (&sorted);
      for (j = vdl_list_begin (&sorted);
	   j != vdl_list_end (&sorted);
	   j = vdl_list_next (j))
	{
	  if (!is_strictly_lower (*j, *i, context))
	    {
	      insertion = j;
	      break;
	    }
	}
      vdl_list_insert (&sorted, insertion, *i);
    }
  vdl_list_clear (list);
  vdl_list_insert_range (list, 
			 vdl_list_begin (list),
			 vdl_list_begin (&sorted),
			 vdl_list_end (&sorted));
  vdl_list_destruct (&sorted);
}
void vdl_list_unique (struct VdlList *list)
{
  void **i = vdl_list_begin (list);
  while (i != vdl_list_end (list))
    {
      void **prev = vdl_list_prev (i);
      if (prev == vdl_list_end (list) ||
	  *prev != *i)
	{
	  i = vdl_list_next (i);
	}
      else
	{
	  i = vdl_list_erase (list, i);
	}
    }
}

void vdl_list_unicize (struct VdlList *list)
{
  void **i;
  for (i = vdl_list_begin (list);
       i != vdl_list_end (list);
       i = vdl_list_next (i))
    {
      void *next = vdl_list_find_from (list, vdl_list_next (i), *i);
      while (next != vdl_list_end (list))
	{
	  next = vdl_list_erase (list, next);
	  next = vdl_list_find_from (list, next, *i);
	}
    }
}

void 
vdl_list_iterate (struct VdlList *list,
		  void (*iterator) (void *data))
{
  void **i;
  for (i = vdl_list_begin (list);
       i != vdl_list_end (list);
       i = vdl_list_next (i))
    {
      (*iterator) (*i);
    }
}

