On Linux, you cannot read any received bytes once the socket has received an
RST packet.

> Upon reception of RST segment, the receiving side will immediately abort the
> connection. This statement has more implications than just meaning that you
> will not be able to receive or send any more data to/from this connection. It
> also implies that any unread data still in the TCP reception buffer will be
> lost. This information can be found in TCP/IP Internetworking volume 2 and
> Unix Network Programming Volume 1 third edition but in my opinion those books
> do not put enough emphasis on that detail and if you are not reading these
> books to find this exact detail, it might slip away from your attention.

<http://blog.olivierlanglois.net/index.php/2010/02/06/tcp_rst_flag_subtleties>
