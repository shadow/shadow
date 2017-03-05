/*
  Red Black balanced tree library

    > Created (Julienne Walker): August 23, 2003
    > Modified (Julienne Walker): March 14, 2008
    > Modified (Justin Tracey): March 03, 2017
*/
#include "vdl-rbtree.h"
#include "vdl-alloc.h"

#ifndef HEIGHT_LIMIT
#define HEIGHT_LIMIT 64         /* Tallest allowable tree */
#endif

typedef struct vdl_rbnode
{
  int red;                      /* Color (1=red, 0=black) */
  void *data;                   /* User-defined content */
  struct vdl_rbnode *link[2];   /* Left (0) and right (1) links */
} vdl_rbnode_t;

struct vdl_rbtree
{
  vdl_rbnode_t *root;           /* Top of the tree */
  cmp_f cmp;                    /* Compare two items */
  dup_f dup;                    /* Clone an item (user-defined) */
  rel_f rel;                    /* Destroy an item (user-defined) */
  size_t size;                  /* Number of items (user-defined) */
};

struct vdl_rbtrav
{
  vdl_rbtree_t *tree;           /* Paired tree */
  vdl_rbnode_t *it;             /* Current node */
  vdl_rbnode_t *path[HEIGHT_LIMIT];     /* Traversal path */
  size_t top;                   /* Top of stack */
};

void *
nodup (void *p)
{
  return p;
}

void
norel ( __attribute__ ((unused))
       void *p)
{
  return;
}

/**
  <summary>
  Checks the color of a red black node
  <summary>
  <param name="root">The node to check</param>
  <returns>1 for a red node, 0 for a black node</returns>
  <remarks>For vdl_rbtree.c internal use only</remarks>
*/
static int
is_red (vdl_rbnode_t *root)
{
  return root != NULL && root->red == 1;
}

/**
  <summary>
  Performs a single red black rotation in the specified direction
  This function assumes that all nodes are valid for a rotation
  <summary>
  <param name="root">The original root to rotate around</param>
  <param name="dir">The direction to rotate (0 = left, 1 = right)</param>
  <returns>The new root ater rotation</returns>
  <remarks>For vdl_rbtree.c internal use only</remarks>
*/
static vdl_rbnode_t *
vdl_single (vdl_rbnode_t *root, int dir)
{
  vdl_rbnode_t *save = root->link[!dir];

  root->link[!dir] = save->link[dir];
  save->link[dir] = root;

  root->red = 1;
  save->red = 0;

  return save;
}

/**
  <summary>
  Performs a double red black rotation in the specified direction
  This function assumes that all nodes are valid for a rotation
  <summary>
  <param name="root">The original root to rotate around</param>
  <param name="dir">The direction to rotate (0 = left, 1 = right)</param>
  <returns>The new root after rotation</returns>
  <remarks>For vdl_rbtree.c internal use only</remarks>
*/
static vdl_rbnode_t *
vdl_double (vdl_rbnode_t *root, int dir)
{
  root->link[!dir] = vdl_single (root->link[!dir], !dir);

  return vdl_single (root, dir);
}

/**
  <summary>
  Creates an initializes a new red black node with a copy of
  the data. This function does not insert the new node into a tree
  <summary>
  <param name="tree">The red black tree this node is being created for</param>
  <param name="data">The data value that will be stored in this node</param>
  <returns>A pointer to the new node</returns>
  <remarks>
  For vdl_rbtree.c internal use only. The data for this node must
  be freed using the same tree's rel function. The returned pointer
  must be freed using C's free function
  </remarks>
*/
static vdl_rbnode_t *
new_node (vdl_rbtree_t *tree, void *data)
{
  vdl_rbnode_t *rn = vdl_alloc_new (vdl_rbnode_t);

  if (rn == NULL)
    return NULL;

  rn->red = 1;
  rn->data = tree->dup (data);
  rn->link[0] = rn->link[1] = NULL;

  return rn;
}

