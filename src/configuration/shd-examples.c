/*
 * The Shadow Simulator
 * Copyright (c) 2010-2011, Rob Jansen
 * See LICENSE for licensing information
 */

#include "shadow.h"

GString* example_getEchoExampleContents() {
	return g_string_new(
		"<plugin id=\"echoplugin\" path=\"libshadow-plugin-echo.so\" />"
		"<cluster id=\"net0\" bandwidthdown=\"1024\" bandwidthup=\"512\" packetloss=\"0.0\" />"
		"<cluster id=\"net1\" bandwidthdown=\"1024\" bandwidthup=\"512\" packetloss=\"0.5\" />"
		"<link clusters=\"net0 net0\" latency=\"50\" jitter=\"10\"/>"
		"<link clusters=\"net1 net1\" latency=\"50\" jitter=\"40\"/>"
		"<node id=\"reliable.udpserver.echo\" cluster=\"net0\" >"
		"	<application plugin=\"echoplugin\" time=\"10\" arguments=\"udp server\" />"
		"</node >"
		"<node id=\"reliable.udpclient.echo\" cluster=\"net0\" >"
		"	<application plugin=\"echoplugin\" time=\"20\" arguments=\"udp client reliable.udpserver.echo\" />"
		"</node >"
		"<node id=\"reliable.udploopback.echo\" cluster=\"net0\" >"
		"	<application plugin=\"echoplugin\" time=\"20\" arguments=\"udp loopback\" />"
		"</node >"
		"<node id=\"reliable.tcpserver.echo\"cluster=\"net0\" >"
		"	<application plugin=\"echoplugin\" time=\"10\" arguments=\"tcp server\" />"
		"</node >"
		"<node id=\"reliable.tcpclient.echo\" cluster=\"net0\" >"
		"	<application plugin=\"echoplugin\" time=\"20\" arguments=\"tcp client reliable.tcpserver.echo\" />"
		"</node >"
		"<node id=\"reliable.tcploopback.echo\" cluster=\"net0\" >"
		"	<application plugin=\"echoplugin\" time=\"20\" arguments=\"tcp loopback\" />"
		"</node >"
		"<node id=\"unreliable.tcpserver.echo\" cluster=\"net1\" >"
		"	<application plugin=\"echoplugin\" time=\"10\" arguments=\"tcp server\" />"
		"</node >"
		"<node id=\"unreliable.tcpclient.echo\" cluster=\"net1\" >"
		"	<application plugin=\"echoplugin\" time=\"20\" arguments=\"tcp client unreliable.tcpserver.echo\" />"
		"</node >"
		"<node id=\"unreliable.tcploopback.echo\" cluster=\"net1\" >"
		"	<application plugin=\"echoplugin\" time=\"20\" arguments=\"tcp loopback\" />"
		"</node >"
		"<node id=\"socketpair.echo\" cluster=\"net0\" >"
		"	<application plugin=\"echoplugin\" time=\"20\" arguments=\"tcp socketpair\" />"
		"</node >"
		"<node id=\"pipe.echo\" cluster=\"net0\" >"
		"	<application plugin=\"echoplugin\" time=\"20\" arguments=\"pipe\" />"
		"</node >"
		"<kill time=\"180\" />");
}

GString* example_getFileExampleContents() {
	/* serve and download /bin/ls 10 times for each of 100 clients */
	return g_string_new(
		"<plugin id=\"filex\" path=\"libshadow-plugin-filetransfer.so\" />"
		"<cluster id=\"net0\" bandwidthdown=\"1024\" bandwidthup=\"512\" packetloss=\"0.005\" />"
		"<link clusters=\"net0 net0\" latency=\"50\" jitter=\"10\"/>"
		"<node id=\"fileserver\" cluster=\"net0\" bandwidthup=\"10240\" bandwidthdown=\"5120\" >"
		"	<application plugin=\"filex\" time=\"10\" arguments=\"server 8080 /bin/\" />"
		"</node >"
		"<node id=\"fileclient\" quantity=\"1000\" >"
		"	<application plugin=\"filex\" time=\"20\" arguments=\"client single fileserver 8080 none 0 10 /ls\" />"
		"</node >"
		"<kill time=\"300\" />");
}

GString* example_getTorrentExampleContents() {
	/* start a P2P torrent download with 10 clients sharing an 8MB file */
	return g_string_new(
		"<plugin id=\"torrent\" path=\"libshadow-plugin-torrent.so\" />"
		"<cluster id=\"net0\" bandwidthdown=\"1024\" bandwidthup=\"512\" packetloss=\"0.005\" />"
		"<link clusters=\"net0 net0\" latency=\"50\" jitter=\"10\"/>"
		"<node id=\"auth.torrent\" cluster=\"net0\" bandwidthup=\"10240\" bandwidthdown=\"5120\" >"
		"	<application plugin=\"torrent\" time=\"10\" arguments=\"authority 5000\" />"
		"</node >"
		"<node id=\"node.torrent\" quantity=\"10\" >"
		"	<application plugin=\"torrent\" time=\"20\" arguments=\"node auth.torrent 5000 none 0 6000 8MB\" />"
		"</node >"
		"<kill time=\"300\" />");
}

GString* example_getBrowserExampleContents() {
	/* start a server and simulate a browser downloading index.htm */
	return g_string_new(
		"<plugin id=\"filex\" path=\"libshadow-plugin-filetransfer.so\" />"
		"<plugin id=\"browser\" path=\"libshadow-plugin-browser.so\" />"
		"<cluster id=\"net0\" bandwidthdown=\"1024\" bandwidthup=\"512\" packetloss=\"0.005\" />"
		"<link clusters=\"net0 net0\" latency=\"50\" jitter=\"10\"/>"
		"<node id=\"webserver\" cluster=\"net0\" bandwidthup=\"10240\" bandwidthdown=\"5120\" >"
		"	<application plugin=\"filex\" time=\"10\" arguments=\"server 80 ./resource/browser-example/\" />"
		"</node >"
		"<node id=\"browserclient\" cluster=\"net0\" bandwidthup=\"10240\" bandwidthdown=\"5120\" >"
		"	<application plugin=\"browser\" time=\"20\" arguments=\"webserver 80 none 0 6 /index.htm\" />"
		"</node >"
		"<kill time=\"300\" />");
}
