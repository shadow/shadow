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

#include <glib.h>
#include <string.h>
#include <stdio.h>

#include "global.h"
#include "clo.h"

gint parse_clo(gint argc, gchar * argv[], struct CLO_entry cloentries[], gint (*clo_handler)(gchar*,gint,gpointer ), gpointer v) {
	gint i,j,k;
	gchar so[3] = "--";
	j = argc-1;
	for(j=1; j<argc; j++) {
		for(i=0; cloentries[i].id; i++){
			so[1] = cloentries[i].option;
			if(!strcmp(so,argv[j]) || !strcmp(cloentries[i].fulloption,argv[j])) {
				if(cloentries[i].transitive) {
					if(argv[j+1] == NULL) {
						printf("Must supply parameter to '%s'.\n",argv[j]);
						i = -1;
						break;
					} else
						k = (*clo_handler)(argv[++j],cloentries[i].id,v);
				} else
					k = (*clo_handler)(NULL,cloentries[i].id,v);

				if( k == CLO_BAD ) {
					printf("Invalid/Unknown parameters for option '%s'.\n", argv[j]);
					i = -1;
				} else if( k == CLO_USAGE ) {
					printf("Usage:\n");
					for(k = 0; cloentries[k].id; k++) {
						if(cloentries[k].option != 0) {
							if(cloentries[k].transitive)
								printf("-%c (%s) [value]: %s\n", cloentries[k].option, cloentries[k].fulloption, cloentries[k].desc);
							else
								printf("-%c (%s): %s\n", cloentries[k].option, cloentries[k].fulloption, cloentries[k].desc);
						} else {
							if(cloentries[k].transitive)
								printf("%s [value]: %s\n", cloentries[k].fulloption, cloentries[k].desc);
							else
								printf("%s: %s\n",  cloentries[k].fulloption, cloentries[k].desc);
						}
					}

					i = -1;
				}

				break;
			}
		}
		if(!cloentries[i].id) {
			printf("Unknown option '%s'.\n\n",argv[j]);
			return 0;
		} else if( i < 0)
			return 0;
	}

	return 1;
}