/**
  <summary>
  Creates and initializes an empty red black tree with
  user-defined comparison, data copy, and data release operations
  <summary>
  <param name="cmp">User-defined data comparison function</param>
  <param name="dup">User-defined data copy function</param>
  <param name="rel">User-defined data release function</param>
  <returns>A pointer to the new tree</returns>
  <remarks>
  The returned pointer must be released with vdl_rbdelete
  </remarks>
*/
vdl_rbtree_t *
vdl_rbnew (cmp_f cmp, dup_f dup, rel_f rel)
{
  vdl_rbtree_t *rt = vdl_alloc_new (vdl_rbtree_t);

  if (rt == NULL)
    return NULL;

  rt->root = NULL;
  rt->cmp = cmp;
  rt->dup = dup;
  rt->rel = rel;
  rt->size = 0;

  return rt;
}

/**
  <summary>
  Releases a valid red black tree
  <summary>
  <param name="tree">The tree to release</param>
  <remarks>
  The tree must have been created using vdl_rbnew
  </remarks>
*/
void
vdl_rbdelete (vdl_rbtree_t *tree)
{
  vdl_rbnode_t *it = tree->root;
  vdl_rbnode_t *save;

  /*
     Rotate away the left links so that
     we can treat this like the destruction
     of a linked list
   */
  while (it != NULL)
    {
      if (it->link[0] == NULL)
        {
          /* No left links, just kill the node and move on */
          save = it->link[1];
          tree->rel (it->data);
          vdl_alloc_delete (it);
        }
      else
        {
          /* Rotate away the left link and check again */
          save = it->link[0];
          it->link[0] = save->link[1];
          save->link[1] = it;
        }

      it = save;
    }

  vdl_alloc_delete (tree);
}

/**
  <summary>
  Search for a copy of the specified
  node data in a red black tree
  <summary>
  <param name="tree">The tree to search</param>
  <param name="data">The data value to search for</param>
  <returns>
  A pointer to the data value stored in the tree,
  or a null pointer if no data could be found
  </returns>
*/
void *
vdl_rbfind (vdl_rbtree_t *tree, void *data)
{
  vdl_rbnode_t *it = tree->root;

  while (it != NULL)
    {
      int cmp = tree->cmp (it->data, data);

      if (cmp == 0)
        break;

      /*
         If the tree supports duplicates, they should be
         chained to the right subtree for this to work
       */
      it = it->link[cmp < 0];
    }

  return it == NULL ? NULL : it->data;
}

/**
  <summary>
  Insert a copy of the user-specified
  data into a red black tree
  <summary>
  <param name="tree">The tree to insert into</param>
  <param name="data">The data value to insert</param>
  <returns>
  1 if the value was inserted successfully,
  0 if the insertion failed for any reason
  </returns>
*/
int
vdl_rbinsert (vdl_rbtree_t *tree, void *data)
{
  if (tree->root == NULL)
    {
      /*
         We have an empty tree; attach the
         new node directly to the root
       */
      tree->root = new_node (tree, data);

      if (tree->root == NULL)
        return 0;
    }
  else
    {
      vdl_rbnode_t head = { 0 };        /* False tree root */
      vdl_rbnode_t *g, *t;      /* Grandparent & parent */
      vdl_rbnode_t *p, *q;      /* Iterator & parent */
      int dir = 0, last = 0;

      /* Set up our helpers */
      t = &head;
      g = p = NULL;
      q = t->link[1] = tree->root;

      /* Search down the tree for a place to insert */
      for (;;)
        {
          if (q == NULL)
            {
              /* Insert a new node at the first null link */
              p->link[dir] = q = new_node (tree, data);

              if (q == NULL)
                return 0;
            }
          else if (is_red (q->link[0]) && is_red (q->link[1]))
            {
              /* Simple red violation: color flip */
              q->red = 1;
              q->link[0]->red = 0;
              q->link[1]->red = 0;
            }

          if (is_red (q) && is_red (p))
            {
              /* Hard red violation: rotations necessary */
              int dir2 = t->link[1] == g;

              if (q == p->link[last])
                t->link[dir2] = vdl_single (g, !last);
              else
                t->link[dir2] = vdl_double (g, !last);
            }

          /*
             Stop working if we inserted a node. This
             check also disallows duplicates in the tree
           */
          if (tree->cmp (q->data, data) == 0)
            break;

          last = dir;
          dir = tree->cmp (q->data, data) < 0;

          /* Move the helpers down */
          if (g != NULL)
            t = g;

          g = p, p = q;
          q = q->link[dir];
        }

      /* Update the root (it may be different) */
      tree->root = head.link[1];
    }

  /* Make the root black for simplified logic */
  tree->root->red = 0;
  ++tree->size;

  return 1;
}

