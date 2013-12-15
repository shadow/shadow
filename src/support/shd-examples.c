/*
 * The Shadow Simulator
 * Copyright (c) 2010-2011, Rob Jansen
 * See LICENSE for licensing information
 */

#include "shadow.h"

GString* example_getFileExampleContents() {
	/* serve and download /bin/ls 10 times for each of 100 clients */
	return g_string_new(
		"<shadow>"
		"<topology path=\"~/.shadow/share/topology.graphml.xml\" />"
		"<plugin id=\"filex\" path=\"libshadow-plugin-filetransfer.so\" />"
		"<node id=\"server\" geocodehint=\"US\" bandwidthup=\"10240\" bandwidthdown=\"5120\" >"
		"	<application plugin=\"filex\" time=\"10\" arguments=\"server 8080 /bin/\" />"
		"</node >"
		"<node id=\"client\" quantity=\"1000\" >"
		"	<application plugin=\"filex\" time=\"20\" arguments=\"client single server 8080 none 0 10 /ls\" />"
		"</node >"
		"<kill time=\"300\" />"
		"</shadow>");
}
