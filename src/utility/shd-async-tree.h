/**
 * The Shadow Simulator
 *
 * Copyright (c) 2010-2012 Rob Jansen <jansen@cs.umn.edu>
 *
 * This file is part of Shadow.
 *
 * Shadow is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * Shadow is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with Shadow.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef SHD_ASYNC_TREE_H_
#define SHD_ASYNC_TREE_H_

typedef struct _AsyncTree AsyncTree;

AsyncTree* asynctree_new(GCompareFunc key_compare_func);
AsyncTree* asynctree_new_with_data(GCompareDataFunc key_compare_func,
		gpointer key_compare_data);
AsyncTree* asynctree_new_full(GCompareDataFunc key_compare_func,
		gpointer key_compare_data, GDestroyNotify key_destroy_func,
		GDestroyNotify value_destroy_func);
AsyncTree* asynctree_ref(AsyncTree *tree);
void asynctree_unref(AsyncTree *tree);
void asynctree_destroy(AsyncTree *tree);

void asynctree_insert(AsyncTree *tree, gpointer key, gpointer value);
void asynctree_replace(AsyncTree *tree, gpointer key, gpointer value);
gboolean asynctree_remove(AsyncTree *tree, gconstpointer key);
gboolean asynctree_steal(AsyncTree *tree, gconstpointer key);
gpointer asynctree_lookup(AsyncTree *tree, gconstpointer key);
gboolean asynctree_lookup_extended(AsyncTree *tree, gconstpointer lookup_key,
		gpointer *orig_key, gpointer *value);
void asynctree_foreach(AsyncTree *tree, GTraverseFunc func, gpointer user_data);
gpointer asynctree_search(AsyncTree *tree, GCompareFunc search_func,
		gconstpointer user_data);
gint asynctree_height(AsyncTree *tree);
gint asynctree_nnodes(AsyncTree *tree);

#endif /* SHD_ASYNC_TREE_H_ */
