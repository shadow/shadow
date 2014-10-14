/*
 * test_linkedbuffer.c
 *
 *  Created on: Oct 9, 2010
 *      Author: rob
 */
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <string.h>

#include "linkedbuffer.h"

#define LINK_CAPACITY_BYTES 1500

void* get_random_buffer(size_t length) {
    void* buffer = malloc(length);

    if(buffer != NULL) {
        FILE* f = fopen("/dev/urandom", "r");
        assert(fread(buffer, 1, length, f) == length);
        fclose(f);
    }

    assert(buffer);

    return buffer;
}

void test_add_remove_small(){
    linkedbuffer_tp lbuffer = linkedbuffer_create(LINK_CAPACITY_BYTES);
    assert(lbuffer);
    assert(lbuffer->num_links == 0);
    assert(lbuffer->length == 0);
    assert(lbuffer->head_w_offset == 0);
    assert(lbuffer->tail_r_offset == 0);

    void* wbuffer = get_random_buffer(LINK_CAPACITY_BYTES+1);
    void* rbuffer = calloc(LINK_CAPACITY_BYTES+1, 1);
    /* buffers should be different */
    assert(memcmp(wbuffer, rbuffer, LINK_CAPACITY_BYTES+1) != 0);

    /* test lazy link creation */
    size_t result = linkedbuffer_write(lbuffer, wbuffer, LINK_CAPACITY_BYTES);
    assert(result == LINK_CAPACITY_BYTES);
    assert(lbuffer->length == LINK_CAPACITY_BYTES);
    assert(lbuffer->num_links == 1);
    assert(lbuffer->head == lbuffer->tail);
    assert(lbuffer->head_w_offset == LINK_CAPACITY_BYTES);
    assert(lbuffer->tail_r_offset == 0);

    /* adding another byte should add another link */
    result = linkedbuffer_write(lbuffer, wbuffer+LINK_CAPACITY_BYTES, 1);
    assert(result == 1);
    assert(lbuffer->length == LINK_CAPACITY_BYTES + 1);
    assert(lbuffer->num_links == 2);
    assert(lbuffer->head != lbuffer->tail);
    assert(lbuffer->head_w_offset == 1);
    assert(lbuffer->tail_r_offset == 0);

    /* removing a byte */
    result = linkedbuffer_read(lbuffer, rbuffer, 1);
    assert(result == 1);
    assert(lbuffer->length == LINK_CAPACITY_BYTES);
    assert(lbuffer->num_links == 2);
    assert(lbuffer->head != lbuffer->tail);
    assert(lbuffer->head_w_offset == 1);
    assert(lbuffer->tail_r_offset == 1);

    /* removing a link, should proactively delete link */
    result = linkedbuffer_read(lbuffer, rbuffer+1, LINK_CAPACITY_BYTES - 1);
    assert(result == LINK_CAPACITY_BYTES - 1);
    assert(lbuffer->length == 1);
    assert(lbuffer->num_links == 1);
    assert(lbuffer->head == lbuffer->tail);
    assert(lbuffer->head_w_offset == 1);
    assert(lbuffer->tail_r_offset == 0);

    /* removing the rest of the data */
    result = linkedbuffer_read(lbuffer, rbuffer+LINK_CAPACITY_BYTES, 1);
    assert(result == 1);
    assert(lbuffer->length == 0);
    assert(lbuffer->num_links == 0);
    assert(lbuffer->head == lbuffer->tail);
    assert(lbuffer->head_w_offset == 0);
    assert(lbuffer->tail_r_offset == 0);

    /* what we read should be what we wrote */
    assert(memcmp(wbuffer, rbuffer, LINK_CAPACITY_BYTES+1) == 0);

    linkedbuffer_destroy(lbuffer);
}

void test_add_remove_large(){
    linkedbuffer_tp lbuffer = linkedbuffer_create(LINK_CAPACITY_BYTES);
    assert(lbuffer);
    assert(lbuffer->length == 0);

    size_t size = LINK_CAPACITY_BYTES*100;

    void* wbuffer = get_random_buffer(size);
    void* rbuffer = calloc(size, 1);
    /* buffers should be different */
    assert(memcmp(wbuffer, rbuffer, size) != 0);

    size_t result = linkedbuffer_write(lbuffer, wbuffer, size);
    assert(result == size);
    assert(lbuffer->length == size);

    result = linkedbuffer_read(lbuffer, rbuffer, size);
    assert(result == size);
    assert(lbuffer->length == 0);

    /* what we read should be what we wrote */
    assert(memcmp(wbuffer, rbuffer, size) == 0);

    linkedbuffer_destroy(lbuffer);
}

void test_remove_empty(){
    linkedbuffer_tp lbuffer = linkedbuffer_create(LINK_CAPACITY_BYTES);
    assert(lbuffer);
    assert(lbuffer->length == 0);

    size_t size = 1;

    void* wbuffer = get_random_buffer(size);
    void* rbuffer = calloc(size, 1);
    /* buffers should be different */
    assert(memcmp(wbuffer, rbuffer, size) != 0);

    size_t result = linkedbuffer_read(lbuffer, rbuffer, size);
    assert(result == 0);
    assert(lbuffer->length == 0);

    result = linkedbuffer_write(lbuffer, wbuffer, size);
    assert(result == size);
    assert(lbuffer->length == size);

    result = linkedbuffer_read(lbuffer, rbuffer, size*2);
    assert(result == size);
    assert(lbuffer->length == 0);

    /* what we read should be what we wrote */
    assert(memcmp(wbuffer, rbuffer, size) == 0);

    linkedbuffer_destroy(lbuffer);
}

int main(int argc, char * argv[]) {
    test_add_remove_small();
    test_add_remove_large();
    test_remove_empty();

    printf("All tests pass!\n");
}
