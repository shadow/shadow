%{
#include <stdio.h>
#include <string.h>
#include <sys/time.h>
#include <stdlib.h>

#include "global.h"
#include "dsim_utils.h"
#include "dsim_parser.tab.h"

int dsim_line_number = 0;
int dsim_is_error = 0;
char * _dsim_curbuf_input;

#define YY_INPUT(buf,result,max_size) \
    { \
	    int cs = max_size, i=0; \
    	while(cs && *_dsim_curbuf_input) {\
    		buf[i++] = *(_dsim_curbuf_input++); \
    		cs--; \
    	} \
	    result = (i == 0) ? YY_NULL : i; \
    }
%}


%s qtd comment

%%
true			return T_TRUE;
false			return T_FALSE;
time			return T_TIME;
on				return T_EVENT;
\?DSIM3			{ return T_DSIM_START; }
<qtd>[^\"]*		{ yylval.v_string = strdup(yytext); return QTDTEXT; } 
<qtd>[\"]		{ BEGIN INITIAL; return QUOTE; }
[\"]			{ BEGIN qtd; return QUOTE; }
\(				return OPAREN;
\)				return EPAREN;
[a-zA-Z_][a-zA-Z0-9_]*		{ yylval.v_string = strdup(yytext); return IDENTIFIER; }
[0-9]+(\.[0-9]+)?	{ yylval.v_number = atof(yytext); return NUMBER; }
\{				return OBRACE;
\}				return EBRACE;
,				return COMMA;
;				return SEMI;
:				return COLON;
=				return EQUALS;
#				{ BEGIN comment; }
<comment>[^\n]*	/* ignore */
<comment>\n		{ dsim_line_number++; BEGIN INITIAL; }
\n				{ dsim_line_number++; } /* ignore */
[ \t\r]+		/* ignore */
%%
int yywrap(void)
{
	return 1;
}
