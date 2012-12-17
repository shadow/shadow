/**
 * The Shadow Simulator
 *
 * Copyright (c) 2010-2012 Rob Jansen <jansen@cs.umn.edu>
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

#include "shadow.h"

GString* example_getEchoExampleContents() {
	return g_string_new(
		"<plugin id=\"echoplugin\" path=\"libshadow-plugin-echo.so\" />"
		"<cluster id=\"net0\" bandwidthdown=\"1024\" bandwidthup=\"512\" packetloss=\"0.0\" >"
		"	<link cluster=\"net0\" latency=\"50\" jitter=\"10\"/>"
		"</cluster >"
		"<cluster id=\"net1\" bandwidthdown=\"1024\" bandwidthup=\"512\" packetloss=\"0.5\" >"
		"	<link cluster=\"net1\" latency=\"50\" jitter=\"40\"/>"
		"</cluster >"
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
		"<cluster id=\"net0\" bandwidthdown=\"1024\" bandwidthup=\"512\" packetloss=\"0.005\" >"
		"	<link cluster=\"net0\" latency=\"50\" jitter=\"10\"/>"
		"</cluster >"
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
		"<cluster id=\"net0\" bandwidthdown=\"1024\" bandwidthup=\"512\" packetloss=\"0.005\" >"
		"	<link cluster=\"net0\" latency=\"50\" jitter=\"10\"/>"
		"</cluster >"
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
		"<cluster id=\"net0\" bandwidthdown=\"1024\" bandwidthup=\"512\" packetloss=\"0.005\" >"
		"	<link cluster=\"net0\" latency=\"50\" jitter=\"10\"/>"
		"</cluster >"
		"<node id=\"webserver\" cluster=\"net0\" bandwidthup=\"10240\" bandwidthdown=\"5120\" >"
		"	<application plugin=\"filex\" time=\"10\" arguments=\"server 80 ./resource/browser-example/\" />"
		"</node >"
		"<node id=\"browserclient\" cluster=\"net0\" bandwidthup=\"10240\" bandwidthdown=\"5120\" >"
		"	<application plugin=\"browser\" time=\"20\" arguments=\"webserver 80 none 0 6 /index.htm\" />"
		"</node >"
		"<kill time=\"300\" />");
}
