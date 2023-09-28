## Behaviour of `shutdown()`

The following examples expect tcpdump to be running in the background.

```bash
sudo tcpdump -i lo -nn 'tcp and port 8000'
```

On Linux, `close()` is meant for closing a socket's file handle and
`shutdown()` is meant for closing the socket. Linux also automatically closes
the socket if the last remaining file handle for a socket is `close()`d.

### Closing a socket handle while there is data in the receive buffer

When a socket's file handle is close()d, a FIN is typically sent to the peer to
shutdown the connection. This is generally not the case if the socket has
unread data in its receive buffer when the socket's file handle is close()d, in
which case a RST is sent instead and the socket is immediately closed.

In this example we send data from s1 to s2, then close() s2 before reading that
data. A RST is sent.

```python3
import socket, time
s1 = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
server = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
server.bind(('127.0.0.20', 8000))
server.listen()
s1.connect(server.getsockname())
(s2, _) = server.accept()

s1.send(b'hello')

time.sleep(2)
s2.close()
```

```text
21:19:48.621975 IP 127.0.0.1.48072 > 127.0.0.20.8000: Flags [S], seq 1907337844, win 65495, options [mss 65495,sackOK,TS val 3465232499 ecr 0,nop,wscale 7], length 0
21:19:48.621989 IP 127.0.0.20.8000 > 127.0.0.1.48072: Flags [S.], seq 3313405637, ack 1907337845, win 65483, options [mss 65495,sackOK,TS val 3947160461 ecr 3465232499,nop,wscale 7], length 0
21:19:48.622001 IP 127.0.0.1.48072 > 127.0.0.20.8000: Flags [.], ack 1, win 512, options [nop,nop,TS val 3465232499 ecr 3947160461], length 0
21:19:48.622260 IP 127.0.0.1.48072 > 127.0.0.20.8000: Flags [P.], seq 1:6, ack 1, win 512, options [nop,nop,TS val 3465232499 ecr 3947160461], length 5
21:19:48.622267 IP 127.0.0.20.8000 > 127.0.0.1.48072: Flags [.], ack 6, win 512, options [nop,nop,TS val 3947160461 ecr 3465232499], length 0
21:19:50.625109 IP 127.0.0.20.8000 > 127.0.0.1.48072: Flags [R.], seq 1, ack 6, win 512, options [nop,nop,TS val 3947162464 ecr 3465232499], length 0
```

### Shutting down (`SHUT_RDWR`) a socket handle while there is data in the receive buffer

This behaviour is slightly different if the sockets are shutdown() instead
before being close()d.

In this example we send data from s1 to s2, shutdown(RDWR) s2, then close s2
before reading that data. A FIN is sent from s2 to s1 during the shutdown() and
a RST is sent from s2 to s1 during the close().

```python3
import socket, time
s1 = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
server = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
server.bind(('127.0.0.20', 8000))
server.listen()
s1.connect(server.getsockname())
(s2, _) = server.accept()

s1.send(b'hello')
s2.shutdown(socket.SHUT_RDWR)

time.sleep(2)
s2.close()
```

```text
20:23:18.401987 IP 127.0.0.1.42276 > 127.0.0.20.8000: Flags [S], seq 2497483556, win 65495, options [mss 65495,sackOK,TS val 3461842279 ecr 0,nop,wscale 7], length 0
20:23:18.402005 IP 127.0.0.20.8000 > 127.0.0.1.42276: Flags [S.], seq 2697381411, ack 2497483557, win 65483, options [mss 65495,sackOK,TS val 3943770241 ecr 3461842279,nop,wscale 7], length 0
20:23:18.402017 IP 127.0.0.1.42276 > 127.0.0.20.8000: Flags [.], ack 1, win 512, options [nop,nop,TS val 3461842279 ecr 3943770241], length 0
20:23:18.402247 IP 127.0.0.1.42276 > 127.0.0.20.8000: Flags [P.], seq 1:6, ack 1, win 512, options [nop,nop,TS val 3461842279 ecr 3943770241], length 5
20:23:18.402254 IP 127.0.0.20.8000 > 127.0.0.1.42276: Flags [.], ack 6, win 512, options [nop,nop,TS val 3943770241 ecr 3461842279], length 0
20:23:18.402379 IP 127.0.0.20.8000 > 127.0.0.1.42276: Flags [F.], seq 1, ack 6, win 512, options [nop,nop,TS val 3943770241 ecr 3461842279], length 0
20:23:18.404996 IP 127.0.0.1.42276 > 127.0.0.20.8000: Flags [.], ack 2, win 512, options [nop,nop,TS val 3461842282 ecr 3943770241], length 0
20:23:20.405084 IP 127.0.0.20.8000 > 127.0.0.1.42276: Flags [R.], seq 2, ack 6, win 512, options [nop,nop,TS val 3943772244 ecr 3461842282], length 0
```

