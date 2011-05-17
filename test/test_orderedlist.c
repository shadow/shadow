/*
 * test_orderedlist.c
 *
 *  Created on: Oct 16, 2010
 *      Author: rob
 */
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <string.h>

#include "orderedlist.h"

static void test_add_peek(){
	char* str1 = "1";
	char* str2 = "2";
	char* str3 = "3";
	char* value;

	orderedlist_tp list = orderedlist_create();
	assert(list != NULL);

	/* empty list should have NULLs */
	assert(orderedlist_peek_first_value(list) == NULL);
	assert(orderedlist_peek_last_value(list) == NULL);
	assert(orderedlist_remove_first(list) == NULL);
	assert(orderedlist_remove_last(list) == NULL);
	assert(list->length == 0);

	/* test add, peek */
	orderedlist_add(list, 5, str1);
	assert(list->length == 1);
	value = (char*) orderedlist_peek_first_value(list);
	assert(value != NULL);
	assert(memcmp(str1, value, strlen(str1)) == 0);

	value = (char*) orderedlist_peek_last_value(list);
	assert(value != NULL);
	assert(memcmp(str1, value, strlen(str1)) == 0);
	assert(list->length == 1);

	orderedlist_add(list, 5, str2);

	value = (char*) orderedlist_peek_last_value(list);
	assert(value != NULL);
	assert(memcmp(str2, value, strlen(str2)) == 0);
	assert(list->length == 2);

	orderedlist_add(list, 4, str3);
	value = (char*) orderedlist_peek_first_value(list);
	assert(value != NULL);
	assert(memcmp(str3, value, strlen(str3)) == 0);
	assert(list->length == 3);

	orderedlist_destroy(list, 0);
}

static void test_remove_single(){
	char* str1 = "1";
	char* str2 = "2";
	char* value;

	orderedlist_tp list = orderedlist_create();
	orderedlist_add(list, 5, str1);

	/* test removing single element */
	value = (char*) orderedlist_remove_first(list);
	assert(value != NULL);
	assert(memcmp(str1, value, strlen(str1)) == 0);
	assert(list->length == 0);

	orderedlist_add(list, 5, str2);

	value = (char*) orderedlist_remove_last(list);
	assert(value != NULL);
	assert(memcmp(str2, value, strlen(str2)) == 0);
	assert(list->length == 0);

	orderedlist_destroy(list, 0);
}

static void test_remove_multiple(){
	char* str1 = "1";
	char* str2 = "2";
	char* value;

	orderedlist_tp list = orderedlist_create();
	orderedlist_add(list, 5, str1);
	orderedlist_add(list, 10, str2);
	assert(list->length == 2);

	/* test removing multiple element */
	value = (char*) orderedlist_remove_first(list);
	assert(value != NULL);
	assert(memcmp(str1, value, strlen(str1)) == 0);
	assert(list->length == 1);
	value = (char*) orderedlist_remove_first(list);
	assert(value != NULL);
	assert(memcmp(str2, value, strlen(str2)) == 0);
	assert(list->length == 0);

	orderedlist_add(list, 5, str1);
	orderedlist_add(list, 10, str2);
	assert(list->length == 2);

	value = (char*) orderedlist_remove_last(list);
	assert(value != NULL);
	assert(memcmp(str2, value, strlen(str2)) == 0);
	assert(list->length == 1);
	value = (char*) orderedlist_remove_last(list);
	assert(value != NULL);
	assert(memcmp(str1, value, strlen(str1)) == 0);
	assert(list->length == 0);

	assert(orderedlist_remove_first(list) == NULL);
	assert(orderedlist_remove_last(list) == NULL);

	orderedlist_destroy(list, 0);
}

static void test_remove_index(){
	char* str1 = "1";
	char* str2 = "2";
	char* str3 = "3";
	char* value;

	orderedlist_tp list = orderedlist_create();
	assert(orderedlist_remove(list, 0) == NULL);

	orderedlist_add(list, 5, str1);
	orderedlist_add(list, 15, str2);
	orderedlist_add(list, 25, str3);
	assert(list->length == 3);

	/* test remove index middle */
	value = (char*) orderedlist_remove(list, 15);
	assert(value != NULL);
	assert(memcmp(str2, value, strlen(str2)) == 0);
	assert(list->length == 2);

	orderedlist_add(list, 15, str2);

	/* test remove index first */
	value = (char*) orderedlist_remove(list, 5);
	assert(value != NULL);
	assert(memcmp(str1, value, strlen(str1)) == 0);
	assert(list->length == 2);

	orderedlist_add(list, 5, str1);

	/* test remove index last */
	value = (char*) orderedlist_remove(list, 25);
	assert(value != NULL);
	assert(memcmp(str3, value, strlen(str3)) == 0);
	assert(list->length == 2);

	assert(orderedlist_remove(list, 25) == NULL);
	assert(orderedlist_remove(list, 0) == NULL);

	orderedlist_destroy(list, 0);
}

static void test_compact(){
	char* str1 = "1";
	char* str2 = "2";
	char* str3 = "3";
	char* value;

	orderedlist_tp list = orderedlist_create();
	orderedlist_add(list, 5, str1);
	orderedlist_add(list, 15, str2);
	orderedlist_add(list, 25, str3);
	assert(list->length == 3);

	assert(orderedlist_compact(list) == 3);
	assert(list->length == 3);

	/* test middle element after compact */
	value = (char*) orderedlist_remove(list, 1);
	assert(value != NULL);
	assert(memcmp(str2, value, strlen(str2)) == 0);

	/* test last element after compact */
	value = (char*) orderedlist_remove(list, 2);
	assert(value != NULL);
	assert(memcmp(str3, value, strlen(str3)) == 0);

	/* test first element after compact */
	value = (char*) orderedlist_remove(list, 0);
	assert(value != NULL);
	assert(memcmp(str1, value, strlen(str1)) == 0);

	orderedlist_destroy(list, 0);
}

int main(int argc, char * argv[]) {
	test_add_peek();
	test_remove_single();
	test_remove_multiple();
	test_remove_index();
	test_compact();
	printf("All tests pass!!\n");
}
