/**
 * The Shadow Simulator
 *
 * Copyright (c) 2010-2011 Rob Jansen <jansen@cs.umn.edu>
 * Copyright (c) 2006-2009 Tyson Malchow <tyson.malchow@gmail.com>
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

#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stdio.h>
#include <errno.h>

#include "socket.h"
#include "netconst.h"
#include "clo.h"
#include "nbdf.h"

struct DVN_CLIENT_CONFIG {
	char type[200];
	char adr[200];
	char dsim_path[200];
	int port;

	char worker_host[200];
	int worker_port;

	int adrsplit;
	int n_threads;

	unsigned int verbose : 1;
};

#define CLOPTION_PORT			1
#define CLOPTION_DSIM			2
#define CLOPTION_NTHREADS		3
#define CLOPTION_VERBOSE		4
#define CLOPTION_ADDRESS		5
#define CLOPTION_TYPE			6
#define CLOPTION_HELP			7
#define CLOPTION_CPORT			8
#define CLOPTION_CHOST			9

int clo_handle(char *v, int o, struct DVN_CLIENT_CONFIG* dconfig) {
	switch(o) {
		case CLOPTION_NTHREADS:
			dconfig->n_threads = atoi(v);
			break;
		case CLOPTION_CHOST:
			strcpy(dconfig->worker_host, v);
			break;
		case CLOPTION_CPORT:
			dconfig->worker_port = atoi(v);
			break;
		case CLOPTION_PORT:
			dconfig->port = atoi(v);
			break;
		case CLOPTION_VERBOSE:
			dconfig->verbose = 1;
			break;
		case CLOPTION_ADDRESS:
			strcpy(dconfig->adr, v);
			break;
		case CLOPTION_DSIM:
			strcpy(dconfig->dsim_path, v);
			break;
		case CLOPTION_TYPE:
			strcpy(dconfig->type, v);
			break;
		case CLOPTION_HELP:
			return CLO_USAGE;
		default:
			return CLO_BAD;
	}
	return CLO_OKAY;
}


struct CLO_entry cloentries[] = {
		{CLOPTION_PORT,'p',"--port",1,"Port of remote DVN host."},
		{CLOPTION_DSIM,'d',"--dsim",1,"Path to DSIM file."},
		{CLOPTION_NTHREADS,'n',"--num-threads",1,"Number of worker threads to run on the DVN machine."},
		{CLOPTION_VERBOSE,'v',"--verbose",0,"Address-split value"},
		{CLOPTION_ADDRESS,'a',"--address",1,"Address of remote DVN host."},
		{CLOPTION_HELP,'h',"--help",0,"Usage."},
		{CLOPTION_TYPE,'t',"--type",1,"Type of frame to send. Can be: dsim, connect, shutdown"},
		{CLOPTION_CPORT,0,"--worker-port",1,"Port of worker to have master connect to."},
		{CLOPTION_CHOST,0,"--worker-host",1,"Host of worker to have master connect to."}
	 };

#define CLTYPE_NO 0
#define CLTYPE_DSIM 1
#define CLTYPE_UNKNONWN 2

int main(int argc, char * argv[]) {
	char issuance[200];
	char * data = NULL;
	socket_tp sock;
	int command;
	struct DVN_CLIENT_CONFIG dconfig;
	nbdf_tp ctl_nb, action_nb = NULL;

	/* defaults */
	memset(&dconfig, 0, sizeof(dconfig));
	dconfig.port = 10000;
	dconfig.adrsplit = 100;
	dconfig.n_threads = 2;
	strcpy(dconfig.type, "dsim");
	issuance[0] = 0;

	parse_clo(argc, argv,cloentries,(int (*)(char*,int,void*))clo_handle,(void*)&dconfig);

	if(strlen(dconfig.adr) == 0) {
		printf("Please enter a remote address to connect to.\n");
		return 1;
	}

	if(!strcmp(dconfig.type,"dsim")) {
		FILE * dsf;
		unsigned int dsim_len ;

		if(strlen(dconfig.dsim_path) == 0) {
			printf("Please give a DSIM file to send to the DVN server.\n");
			return 1;
		}

		dsf = fopen(dconfig.dsim_path, "r");
		if(!dsf) {
			printf("Unable to open DSIM file '%s'.\n", dconfig.dsim_path);
			return 1;
		}

		/* find the length of the file */
		fseek (dsf,0,SEEK_END);
		dsim_len = ftell(dsf) + 1;
		fseek (dsf,0,SEEK_SET);

		command = DVN_CFRAME_START;

		data = malloc(dsim_len + 1);
		fread(data, 1, dsim_len, dsf);
		fclose(dsf);

		data[dsim_len] = 0;
		action_nb = nbdf_construct("s", data);
		free(data);

		strcpy(issuance, "Issuing start command to DVN...");
	} else if(!strcmp(dconfig.type,"connect")) {

		action_nb = nbdf_construct("si", dconfig.worker_host, dconfig.worker_port);
		command = DVN_CFRAME_CONNECT;

		/*struct NETCTL_CONNECT * nc;

		data = malloc(sizeof(struct NETCTL_CONNECT));
		nc = (struct NETCTL_CONNECT*)data;

		strcpy(nc->host, dconfig.worker_host);
		nc->port = dconfig.worker_port;
		msghdr.command = NETCTL_CMD_CONNECT;
		msghdr.size = sizeof(struct NETCTL_CONNECT); */

		strcpy(issuance,"Issuing worker connection command to DVN...");
	} else if(!strcmp(dconfig.type, "shutdown")) {
		command = DVN_CFRAME_SHUTDOWN;
		action_nb = nbdf_construct("s", "eat me");
		strcpy(issuance, "Issuing halt command to DVN...");
	} else {
		printf("Unknown frametype. Aborting.");
		return 1;
	}

	sock = socket_create(SOCKET_OPTION_TCP);
	printf("Connecting to %s:%i...\n", dconfig.adr, dconfig.port);
	if(!socket_connect(sock, dconfig.adr, dconfig.port)) {
		printf("\tUnable to connect: %s\n", strerror(errno));
		return 1;
	}
	printf("Connected! %s\n", issuance);

	ctl_nb = nbdf_construct("iin", DVN_CPREFIX, command, action_nb);
	nbdf_send(ctl_nb, sock);

	while(socket_data_outgoing(sock) > 0)
		socket_issue_write(sock);

	nbdf_free(ctl_nb);
	nbdf_free(action_nb);

	printf("Complete.\n");

	socket_destroy(sock);

	return 0;
}