/**
  <summary>
  Remove a node from a red black tree
  that matches the user-specified data
  <summary>
  <param name="tree">The tree to remove from</param>
  <param name="data">The data value to search for</param>
  <returns>
  1 if the value was removed successfully,
  0 if the removal failed for any reason
  </returns>
  <remarks>
  The most common failure reason should be
  that the data was not found in the tree
  </remarks>
*/
int
vdl_rberase (vdl_rbtree_t *tree, void *data)
{
  if (tree->root != NULL)
    {
      vdl_rbnode_t head = { 0 };        /* False tree root */
      vdl_rbnode_t *q, *p, *g;  /* Helpers */
      vdl_rbnode_t *f = NULL;   /* Found item */
      int dir = 1;

      /* Set up our helpers */
      q = &head;
      g = p = NULL;
      q->link[1] = tree->root;

      /*
         Search and push a red node down
         to fix red violations as we go
       */
      while (q->link[dir] != NULL)
        {
          int last = dir;

          /* Move the helpers down */
          g = p, p = q;
          q = q->link[dir];
          dir = tree->cmp (q->data, data) < 0;

          /*
             Save the node with matching data and keep
             going; we'll do removal tasks at the end
           */
          if (tree->cmp (q->data, data) == 0)
            f = q;

          /* Push the red node down with rotations and color flips */
          if (!is_red (q) && !is_red (q->link[dir]))
            {
              if (is_red (q->link[!dir]))
                p = p->link[last] = vdl_single (q, dir);
              else if (!is_red (q->link[!dir]))
                {
                  vdl_rbnode_t *s = p->link[!last];

                  if (s != NULL)
                    {
                      if (!is_red (s->link[!last]) && !is_red (s->link[last]))
                        {
                          /* Color flip */
                          p->red = 0;
                          s->red = 1;
                          q->red = 1;
                        }
                      else
                        {
                          int dir2 = g->link[1] == p;

                          if (is_red (s->link[last]))
                            g->link[dir2] = vdl_double (p, last);
                          else if (is_red (s->link[!last]))
                            g->link[dir2] = vdl_single (p, last);

                          /* Ensure correct coloring */
                          q->red = g->link[dir2]->red = 1;
                          g->link[dir2]->link[0]->red = 0;
                          g->link[dir2]->link[1]->red = 0;
                        }
                    }
                }
            }
        }

      /* Replace and remove the saved node */
      if (f != NULL)
        {
          tree->rel (f->data);
          f->data = q->data;
          p->link[p->link[1] == q] = q->link[q->link[0] == NULL];
          vdl_alloc_delete (q);
        }

      /* Update the root (it may be different) */
      tree->root = head.link[1];

      /* Make the root black for simplified logic */
      if (tree->root != NULL)
        tree->root->red = 0;

      --tree->size;
    }

  return 1;
}

/**
  <summary>
  Gets the number of nodes in a red black tree
  <summary>
  <param name="tree">The tree to calculate a size for</param>
  <returns>The number of nodes in the tree</returns>
*/
size_t
vdl_rbsize (vdl_rbtree_t *tree)
{
  return tree->size;
}

/**
  <summary>
  Create a new traversal object
  <summary>
  <returns>A pointer to the new object</returns>
  <remarks>
  The traversal object is not initialized until
  vdl_rbtfirst or vdl_rbtlast are called.
  The pointer must be released with vdl_rbtdelete
  </remarks>
*/
vdl_rbtrav_t *
vdl_rbtnew (void)
{
  return vdl_alloc_new (vdl_rbtrav_t);
}

