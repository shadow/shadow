#include <sys/time.h>
#include <time.h>
#include "evtracker.h"

int main() {
	unsigned int slots, events, i,j, times, mischecks=0;
	struct timeval tv1,tv2;
	char event = '!';
	ptime_t time, time2;
	evtracker_tp evt;
	heap_tp heap;
	
	srand(481438);
	
	slots = 256;
	times = 4000;
	events = 10000;
	
	evt = evtracker_create(slots, 100);
	gettimeofday(&tv1,NULL);
	time.v.type = PTIME_TYPE_VALID;
	
	for(i=0; i<times; i++) {
		time.v.sec = rand();
		time.v.msec = rand() % 1000;
		for(j=0; j<events; j++)
			evtracker_insert_event(evt, time, &event);
	}
	
	if(evtracker_get_numevents(evt) != times*events)
		printf("Event count off. Should be %i, got %i\n",times*events,evtracker_get_numevents(evt));
	
	time.packed = 0;
	times = 0;
	events = 0;
	
	while(evtracker_get_nextevent(evt, &time2, 1)) {
		events++;
		if(time2.packed < time.packed) 
			mischecks++;
		
		if(time2.packed > time.packed) {
			times++;
			time = time2;
		}
	}
	printf("Total times pulled: %i. Total events pulled: %i. Events left: %i. Mischecks: %i\n", times, events, evtracker_get_numevents(evt),mischecks);
	
	gettimeofday(&tv2,NULL);
	printf("\nTime 1: %i %i   Time 2:  %i %i\n", tv1.tv_sec, tv1.tv_usec, tv2.tv_sec, tv2.tv_usec);

	evtracker_destroy(evt);

/*	gettimeofday(&tv1,NULL);
	for(i=0; i<events; i++) {
		time.packed = rand();
		heap_insert(*/
	
	
	return 0;
}