This behaviour is different if both sockets have been shutdown(WR/RDWR) before
the receiving socket is close()d. In this case no RST is sent.

In this example we send data from s1 to s2, shutdown(RDWR) both s2 and s1, then
close s2 before reading that data. Both s1 and s2 send a FIN during their
respective shutdown()s, and no RST is sent even though s2 has unread data in
its receive buffer. The behaviour is the same if only `SHUT_WR` is used instead
of `SHUT_RDWR`.

```python3
import socket, time
s1 = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
server = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
server.bind(('127.0.0.20', 8000))
server.listen()
s1.connect(server.getsockname())
(s2, _) = server.accept()

s1.send(b'hello')
s2.shutdown(socket.SHUT_RDWR)
s1.shutdown(socket.SHUT_RDWR)

time.sleep(2)
s2.close()
```

```text
21:27:56.162525 IP 127.0.0.1.37594 > 127.0.0.20.8000: Flags [S], seq 3796504000, win 65495, options [mss 65495,sackOK,TS val 3465720039 ecr 0,nop,wscale 7], length 0
21:27:56.162562 IP 127.0.0.20.8000 > 127.0.0.1.37594: Flags [S.], seq 2197987041, ack 3796504001, win 65483, options [mss 65495,sackOK,TS val 3947648001 ecr 3465720039,nop,wscale 7], length 0
21:27:56.162595 IP 127.0.0.1.37594 > 127.0.0.20.8000: Flags [.], ack 1, win 512, options [nop,nop,TS val 3465720039 ecr 3947648001], length 0
21:27:56.163434 IP 127.0.0.1.37594 > 127.0.0.20.8000: Flags [P.], seq 1:6, ack 1, win 512, options [nop,nop,TS val 3465720040 ecr 3947648001], length 5
21:27:56.163455 IP 127.0.0.20.8000 > 127.0.0.1.37594: Flags [.], ack 6, win 512, options [nop,nop,TS val 3947648002 ecr 3465720040], length 0
21:27:56.163854 IP 127.0.0.20.8000 > 127.0.0.1.37594: Flags [F.], seq 1, ack 6, win 512, options [nop,nop,TS val 3947648002 ecr 3465720040], length 0
21:27:56.164203 IP 127.0.0.1.37594 > 127.0.0.20.8000: Flags [F.], seq 6, ack 2, win 512, options [nop,nop,TS val 3465720041 ecr 3947648002], length 0
21:27:56.164224 IP 127.0.0.20.8000 > 127.0.0.1.37594: Flags [.], ack 7, win 512, options [nop,nop,TS val 3947648003 ecr 3465720041], length 0
```

### Reading from closed sockets

Using shutdown() you can have an open file handle to a closed socket. If each
socket calls `shutdown(SHUT_WR)` and the first socket waits the 2\*MSL
time-wait timeout, both sockets will be in the "closed" state. Data that is
already in a closed socket's receive buffer can still be read.

```python3
import socket, time
s1 = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
server = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
server.bind(('127.0.0.20', 8000))
server.listen()
s1.connect(server.getsockname())
(s2, _) = server.accept()

s1.send(b'hello')
s2.shutdown(socket.SHUT_RDWR)
s1.shutdown(socket.SHUT_RDWR)

# need to wait for s2 to transition from "time-wait" to "closed"
time.sleep(120)
assert s2.recv(5) == b'hello'
```

