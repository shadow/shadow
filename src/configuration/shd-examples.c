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
		"<cluster id=\"net0\" bandwidthdown=\"1024\" bandwidthup=\"512\" packetloss=\"0.0\"/>"
		"<link id=\"link0\" clusters=\"net0 net0\" latency=\"50\" jitter=\"10\"/>"
		"<cluster id=\"net1\" bandwidthdown=\"1024\" bandwidthup=\"512\" packetloss=\"0.5\"/>"
		"<link id=\"link1\" clusters=\"net1 net1\" latency=\"50\" jitter=\"40\"/>"
		"<software id=\"echoudpserver\" plugin=\"echoplugin\" time=\"10\" arguments=\"udp server\" />"
		"<software id=\"reliableechoudpclient\" plugin=\"echoplugin\" time=\"20\" arguments=\"udp client reliable.udpserver.echo\" />"
		"<software id=\"echoudploopback\" plugin=\"echoplugin\" time=\"20\" arguments=\"udp loopback\" />"
		"<software id=\"echotcpserver\" plugin=\"echoplugin\" time=\"10\" arguments=\"tcp server\" />"
		"<software id=\"reliableechotcpclient\" plugin=\"echoplugin\" time=\"20\" arguments=\"tcp client reliable.tcpserver.echo\" />"
		"<software id=\"unreliableechotcpclient\" plugin=\"echoplugin\" time=\"20\" arguments=\"tcp client unreliable.tcpserver.echo\" />"
		"<software id=\"echotcploopback\" plugin=\"echoplugin\" time=\"20\" arguments=\"tcp loopback\" />"
		"<software id=\"echosocketpair\" plugin=\"echoplugin\" time=\"20\" arguments=\"tcp socketpair\" />"
		"<software id=\"echopipe\" plugin=\"echoplugin\" time=\"20\" arguments=\"pipe\" />"
		"<node id=\"reliable.udpserver.echo\" software=\"echoudpserver\" cluster=\"net0\" />"
		"<node id=\"reliable.udpclient.echo\" software=\"reliableechoudpclient\" cluster=\"net0\" />"
		"<node id=\"reliable.udploopback.echo\" software=\"echoudploopback\" cluster=\"net0\" />"
		"<node id=\"reliable.tcpserver.echo\" software=\"echotcpserver\" cluster=\"net0\" />"
		"<node id=\"reliable.tcpclient.echo\" software=\"reliableechotcpclient\" cluster=\"net0\" />"
		"<node id=\"reliable.tcploopback.echo\" software=\"echotcploopback\" cluster=\"net0\" />"
		"<node id=\"unreliable.tcpserver.echo\" software=\"echotcpserver\" cluster=\"net1\" />"
		"<node id=\"unreliable.tcpclient.echo\" software=\"unreliableechotcpclient\" cluster=\"net1\" />"
		"<node id=\"unreliable.tcploopback.echo\" software=\"echotcploopback\" cluster=\"net1\" />"
		"<node id=\"socketpair.echo\" software=\"echosocketpair\" cluster=\"net0\" />"
		"<node id=\"pipe.echo\" software=\"echopipe\" cluster=\"net0\" />"
		"<kill time=\"180\" />");
}

GString* example_getFileExampleContents() {
	/* serve and download /bin/ls 10 times for each of 100 clients */
	return g_string_new(
		"<plugin id=\"filex\" path=\"libshadow-plugin-filetransfer.so\" />"
		"<cluster id=\"net0\" bandwidthdown=\"1024\" bandwidthup=\"512\" packetloss=\"0.005\"/>"
		"<link id=\"link0\" clusters=\"net0 net0\" latency=\"50\" jitter=\"10\"/>"
		"<software id=\"fileserver\" plugin=\"filex\" time=\"10\" arguments=\"server 8080 /bin/\" />"
		"<software id=\"fileclient\" plugin=\"filex\" time=\"20\" arguments=\"client single server.filetransfer 8080 none 0 10 /ls\" />"
		"<node id=\"server.filetransfer\" software=\"fileserver\" cluster=\"net0\" bandwidthup=\"10240\" bandwidthdown=\"5120\" />"
		"<node id=\"client.filetransfer\" quantity=\"1000\" software=\"fileclient\" />"
		"<kill time=\"300\" />");
}

GString* example_getTorrentExampleContents() {
	/* start a P2P torrent download with 10 clients sharing an 8MB file */
	return g_string_new(
		"<plugin id=\"torrent\" path=\"libshadow-plugin-torrent.so\" />"
		"<cluster id=\"net0\" bandwidthdown=\"1024\" bandwidthup=\"512\" packetloss=\"0.005\"/>"
		"<link id=\"link0\" clusters=\"net0 net0\" latency=\"50\" jitter=\"10\"/>"
		"<software id=\"torrentauth\" plugin=\"torrent\" time=\"10\" arguments=\"authority 5000\" />"
		"<software id=\"torrentnode\" plugin=\"torrent\" time=\"20\" arguments=\"node auth.torrent 5000 none 0 6000 8MB\" />"
		"<node id=\"auth.torrent\" software=\"torrentauth\" cluster=\"net0\" bandwidthup=\"10240\" bandwidthdown=\"5120\" />"
		"<node id=\"node.torrent\" quantity=\"10\" software=\"torrentnode\" />"
		"<kill time=\"300\" />");
}

GString* example_getBrowserExampleContents() {
	/* start a server and simulate a browser downloading index.htm */
	return g_string_new(
		"<plugin id=\"filex\" path=\"libshadow-plugin-filetransfer.so\" />"
		"<plugin id=\"browser\" path=\"libshadow-plugin-browser.so\" />"
		"<cluster id=\"net0\" bandwidthdown=\"1024\" bandwidthup=\"512\" packetloss=\"0.005\"/>"
		"<link id=\"link0\" clusters=\"net0 net0\" latency=\"50\" jitter=\"10\"/>"
		"<software id=\"server\" plugin=\"filex\" time=\"10\" arguments=\"server 80 ./resource/browser-example/\" />"
		"<software id=\"browser\" plugin=\"browser\" time=\"20\" arguments=\"server.node 80 none 0 6 /index.htm\" />"
		"<node id=\"server.node\" software=\"server\" cluster=\"net0\" bandwidthup=\"10240\" bandwidthdown=\"5120\" />"
		"<node id=\"browser.node\"software=\"browser\" cluster=\"net0\"  bandwidthup=\"10240\" bandwidthdown=\"5120\" />"
		"<kill time=\"300\" />");
}
