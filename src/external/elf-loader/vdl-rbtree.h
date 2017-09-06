#ifndef VDL_RBTREE_H
#define VDL_RBTREE_H

/*
  Red Black balanced tree library

    > Created (Julienne Walker): August 23, 2003
    > Modified (Julienne Walker): March 14, 2008
    > Modified (Justin Tracey): March 03, 2017
*/
#include <stddef.h>

/* Opaque types */
typedef struct vdl_rbtree vdl_rbtree_t;
typedef struct vdl_rbtrav vdl_rbtrav_t;

/* User-defined item handling */
typedef int (*cmp_f) (const void *p1, const void *p2);
typedef void *(*dup_f) (void *p);
typedef void (*rel_f) (void *p);

/* Placeholder dup_f and rel_f functions */
void *nodup (void *p);
void norel (void *p);

/* Red Black tree functions */
vdl_rbtree_t *vdl_rbnew (cmp_f cmp, dup_f dup, rel_f rel);
void vdl_rbdelete (vdl_rbtree_t *tree);
void *vdl_rbfind (const vdl_rbtree_t *tree, void *data);
int vdl_rbinsert (vdl_rbtree_t *tree, void *data);
int vdl_rberase (vdl_rbtree_t *tree, void *data);
size_t vdl_rbsize (vdl_rbtree_t *tree);

/* Traversal functions */
/* currently thread-unsafe */
vdl_rbtrav_t *vdl_rbtnew (void);
void vdl_rbtdelete (vdl_rbtrav_t *trav);
void *vdl_rbtfirst (vdl_rbtrav_t *trav, vdl_rbtree_t *tree);
void *vdl_rbtlast (vdl_rbtrav_t *trav, vdl_rbtree_t *tree);
void *vdl_rbtnext (vdl_rbtrav_t *trav);
void *vdl_rbtprev (vdl_rbtrav_t *trav);

#endif
