import socket
import sys
import time

connect_to = sys.argv[1];

client_sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
server_sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)

server_sock.bind(('0.0.0.0', 8000))
server_sock.listen()
print("Server listening")

# asynchronously connect the client to the server
client_sock.setblocking(0)
try:
    client_sock.connect((connect_to, 8000))
except BlockingIOError:
    pass
client_sock.setblocking(1)
print("Client connecting")

(child_sock, _) = server_sock.accept()
print("Connection accepted")

# send a bunch of packets back-and-forth, and make sure the rtt is close to 0
for i in range(200):
    start_time = time.time()

    client_sock.sendall(b"Hello, world")
    assert len(child_sock.recv(1024)) == 12

    duration = time.time() - start_time
    print(f"({i}) Send-recv duration: {duration*1000*1000*1000:.0f} ns")

    # on a laptop on Linux (no Shadow) the duration ranges from around 6 us to 130 us
    assert duration < 0.0005 # 500 us

print("Done")
