/*
 * The Shadow Simulator
 * See LICENSE for licensing information
 */

#include "main/utility/pcap_writer.h"

#include <stdio.h>

#include "main/core/logger/logger.h"
#include "main/core/support/definitions.h"
#include "main/core/worker.h"
#include "main/host/host.h"

struct _PCapWriter {
    FILE *pcapFile;
};

static void _pcapwriter_writeHeader(PCapWriter* pcap) {
    guint32 magic_number;   /* magic number */
    guint16 version_major;  /* major version number */
    guint16 version_minor;  /* minor version number */
    gint32  thiszone;       /* GMT to local correction */
    guint32 sigfigs;        /* accuracy of timestamps */
    guint32 snaplen;        /* max length of captured packets, in octets */
    guint32 network;        /* data link type */

    magic_number = 0xA1B2C3D4;
    version_major = 2;
    version_minor = 4;
    thiszone = 0;
    sigfigs = 0;
    snaplen = 65535;
    network = 1;

    fwrite(&magic_number, 1, sizeof(magic_number), pcap->pcapFile);
    fwrite(&version_major, 1, sizeof(version_major), pcap->pcapFile);
    fwrite(&version_minor, 1, sizeof(version_minor), pcap->pcapFile);
    fwrite(&thiszone, 1, sizeof(thiszone), pcap->pcapFile);
    fwrite(&sigfigs, 1, sizeof(sigfigs), pcap->pcapFile);
    fwrite(&snaplen, 1, sizeof(snaplen), pcap->pcapFile);
    fwrite(&network, 1, sizeof(network), pcap->pcapFile);
}