```text
21:47:50.751326 IP 127.0.0.1.38404 > 127.0.0.20.8000: Flags [S], seq 4079067346, win 65495, options [mss 65495,sackOK,TS val 3466914628 ecr 0,nop,wscale 7], length 0
21:47:50.751341 IP 127.0.0.20.8000 > 127.0.0.1.38404: Flags [S.], seq 670973094, ack 4079067347, win 65483, options [mss 65495,sackOK,TS val 3948842590 ecr 3466914628,nop,wscale 7], length 0
21:47:50.751352 IP 127.0.0.1.38404 > 127.0.0.20.8000: Flags [.], ack 1, win 512, options [nop,nop,TS val 3466914628 ecr 3948842590], length 0
21:47:50.751656 IP 127.0.0.1.38404 > 127.0.0.20.8000: Flags [P.], seq 1:6, ack 1, win 512, options [nop,nop,TS val 3466914628 ecr 3948842590], length 5
21:47:50.751665 IP 127.0.0.20.8000 > 127.0.0.1.38404: Flags [.], ack 6, win 512, options [nop,nop,TS val 3948842590 ecr 3466914628], length 0
21:47:50.751833 IP 127.0.0.20.8000 > 127.0.0.1.38404: Flags [F.], seq 1, ack 6, win 512, options [nop,nop,TS val 3948842590 ecr 3466914628], length 0
21:47:50.751981 IP 127.0.0.1.38404 > 127.0.0.20.8000: Flags [F.], seq 6, ack 2, win 512, options [nop,nop,TS val 3466914629 ecr 3948842590], length 0
21:47:50.751992 IP 127.0.0.20.8000 > 127.0.0.1.38404: Flags [.], ack 7, win 512, options [nop,nop,TS val 3948842591 ecr 3466914629], length 0
```

When the `s2.recv()` call is made, both sockets had already completely closed.

```text
$ ss --tcp --all --numeric '( src 127.0.0.20 or dst 127.0.0.20 )'
State    Recv-Q   Send-Q    Local Address:Port    Peer Address:Port
LISTEN   0        128          127.0.0.20:8000            0.0.0.0:*
```

### Shutting down (`SHUT_RD`) receptions for a socket handle while there is data in the receive buffer

On Linux it seems that `SHUT_RD` does not prevent future data from being read,
which means that the shutdown(2) man page is incorrect.

> If how is SHUT\_RD, further receptions will be disallowed.

Instead the `SHUT_RD` causes EOF to be returned from reads if there is no
available data. But if more data arrives, it can still be read even after an
EOF is returned.

In this example, data sent from s1 to s2 both before and after the
`shutdown(socket.SHUT_RD)` call can be read from s2. An EOF is read after both
messages. The shutdown() did not affect the socket states of either socket.

```python3
import socket
s1 = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
server = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
server.bind(('127.0.0.20', 8000))
server.listen()
s1.connect(server.getsockname())
(s2, _) = server.accept()

s1.send(b'hello')
s2.shutdown(socket.SHUT_RD)

assert s2.recv(5) == b'hello'
assert s2.recv(5) == b''

s1.send(b'world')
assert s2.recv(5) == b'world'
assert s2.recv(5) == b''
```

```text
20:28:09.170324 IP 127.0.0.1.59618 > 127.0.0.20.8000: Flags [S], seq 1897287366, win 65495, options [mss 65495,sackOK,TS val 3462133047 ecr 0,nop,wscale 7], length 0
20:28:09.170338 IP 127.0.0.20.8000 > 127.0.0.1.59618: Flags [S.], seq 2926436180, ack 1897287367, win 65483, options [mss 65495,sackOK,TS val 3944061009 ecr 3462133047,nop,wscale 7], length 0
20:28:09.170350 IP 127.0.0.1.59618 > 127.0.0.20.8000: Flags [.], ack 1, win 512, options [nop,nop,TS val 3462133047 ecr 3944061009], length 0
20:28:09.170624 IP 127.0.0.1.59618 > 127.0.0.20.8000: Flags [P.], seq 1:6, ack 1, win 512, options [nop,nop,TS val 3462133047 ecr 3944061009], length 5
20:28:09.170632 IP 127.0.0.20.8000 > 127.0.0.1.59618: Flags [.], ack 6, win 512, options [nop,nop,TS val 3944061009 ecr 3462133047], length 0
20:28:09.171109 IP 127.0.0.1.59618 > 127.0.0.20.8000: Flags [P.], seq 6:11, ack 1, win 512, options [nop,nop,TS val 3462133048 ecr 3944061009], length 5
20:28:09.171115 IP 127.0.0.20.8000 > 127.0.0.1.59618: Flags [.], ack 11, win 512, options [nop,nop,TS val 3944061010 ecr 3462133048], length 0
```

