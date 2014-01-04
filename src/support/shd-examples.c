/*
 * The Shadow Simulator
 * Copyright (c) 2010-2011, Rob Jansen
 * See LICENSE for licensing information
 */

#include "shadow.h"


GString* example_getFileExampleContents() {
	/* serve and download /bin/ls 10 times for each of 1000 clients */
	return g_string_new("\
		<shadow>\
		<topology>\
		<![CDATA[<?xml version=\"1.0\" encoding=\"utf-8\"?><graphml xmlns=\"http://graphml.graphdrawing.org/xmlns\" xmlns:xsi=\"http://www.w3.org/2001/XMLSchema-instance\" xsi:schemaLocation=\"http://graphml.graphdrawing.org/xmlns http://graphml.graphdrawing.org/xmlns/1.0/graphml.xsd\">\
		  <key attr.name=\"packetloss\" attr.type=\"double\" for=\"edge\" id=\"d9\" />\
		  <key attr.name=\"jitter\" attr.type=\"double\" for=\"edge\" id=\"d8\" />\
		  <key attr.name=\"latency\" attr.type=\"double\" for=\"edge\" id=\"d7\" />\
		  <key attr.name=\"asn\" attr.type=\"int\" for=\"node\" id=\"d6\" />\
		  <key attr.name=\"type\" attr.type=\"string\" for=\"node\" id=\"d5\" />\
		  <key attr.name=\"bandwidthup\" attr.type=\"int\" for=\"node\" id=\"d4\" />\
		  <key attr.name=\"bandwidthdown\" attr.type=\"int\" for=\"node\" id=\"d3\" />\
		  <key attr.name=\"geocode\" attr.type=\"string\" for=\"node\" id=\"d2\" />\
		  <key attr.name=\"ip\" attr.type=\"string\" for=\"node\" id=\"d1\" />\
		  <key attr.name=\"packetloss\" attr.type=\"double\" for=\"node\" id=\"d0\" />\
		  <graph edgedefault=\"undirected\">\
			<node id=\"poi-1\">\
			  <data key=\"d0\">0.0</data>\
			  <data key=\"d1\">0.0.0.0</data>\
			  <data key=\"d2\">US</data>\
			  <data key=\"d3\">17038</data>\
			  <data key=\"d4\">2251</data>\
			  <data key=\"d5\">net</data>\
			  <data key=\"d6\">0</data>\
			</node>\
			<edge source=\"poi-1\" target=\"poi-1\">\
			  <data key=\"d7\">50.0</data>\
			  <data key=\"d8\">0.0</data>\
			  <data key=\"d9\">0.05</data>\
			</edge>\
		  </graph>\
		</graphml>]]>\
		</topology>\
		<plugin id=\"filex\" path=\"libshadow-plugin-filetransfer.so\" />\
		<node id=\"server\" geocodehint=\"US\" bandwidthup=\"10240\" bandwidthdown=\"5120\" >\
			<application plugin=\"filex\" time=\"10\" arguments=\"server 8080 /bin/\" />\
		</node >\
		<node id=\"client\" quantity=\"1000\" >\
			<application plugin=\"filex\" time=\"20\" arguments=\"client single server 8080 none 0 10 /ls\" />\
		</node >\
		<kill time=\"300\" />\
		</shadow>");
}
