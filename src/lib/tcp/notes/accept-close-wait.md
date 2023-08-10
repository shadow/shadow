Calling `accept()` doesn't only return sockets in the "established" state. It
can also return sockets that are closing, for example in the "close-wait"
state. This allows you to receive data on short-lived connections.

```python3
import socket
s1 = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
s2 = socket.socket(socket.AF_INET, socket.SOCK_STREAM)

s1.bind(('127.0.0.10', 10000))
s1.listen()
s2.connect(('127.0.0.10', 10000))

s2.send(b'hello')
s2.close()

# s2 is in the "fin-wait-2" state since it sent a FIN

(s3, _) = s1.accept()

# s3 is in the "close-wait" state since it received a FIN

assert s3.recv(10) == b'hello'
assert s3.recv(10) == b''
```