```text
$ ss --tcp --all --numeric '( src 127.0.0.20 or dst 127.0.0.20 )'
State   Recv-Q  Send-Q    Local Address:Port     Peer Address:Port
LISTEN  0       128          127.0.0.20:8000             0.0.0.0:*
ESTAB   0       0            127.0.0.1:59618       127.0.0.20:8000
ESTAB   0       0            127.0.0.20:8000       127.0.0.1:59618
```

### Sending to a socket that has shut down (`SHUT_RDWR` or `SHUT_RD` + `SHUT_WR`)

Depending on how the socket is shutdown(), the socket may send RST packets if
it receives more payload data.

The RST is only sent for `SHUT_RDWR`, not `SHUT_RD` nor `SHUT_WR`. Both
`SHUT_RDWR` and `SHUT_WR` put the "established" socket into the "fin-wait-2"
state, but each option causes the socket to respond differently to incoming
payload packets. `SHUT_RDWR` will cause an RST while `SHUT_WR` does not. But
`SHUT_RD` and `SHUT_WR` can be combined (in separate shutdown() calls) to have
the same behaviour as `SHUT_RDWR`. In other words, the effects of `SHUT_RDWR`
are not simply the effects of `SHUT_RD` and the effects of `SHUT_WR` combined.
But setting both `SHUT_RD` and `SHUT_WR` results in the effects of `SHUT_RDWR`.

```python3
import socket, time
s1 = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
server = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
server.bind(('127.0.0.20', 8000))
server.listen()
s1.connect(server.getsockname())
(s2, _) = server.accept()

s1.send(b'hello')
s2.shutdown(socket.SHUT_RDWR)

assert s2.recv(5) == b'hello'
assert s2.recv(5) == b''

time.sleep(2)
s1.send(b'world')

# raises a ConnectionResetError
s2.recv(5)
```

```text
23:32:50.707355 IP 127.0.0.1.52788 > 127.0.0.20.8000: Flags [S], seq 4168666343, win 65495, options [mss 65495,sackOK,TS val 3473214584 ecr 0,nop,wscale 7], length 0
23:32:50.707370 IP 127.0.0.20.8000 > 127.0.0.1.52788: Flags [S.], seq 1420018846, ack 4168666344, win 65483, options [mss 65495,sackOK,TS val 3955142546 ecr 3473214584,nop,wscale 7], length 0
23:32:50.707382 IP 127.0.0.1.52788 > 127.0.0.20.8000: Flags [.], ack 1, win 512, options [nop,nop,TS val 3473214584 ecr 3955142546], length 0
23:32:50.707684 IP 127.0.0.1.52788 > 127.0.0.20.8000: Flags [P.], seq 1:6, ack 1, win 512, options [nop,nop,TS val 3473214584 ecr 3955142546], length 5
23:32:50.707693 IP 127.0.0.20.8000 > 127.0.0.1.52788: Flags [.], ack 6, win 512, options [nop,nop,TS val 3955142546 ecr 3473214584], length 0
23:32:50.707865 IP 127.0.0.20.8000 > 127.0.0.1.52788: Flags [F.], seq 1, ack 6, win 512, options [nop,nop,TS val 3955142546 ecr 3473214584], length 0
23:32:50.708945 IP 127.0.0.1.52788 > 127.0.0.20.8000: Flags [.], ack 2, win 512, options [nop,nop,TS val 3473214586 ecr 3955142546], length 0
23:32:52.710862 IP 127.0.0.1.52788 > 127.0.0.20.8000: Flags [P.], seq 6:11, ack 2, win 512, options [nop,nop,TS val 3473216587 ecr 3955142546], length 5
23:32:52.710917 IP 127.0.0.20.8000 > 127.0.0.1.52788: Flags [R], seq 1420018848, win 0, length 0
```

```text
$ ss --tcp --all --numeric '( src 127.0.0.20 or dst 127.0.0.20 )'
State   Recv-Q  Send-Q   Local Address:Port    Peer Address:Port
LISTEN   0        128       127.0.0.20:8000            0.0.0.0:*
```

