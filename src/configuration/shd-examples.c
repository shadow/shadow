/**
 * The Shadow Simulator
 *
 * Copyright (c) 2010-2011 Rob Jansen <jansen@cs.umn.edu>
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

GString* example_getPingExampleContents() {
	return g_string_new("<plugin name=\"pingpongplugin\" path=\"libshadow-plugin-pingpong.so\" /><cdf name=\"pingpongcpu\" center=\"100000000\" width=\"20000000\" tail=\"20000000\" /><cdf name=\"clientbandwidth\" center=\"512\" width=\"300\" tail=\"100\" /><cdf name=\"serverbandwidth\" center=\"10240\" width=\"300\" tail=\"100\" /><cdf name=\"pingponglatency\" center=\"200\" width=\"40\" tail=\"20\" /><network name=\"pingpongnetwork\" latency=\"pingponglatency\" reliability=\"1.0\" /><software name=\"tcpserver\" plugin=\"pingpongplugin\" time=\"10\" arguments=\"server tcp\" /><software name=\"udpserver\" plugin=\"pingpongplugin\" time=\"10\" arguments=\"server udp\" /><software name=\"tcpclient\" plugin=\"pingpongplugin\" time=\"20\" arguments=\"client tcp tcpserver\" /><software name=\"udpclient\" plugin=\"pingpongplugin\" time=\"20\" arguments=\"client udp udpserver\" /><node name=\"tcpserver\" software=\"tcpserver\" network=\"pingpongnetwork\" bandwidthup=\"serverbandwidth\" bandwidthdown=\"serverbandwidth\" cpu=\"pingpongcpu\" /><node name=\"udpserver\" software=\"udpserver\" network=\"pingpongnetwork\" bandwidthup=\"serverbandwidth\" bandwidthdown=\"serverbandwidth\" cpu=\"pingpongcpu\" /><node name=\"tcpclient\" software=\"tcpclient\" network=\"pingpongnetwork\" bandwidthup=\"clientbandwidth\" bandwidthdown=\"clientbandwidth\" cpu=\"pingpongcpu\" /><node name=\"udpclient\" software=\"udpclient\" network=\"pingpongnetwork\" bandwidthup=\"clientbandwidth\" bandwidthdown=\"clientbandwidth\" cpu=\"pingpongcpu\" /><kill time=\"30\" />");
}

GString* example_getEchoExampleContents() {
	return g_string_new("<plugin name=\"echoplugin\" path=\"libshadow-plugin-echo.so\" /><cdf name=\"echocpu\" center=\"100000000\" width=\"20000000\" tail=\"20000000\" /><cdf name=\"echobandwidth\" center=\"10\" width=\"0\" tail=\"0\" /><cdf name=\"echolatency\" center=\"200\" width=\"40\" tail=\"20\" /><network name=\"echonetwork\" latency=\"echolatency\" reliability=\"1.0\" /><software name=\"echoserver\" plugin=\"echoplugin\" time=\"10\" arguments=\"server\" /><software name=\"echoclient\" plugin=\"echoplugin\" time=\"20\" arguments=\"client server.echo\" /><software name=\"echoloopback\" plugin=\"echoplugin\" time=\"20\" arguments=\"loopback\" /><node name=\"server.echo\" software=\"echoserver\" network=\"echonetwork\" bandwidthup=\"echobandwidth\" bandwidthdown=\"echobandwidth\" cpu=\"echocpu\" /><node name=\"client.echo\" software=\"echoclient\" network=\"echonetwork\" bandwidthup=\"echobandwidth\" bandwidthdown=\"echobandwidth\" cpu=\"echocpu\" /><node name=\"loopback.echo\" software=\"echoloopback\" network=\"echonetwork\" bandwidthup=\"echobandwidth\" bandwidthdown=\"echobandwidth\" cpu=\"echocpu\" /><kill time=\"180\" />");
}

GString* example_getFileExampleContents() {
	/* serve and download /bin/ls 10 times for each of 100 clients */
	return g_string_new("<plugin name=\"filetransfer\" path=\"libshadow-plugin-filetransfer.so\" /><cdf name=\"filecpu\" center=\"100000000\" width=\"20000000\" tail=\"20000000\" /><cdf name=\"serverbandwidth\" center=\"10240\" width=\"0\" tail=\"0\" /><cdf name=\"clientbandwidth\" center=\"1024\" width=\"0\" tail=\"0\" /><cdf name=\"filelatency\" center=\"100\" width=\"20\" tail=\"20\" /><network name=\"filenetwork\" latency=\"filelatency\" reliability=\"1.0\" /><software name=\"fileserver\" plugin=\"filetransfer\" time=\"10\" arguments=\"server 8080 /bin/\" /><software name=\"fileclient\" plugin=\"filetransfer\" time=\"20\" arguments=\"client single server.filetransfer 8080 none 0 10 /ls\" /><node name=\"server.filetransfer\" software=\"fileserver\" network=\"filenetwork\" bandwidthup=\"serverbandwidth\" bandwidthdown=\"serverbandwidth\" cpu=\"filecpu\" /><node name=\"client.filetransfer\" quantity=\"100\" software=\"fileclient\" network=\"filenetwork\" bandwidthup=\"clientbandwidth\" bandwidthdown=\"clientbandwidth\" cpu=\"filecpu\" /><kill time=\"600\" />");
}
