
#include <stdlib.h>
#include <stdio.h>
#include "btree.h"

int main(void) {
	btree_tp bt = btree_create(5);
	int items,i;
	int * values; 
	
	srand(481438);
	
	printf("Items: ");
	scanf("%i", &items);
	
	values = malloc(sizeof(*values) * items);
	
	for(i=0; i<items; i++) {
		values[i] = rand() % 100;
		btree_insert(bt, values[i], &items);
	}
	
	//btree_dump(bt);
	printf("Btree size: %d\n", btree_get_size(bt));
	
	for(i=0; i<items; i++) {
		//printf("Removing %d: ", values[i]);
		btree_remove(bt, values[i]);
		//btree_dump(bt);
	}
	
	//btree_dump(bt);
	printf("Btree size: %d\n", btree_get_size(bt));
	
	free(values);
	btree_destroy(bt);
	
	return;
}