/**
  <summary>
  Release a traversal object
  <summary>
  <param name="trav">The object to release</param>
  <remarks>
  The object must have been created with vdl_rbtnew
  </remarks>
*/
void
vdl_rbtdelete (vdl_rbtrav_t *trav)
{
  vdl_alloc_delete (trav);
}

/**
  <summary>
  Initialize a traversal object. The user-specified
  direction determines whether to begin traversal at the
  smallest or largest valued node
  <summary>
  <param name="trav">The traversal object to initialize</param>
  <param name="tree">The tree that the object will be attached to</param>
  <param name="dir">
  The direction to traverse (0 = ascending, 1 = descending)
  </param>
  <returns>A pointer to the smallest or largest data value</returns>
  <remarks>For vdl_rbtree.c internal use only</remarks>
*/
static void *
start (vdl_rbtrav_t *trav, vdl_rbtree_t *tree, int dir)
{
  trav->tree = tree;
  trav->it = tree->root;
  trav->top = 0;

  /* Save the path for later traversal */
  if (trav->it != NULL)
    {
      while (trav->it->link[dir] != NULL)
        {
          trav->path[trav->top++] = trav->it;
          trav->it = trav->it->link[dir];
        }
    }

  return trav->it == NULL ? NULL : trav->it->data;
}

/**
  <summary>
  Traverse a red black tree in the user-specified direction
  <summary>
  <param name="trav">The initialized traversal object</param>
  <param name="dir">
  The direction to traverse (0 = ascending, 1 = descending)
  </param>
  <returns>
  A pointer to the next data value in the specified direction
  </returns>
  <remarks>For vdl_rbtree.c internal use only</remarks>
*/
static void *
move (vdl_rbtrav_t *trav, int dir)
{
  if (trav->it->link[dir] != NULL)
    {
      /* Continue down this branch */
      trav->path[trav->top++] = trav->it;
      trav->it = trav->it->link[dir];

      while (trav->it->link[!dir] != NULL)
        {
          trav->path[trav->top++] = trav->it;
          trav->it = trav->it->link[!dir];
        }
    }
  else
    {
      /* Move to the next branch */
      vdl_rbnode_t *last;

      do
        {
          if (trav->top == 0)
            {
              trav->it = NULL;
              break;
            }

          last = trav->it;
          trav->it = trav->path[--trav->top];
        }
      while (last == trav->it->link[dir]);
    }

  return trav->it == NULL ? NULL : trav->it->data;
}

/**
  <summary>
  Initialize a traversal object to the smallest valued node
  <summary>
  <param name="trav">The traversal object to initialize</param>
  <param name="tree">The tree that the object will be attached to</param>
  <returns>A pointer to the smallest data value</returns>
*/
void *
vdl_rbtfirst (vdl_rbtrav_t *trav, vdl_rbtree_t *tree)
{
  return start (trav, tree, 0); /* Min value */
}

/**
  <summary>
  Initialize a traversal object to the largest valued node
  <summary>
  <param name="trav">The traversal object to initialize</param>
  <param name="tree">The tree that the object will be attached to</param>
  <returns>A pointer to the largest data value</returns>
*/
void *
vdl_rbtlast (vdl_rbtrav_t *trav, vdl_rbtree_t *tree)
{
  return start (trav, tree, 1); /* Max value */
}

/**
  <summary>
  Traverse to the next value in ascending order
  <summary>
  <param name="trav">The initialized traversal object</param>
  <returns>A pointer to the next value in ascending order</returns>
*/
void *
vdl_rbtnext (vdl_rbtrav_t *trav)
{
  return move (trav, 1);        /* Toward larger items */
}

/**
  <summary>
  Traverse to the next value in descending order
  <summary>
  <param name="trav">The initialized traversal object</param>
  <returns>A pointer to the next value in descending order</returns>
*/
void *
vdl_rbtprev (vdl_rbtrav_t *trav)
{
  return move (trav, 0);        /* Toward smaller items */
}
