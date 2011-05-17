#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <stdlib.h>

#include "dsim.h"

extern FILE * yyin;

int main(int argc, char * argv [] ) {
	printf(" dsim!\n");
	
	oplist_tp oplist = dsim_oplist_create();
	
	dsim_parse(oplist, "/cygdrive/c/projects/p2p/dvn/src/dvn.dsim");
	
	return 0;
}
