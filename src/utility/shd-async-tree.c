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

#include <glib.h>

#include "shd-async-tree.h"

struct _AsyncTree {
	GTree* t;
	GMutex* lock;
	guint refcount;
};

AsyncTree* asynctree_new(GCompareFunc key_compare_func) {
	AsyncTree* tree = g_new0(AsyncTree, 1);
	tree->t = g_tree_new(key_compare_func);
	tree->refcount = 1;
	tree->lock = g_mutex_new();
	return tree;
}

AsyncTree* asynctree_new_with_data(GCompareDataFunc key_compare_func,
		gpointer key_compare_data) {
	AsyncTree* tree = g_new0(AsyncTree, 1);
	tree->t = g_tree_new_with_data(key_compare_func, key_compare_data);
	tree->refcount = 1;
	tree->lock = g_mutex_new();
	return tree;
}

AsyncTree* asynctree_new_full(GCompareDataFunc key_compare_func,
		gpointer key_compare_data, GDestroyNotify key_destroy_func,
		GDestroyNotify value_destroy_func) {
	AsyncTree* tree = g_new0(AsyncTree, 1);
	tree->t = g_tree_new_full(key_compare_func, key_compare_data,
			key_destroy_func, value_destroy_func);
	tree->refcount = 1;
	tree->lock = g_mutex_new();
	return tree;
}

AsyncTree* asynctree_ref(AsyncTree *tree) {
	g_assert(tree);
	g_mutex_lock(tree->lock);
	g_tree_ref(tree->t);
	(tree->refcount)++;
	g_mutex_unlock(tree->lock);
	return tree;
}

void asynctree_unref(AsyncTree *tree) {
	g_assert(tree);
	g_mutex_lock(tree->lock);
	g_tree_unref(tree->t);
	(tree->refcount)--;
	if(tree->refcount == 0) {
		tree->t = NULL;
		g_mutex_unlock(tree->lock);
		g_mutex_free(tree->lock);
		g_free(tree);
	} else {
		g_mutex_unlock(tree->lock);
	}
}

void asynctree_destroy(AsyncTree *tree) {
	g_assert(tree);
	g_mutex_lock(tree->lock);
	g_tree_destroy(tree->t);
	tree->t = NULL;
	g_mutex_unlock(tree->lock);
	g_mutex_free(tree->lock);
	g_free(tree);
}

void asynctree_insert(AsyncTree *tree, gpointer key, gpointer value) {
	g_assert(tree);
	g_mutex_lock(tree->lock);
	g_tree_insert(tree->t, key, value);
	g_mutex_unlock(tree->lock);
}

void asynctree_replace(AsyncTree *tree, gpointer key, gpointer value) {
	g_assert(tree);
	g_mutex_lock(tree->lock);
	g_tree_replace(tree->t, key, value);
	g_mutex_unlock(tree->lock);
}

gboolean asynctree_remove(AsyncTree *tree, gconstpointer key) {
	g_assert(tree);
	g_mutex_lock(tree->lock);
	gboolean b = g_tree_remove(tree->t, key);
	g_mutex_unlock(tree->lock);
	return b;
}

gboolean asynctree_steal(AsyncTree *tree, gconstpointer key) {
	g_assert(tree);
	g_mutex_lock(tree->lock);
	gboolean b = g_tree_steal(tree->t, key);
	g_mutex_unlock(tree->lock);
	return b;
}

gpointer asynctree_lookup(AsyncTree *tree, gconstpointer key) {
	g_assert(tree);
	g_mutex_lock(tree->lock);
	gpointer p = g_tree_lookup(tree->t, key);
	g_mutex_unlock(tree->lock);
	return p;
}

gboolean asynctree_lookup_extended(AsyncTree *tree, gconstpointer lookup_key,
		gpointer *orig_key, gpointer *value) {
	g_assert(tree);
	g_mutex_lock(tree->lock);
	gboolean b = g_tree_lookup_extended(tree->t, lookup_key, orig_key, value);
	g_mutex_unlock(tree->lock);
	return b;
}

void asynctree_foreach(AsyncTree *tree, GTraverseFunc func, gpointer user_data) {
	g_assert(tree);
	g_mutex_lock(tree->lock);
	g_tree_foreach(tree->t, func, user_data);
	g_mutex_unlock(tree->lock);
}

gpointer asynctree_search(AsyncTree *tree, GCompareFunc search_func,
		gconstpointer user_data) {
	g_assert(tree);
	g_mutex_lock(tree->lock);
	gpointer p = g_tree_search(tree->t, search_func, user_data);
	g_mutex_unlock(tree->lock);
	return p;
}

gint asynctree_height(AsyncTree *tree) {
	g_assert(tree);
	g_mutex_lock(tree->lock);
	gint h = g_tree_height(tree->t);
	g_mutex_unlock(tree->lock);
	return h;
}

gint asynctree_nnodes(AsyncTree *tree) {
	g_assert(tree);
	g_mutex_lock(tree->lock);
	gint n = g_tree_nnodes(tree->t);
	g_mutex_unlock(tree->lock);
	return n;
}