void pcapwriter_writePacket(PCapWriter* pcap, PCapPacket* packet) {
    if(!pcap || !pcap->pcapFile || !packet) {
        return;
    }

    guint32 ts_sec;         /* timestamp seconds */
    guint32 ts_usec;        /* timestamp microseconds */
    guint32 incl_len;       /* number of octets of packet saved in file */
    guint32 orig_len;       /* actual length of packet */

    /* get the current time that the packet is being sent/received */
    SimulationTime now = worker_getCurrentTime();
    ts_sec = now / SIMTIME_ONE_SECOND;
    ts_usec = (now % SIMTIME_ONE_SECOND) / SIMTIME_ONE_MICROSECOND;

    /* get the header and payload lengths */
    guint headerSize = packet->headerSize;
    guint payloadLength = packet->payloadLength;
    incl_len = headerSize + payloadLength;
    orig_len = headerSize + payloadLength;

    /* write the PCAP packet header to the pcap file */
    fwrite(&ts_sec, sizeof(ts_sec), 1, pcap->pcapFile);
    fwrite(&ts_usec, sizeof(ts_usec), 1, pcap->pcapFile);
    fwrite(&incl_len, sizeof(incl_len), 1, pcap->pcapFile);
    fwrite(&orig_len, sizeof(orig_len), 1, pcap->pcapFile);

    /* write the ethernet header */
    guint8 destinationMAC[6] = {0x01, 0x23, 0x45, 0x67, 0x89, 0xAB};
    guint8 sourceMAC[6] = {0xA1, 0xB2, 0xC3, 0xD4, 0xE5, 0xF6};
    guint16 type = htons(0x0800);

    fwrite(destinationMAC, 1, sizeof(destinationMAC), pcap->pcapFile);
    fwrite(sourceMAC, 1, sizeof(sourceMAC), pcap->pcapFile);
    fwrite(&type, 1, sizeof(type), pcap->pcapFile);

    /* write the IP header */
    guint8 versionAndHeaderLength = 0x45;
    guint8 fields = 0x00;
    guint16 totalLength = htons(orig_len - 14);
    guint16 identification = 0x0000;
    guint16 flagsAndFragment = 0x0040;
    guint8 timeToLive = 64;
    guint8 protocol = 6;  /* TCP */
    guint16 headerChecksum = 0x0000;
    guint32 sourceIP = packet->srcIP;
    guint32 destinationIP = packet->dstIP;

    fwrite(&versionAndHeaderLength, 1, sizeof(versionAndHeaderLength), pcap->pcapFile);
    fwrite(&fields, 1, sizeof(fields), pcap->pcapFile);
    fwrite(&totalLength, 1, sizeof(totalLength), pcap->pcapFile);
    fwrite(&identification, 1, sizeof(identification), pcap->pcapFile);
    fwrite(&flagsAndFragment, 1, sizeof(flagsAndFragment), pcap->pcapFile);
    fwrite(&timeToLive, 1, sizeof(timeToLive), pcap->pcapFile);
    fwrite(&protocol, 1, sizeof(protocol), pcap->pcapFile);
    fwrite(&headerChecksum, 1, sizeof(headerChecksum), pcap->pcapFile);
    fwrite(&sourceIP, 1, sizeof(sourceIP), pcap->pcapFile);
    fwrite(&destinationIP, 1, sizeof(destinationIP), pcap->pcapFile);


    /* write the TCP header */
    guint16 sourcePort = packet->srcPort;
    guint16 destinationPort = packet->dstPort;
    guint32 sequence = htonl(packet->seq);
    guint32 acknowledgement = 0;
    if(packet->ackFlag) {
        acknowledgement = htonl(packet->ack);
    }
    guint8 headerLength = 0x80;
    guint8 tcpFlags = 0;
    if(packet->rstFlag) tcpFlags |= 0x04;
    if(packet->synFlag) tcpFlags |= 0x02;
    if(packet->ackFlag) tcpFlags |= 0x10;
    if(packet->finFlag) tcpFlags |= 0x01;
    guint16 window = htons(packet->win);
    guint16 tcpChecksum = 0x0000;
    guint8 options[14] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};

    fwrite(&sourcePort, 1, sizeof(sourcePort), pcap->pcapFile);
    fwrite(&destinationPort, 1, sizeof(destinationPort), pcap->pcapFile);
    fwrite(&sequence, 1, sizeof(sequence), pcap->pcapFile);
    fwrite(&acknowledgement, 1, sizeof(acknowledgement), pcap->pcapFile);
    fwrite(&headerLength, 1, sizeof(headerLength), pcap->pcapFile);
    fwrite(&tcpFlags, 1, sizeof(tcpFlags), pcap->pcapFile);
    fwrite(&window, 1, sizeof(window), pcap->pcapFile);
    fwrite(&tcpChecksum, 1, sizeof(tcpChecksum), pcap->pcapFile);
    fwrite(options, 1, sizeof(options), pcap->pcapFile);

    /* write payload data */
    if(payloadLength > 0 && packet->payload) {
        fwrite(packet->payload, 1, payloadLength, pcap->pcapFile);
    }
}

PCapWriter* pcapwriter_new(gchar* pcapDirectory, gchar* pcapFilename) {
    PCapWriter* pcap = g_new0(PCapWriter, 1);

    /* open the PCAP file for writing */
    GString *filename = g_string_new("");
    if (pcapDirectory) {
        g_string_append(filename, pcapDirectory);
        /* Append trailing slash if not present */
        if (!g_str_has_suffix(pcapDirectory, "/")) {
            g_string_append(filename, "/");
        }
    } else {
        /* Use default directory */
        g_string_append(filename, "data/pcapdata/");
    }

    if(pcapFilename) {
        g_string_append_printf(filename, "%s", pcapFilename);
    } else {
        g_string_append_printf(filename, "%s", host_getName(worker_getActiveHost()));
    }

    if (!g_str_has_suffix(filename->str, ".pcap")) {
        g_string_append(filename, ".pcap");
    }

    pcap->pcapFile = fopen(filename->str, "w");
    if(!pcap->pcapFile) {
        warning("error trying to open PCAP file '%s' for writing", filename->str);
    } else {
        _pcapwriter_writeHeader(pcap);
    }

    return pcap;
}

void pcapwriter_free(PCapWriter* pcap) {
    if(pcap && pcap->pcapFile) {
        fclose(pcap->pcapFile);
    }
}
