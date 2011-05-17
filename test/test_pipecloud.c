/*
 * test_pipecloud.c
 *
 *  Created on: Aug 31, 2009
 *      Author: Tyson
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include "pipecloud.h"

#define TOTAL 100000
#define FRAMESIZE 10
#define OWIDTH "9"

void process_consumer(pipecloud_tp pc, int mb, int num_stops) {
	pipecloud_config_localized(pc, mb, 0);
	char buffer[FRAMESIZE];
	int rr;
	int go = 1;
	int outer = 0;
	int total=0;

	while(num_stops) {
		outer++;
		pipecloud_select(pc);

		while((rr = pipecloud_read(pc, buffer, FRAMESIZE)) > 0){
			total++;
			if(buffer[0] == '?')
				num_stops--;	
		}
	}

	printf("consumer: %d done (total:%d, outerloop: %d)\n",mb,total,outer);
}

void process_producer(pipecloud_tp pc, int num_mboxes, int count) {
	//pipecloud_config_localized(pc, mb, 0);
	char dbuf[FRAMESIZE];
	int i;

	for( i=0; i<count; i++) {
		snprintf(dbuf, FRAMESIZE, "%" OWIDTH "d", i);
		pipecloud_write(pc, rand() % num_mboxes, dbuf, FRAMESIZE);
	}

	snprintf(dbuf, FRAMESIZE, "?         ");
	for(i=0; i<num_mboxes; i++) 
		pipecloud_write(pc, i, dbuf, FRAMESIZE); 

	printf("producer: done\n");
}

int main(int argc, char * argv[]) {
	pipecloud_tp pc;
	int pcount, ccount, pitemcount;
	int fv;
	int total_procs, wait_status;
	int i;

	if(argc != 4) {
		printf("Usage: %s <num producers> <num consumers> <producer itemcount>\n", argv[0]);
		exit(1);
	}

	pcount = atoi(argv[1]);
	ccount = atoi(argv[2]);
	pitemcount = atoi(argv[3]);

	total_procs = pcount + ccount;

	srand(time(NULL));

	printf("\nRunning...\n");

	// create pc
	pc = pipecloud_create(ccount, 20480);

	// spawn consumers
	for(i = 0; i < ccount; i++) {
		if(fork() == 0) {
			process_consumer(pc, i, pcount);
			exit(0);
		}
	}

	// spawn producers
	for(i = 0; i < pcount; i++) {
		if(fork() == 0) {
			process_producer(pc, ccount, pitemcount); 
			exit(0);
		}
	}

	while(total_procs) {
		wait(NULL);
		total_procs--;
	}

	printf("Complete.\n");


	return 0;
}
