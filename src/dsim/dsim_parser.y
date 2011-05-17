%{
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/time.h>
#include <sys/select.h>
#include <string.h>

#include "global.h"
#include "dsim_utils.h"

int yydebug=0;
extern int dsim_line_number;
extern int dsim_is_error;

%}

%union {
	char * v_string;
	double v_number;
	delement_tp v_delement;
	ptime_t v_timeval;
}

%token <v_string> IDENTIFIER
%token <v_number> NUMBER
%token QUOTE OBRACE EBRACE SEMI OPAREN EPAREN WS COMMA EQUALS
%token <v_string> QTDTEXT
%token T_EVENT T_DSIM_START T_INIT T_TRUE T_FALSE T_TIME COLON

%type <v_timeval> event_specifier
%type <v_delement> argument arguments function functions

%%

dsim: T_DSIM_START events 
	;
events: 
	| events event
	;

event: T_EVENT OPAREN event_specifier EPAREN OBRACE functions EBRACE {
			if(!dsim_finalize_operations(global_current_dsim, $6, $3)) {
				yyerror("Invalid function placement");
				YYERROR;
			}
		}
	;

event_specifier: T_TIME NUMBER COLON NUMBER {
		ptime_t rt;
		rt = $2 * 1000 + $4;
		$$ = rt; }
	;

functions: {
			$$ = NULL;
		}
	| function functions {
			$1->next = $2;
			$$ = $1;
		}
	;

function: IDENTIFIER OPAREN arguments EPAREN SEMI {
			delement_t * this_e;
			operation_tp op = dsim_create_operation($1, $3, NULL); 
			if(!op) { yyerror("Invalid function call: %s",$1); YYERROR;}
			
			this_e = malloc(sizeof(delement_t));
			if(!this_e) { yyerror("Out of memory: dsim file parsing"); YYERROR;}
			this_e->next = NULL;
			this_e->DTYPE = DT_OP;
			this_e->data = op;
			$$ = this_e;
			
			free($1);
		}
	| IDENTIFIER EQUALS IDENTIFIER OPAREN arguments EPAREN SEMI { 
			dsim_vartracker_var_tp ret_var;
			operation_tp op;
			delement_t * this_e;
			
			/* get the return value.. */
			ret_var = dsim_vartracker_createvar(global_current_dsim->vartracker, $1, NULL);
			op = dsim_create_operation($3, $5, ret_var);
			if(!op) {
				yyerror("Invalid function call: %s", $3);
				YYERROR;
			}
			
			this_e = malloc(sizeof(delement_t));
			if(!this_e) { yyerror("Out of memory: dsim file parsing"); YYERROR;}
			
			this_e->next = NULL;
			this_e->DTYPE = DT_OP;
			this_e->data = op;
			$$ = this_e;
			
			free($1); free($3);
		}
	;

arguments: argument {
			$$ = $1;
		}
	| argument COMMA arguments {
			if($1 != NULL) {
				$1->next = $3;
				$$ = $1;
			} else if ($3 != NULL) {
				$$ = $3;
			}
		}
	;

argument: NUMBER {
			delement_t * this_e = malloc(sizeof(delement_t));
			this_e->next = NULL;
			this_e->DTYPE = DT_NUMBER;
			this_e->data = malloc(sizeof(double));
			*((double*)this_e->data) = $1;
			$$ = this_e;
		}
	| QUOTE QTDTEXT QUOTE {
			delement_t * this_e = malloc(sizeof(delement_t));
			this_e->next = NULL;
			this_e->DTYPE = DT_STRING;
			this_e->data = malloc(strlen($2)+1);
			strcpy(this_e->data, $2);
			free($2);
			$$ = this_e;
		}
	| QUOTE QUOTE {
			delement_t * this_e = malloc(sizeof(delement_t));
			this_e->next = NULL;
			this_e->DTYPE = DT_STRING;
			this_e->data = malloc(1);
			strcpy(this_e->data, "");
			$$ = this_e;
		}
	| IDENTIFIER {
			delement_t * this_e = malloc(sizeof(delement_t));
			this_e->next = NULL;
			this_e->DTYPE = DT_IDEN;
			this_e->data = dsim_vartracker_findvar(global_current_dsim->vartracker, $1);
			free($1);
			if(!this_e->data) {
				yyerror("Error creating variable '%s'!", $1);
				YYERROR;
			}
			$$ = this_e;
		}
	| 	{
			$$ = NULL;
		}
	;


%%
#include<stdio.h>
#include <string.h>
#include <stdarg.h>

extern int yylineno;

int yyerror(char* msg,...)
{
	va_list arg;

	va_start(arg,msg);
	printf(" Syntax Error in Line : %d : \n",dsim_line_number);
	vprintf(msg,arg);
	va_end(arg);
	
	dsim_is_error = 1;
	
	return 0;
}

