#!/usr/bin/env python3

"""
Shows the flags returned by `poll()` while the socket is in various states.
"""

import socket
import select


def select_flag_to_str(flags):
    display = []

    if flags & select.POLLIN != 0:
        display.append("POLLIN")
        flags &= ~select.POLLIN
    if flags & select.POLLOUT != 0:
        display.append("POLLOUT")
        flags &= ~select.POLLOUT
    if flags & select.POLLPRI != 0:
        display.append("POLLPRI")
        flags &= ~select.POLLPRI
    if flags & select.POLLERR != 0:
        display.append("POLLERR")
        flags &= ~select.POLLERR
    if flags & select.POLLHUP != 0:
        display.append("POLLHUP")
        flags &= ~select.POLLHUP
    if flags & select.POLLRDHUP != 0:
        display.append("POLLRDHUP")
        flags &= ~select.POLLRDHUP
    if flags & select.POLLNVAL != 0:
        display.append("POLLNVAL")
        flags &= ~select.POLLNVAL

    if flags != 0:
        display.append(str(flags))

    return " | ".join(display)


def test_socket(closure):
    with socket.socket(socket.AF_INET, socket.SOCK_STREAM | socket.SOCK_NONBLOCK) as s:
        p = select.poll()
        mask = (
            select.POLLIN
            | select.POLLOUT
            | select.POLLPRI
            | select.POLLERR
            | select.POLLHUP
            | select.POLLRDHUP
        )
        p.register(s, mask)

        closure(s)

        flags = p.poll(1)
        return flags[0][1] if len(flags) > 0 else 0


flags = test_socket(lambda s: None)
print("New socket:", select_flag_to_str(flags))

flags = test_socket(lambda s: s.close())
print("Closed socket:", select_flag_to_str(flags))

flags = test_socket(lambda s: s.listen())
print("Listening socket:", select_flag_to_str(flags))


def test(s):
    s.listen()
    sockname = s.getsockname()
    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as s2:
        s2.connect(sockname)


flags = test_socket(test)
print("Listening socket with incoming connection:", select_flag_to_str(flags))


def test(s):  # type: ignore[no-redef]
    try:
        s.connect(("127.0.0.1", 23427))
    except BlockingIOError as e:
        pass


flags = test_socket(test)
print("Connecting socket to non-existent localhost:", select_flag_to_str(flags))


def test(s):  # type: ignore[no-redef]
    try:
        s.connect(("1.2.3.4", 23427))
    except BlockingIOError as e:
        pass


flags = test_socket(test)
print("Connecting socket to probably-non-existent internet:", select_flag_to_str(flags))