### Connecting after shutdown of connected socket

If you attempt to connect a socket to some address and the connection fails
(for example if the endpoint does not exist), Linux will allow you to try
connecting to a different address using the same socket. Conceptually, this
would be like if Linux changes the socket back to the "closed" state when the
connection fails. (This is what [`tcp_disconnect`][tcp-disconnect] does,
although I'm not sure if it's used in this case.) This raises a question: If a
socket moves to the "closed" state through some other means, for example
through the use of shutdown(), will Linux also allow this socket to be reused
and connected to a new address? The answer empirically is "no".

[tcp-disconnect]: https://github.com/torvalds/linux/blob/0bb80ecc33a8fb5a682236443c1e740d5c917d1d/net/ipv4/tcp.c#L2967-L2982

```python3
import socket
s1 = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
server = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
server.bind(('127.0.0.20', 8000))
server.listen()
s1.connect(server.getsockname())
(s2, _) = server.accept()

s2.shutdown(socket.SHUT_RDWR)
s1.shutdown(socket.SHUT_RDWR)

# s1 is "closed" and s2 is in "time-wait"

# raises a "OSError: [Errno 106] Transport endpoint is already connected"
s1.connect(server.getsockname())
```

### Connecting after shutdown of listening socket

If a listening socket is shutdown(SHUT\_RD) (or SHUT\_RDWR), it will stop
listening and can be reused as a listening or client socket.

```python3
import socket
server = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
server.bind(('127.0.0.20', 8000))
server.listen()
server.shutdown(socket.SHUT_RD)
server.connect(('127.0.0.20', 1234))
```

### Shutdown during non-blocking connect

If a connection is in-progress and shutdown(WR) is issued, the connection will
be closed. For "syn-sent" sockets, Linux reports this as a "connection reset"
during the next `recv()` call, but no RST packet is sent if the server didn't
reply.

```python3
import socket, time, threading
s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
s.setblocking(False)

def connect():
  start = time.time()
  try:
    s.connect(('8.8.8.8', 8067))
  except Exception as e:
    print(e)
  print('Done non-blocking connect in {} seconds'.format(time.time() - start))

t = threading.Thread(target=connect)
t.start()

time.sleep(5)

# returns a BlockingIOError
s.recv(1)

s.shutdown(socket.SHUT_WR)

# returns a ConnectionResetError
s.recv(1)
```

```
17:30:31.966635 IP 10.0.0.205.40596 > 8.8.8.8.8067: Flags [S], seq 4277443244, win 64240, options [mss 1460,sackOK,TS val 904028831 ecr 0,nop,wscale 7], length 0
17:30:32.990105 IP 10.0.0.205.40596 > 8.8.8.8.8067: Flags [S], seq 4277443244, win 64240, options [mss 1460,sackOK,TS val 904029855 ecr 0,nop,wscale 7], length 0
17:30:35.006115 IP 10.0.0.205.40596 > 8.8.8.8.8067: Flags [S], seq 4277443244, win 64240, options [mss 1460,sackOK,TS val 904031871 ecr 0,nop,wscale 7], length 0
```

### Shutdown during blocking connect

If a thread is blocked on a `connect()` call and another thread calls
`shutdown()` with any of the three options, the shutdown will return `EBUSY`
and not affect the current `connect()`.

```python3
import socket, time, threading
s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)

def connect():
  start = time.time()
  try:
    s.connect(('8.8.8.8', 8067))
  except Exception as e:
    print(e)
  print('Done connecting in {} seconds'.format(time.time() - start))

t = threading.Thread(target=connect)
t.start()

time.sleep(5)

# raises a "OSError: [Errno 16] Device or resource busy"
s.shutdown(socket.SHUT_WR)

# eventually a TimeoutError will occur after ~130 seconds
```

Also worth noting that calling `close()` on the socket has no effect on the
connection attempt.

```python3
import socket, time, threading
s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)

def connect():
  start = time.time()
  try:
    s.connect(('8.8.8.8', 8067))
  except Exception as e:
    print(e)
  print('Done connecting in {} seconds'.format(time.time() - start))

t = threading.Thread(target=connect)
t.start()

time.sleep(5)

s.close()

# eventually a TimeoutError will occur after ~130 seconds
```
