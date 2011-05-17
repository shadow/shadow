/*
 * test_hashtable.h
 *
 * This test was written because I found that even though keys existed in
 * the hashtable, they were not accessible.
 *
 *  Created on: Dec 22, 2010
 *      Author: rob
 */

#include <stdio.h>
#include <stdlib.h>

#include "hashtable.h"

typedef struct test_s {
	hashtable_tp h;
}test_t, *test_tp;

void testht(void* v, int k) {
	hashtable_tp ht = (hashtable_tp)v;

	void* got = hashtable_get(ht, k);
	if(!got){
		printf("ERROR, cant get k=%i\n", k);
	} else {
		printf("k=%i\n", k);
	}
}

void add(hashtable_tp ht, int k){
	printf("====before_add_%i====\n", k);
	hashtable_walk(ht, &testht);
	hashtable_set(ht, k, ht);
	printf("----after_add_%i----\n", k);
	hashtable_walk(ht, &testht);
}

void rem(hashtable_tp ht, int k){
	printf("====before_rem_%i====\n", k);
	hashtable_walk(ht, &testht);
	hashtable_remove(ht, k);
	printf("----after_rem_%i----\n", k);
	hashtable_walk(ht, &testht);
}

int main(void) {
	hashtable_tp ht = hashtable_create(10,0.75);
	int keys[] = {91816332,138674712,227672893,140313093,176947854,192742194,100860324,152830647,46137617,139788839};

	add(ht, keys[2]);
	add(ht, keys[4]);
	rem(ht, keys[2]); /*<--rehash*/
	add(ht, keys[0]);
	add(ht, keys[3]);
	add(ht, keys[1]); /*<--rehash*/
	add(ht, keys[6]);
	add(ht, keys[5]);
	rem(ht, keys[4]); /*error*/
	add(ht, keys[7]);
	add(ht, keys[8]);
	rem(ht, keys[0]);
	rem(ht, keys[3]);
	add(ht, keys[9]);
	rem(ht, keys[1]);

	hashtable_destroy(ht);
	printf("done.\n");

	return 0;
}
