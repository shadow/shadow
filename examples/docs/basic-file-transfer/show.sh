#!/usr/bin/env bash
set -euo pipefail

# ANCHOR: body_1
ls -l shadow.data/hosts/
# ANCHOR_END: body_1

: <<OUTPUT
# ANCHOR: output_1
drwxrwxr-x 2 user user 4096 Jun  2 16:54 client1
drwxrwxr-x 2 user user 4096 Jun  2 16:54 client2
drwxrwxr-x 2 user user 4096 Jun  2 16:54 client3
drwxrwxr-x 2 user user 4096 Jun  2 16:54 server
# ANCHOR_END: output_1
OUTPUT

# ANCHOR: body_2
ls -l shadow.data/hosts/client1/
# ANCHOR_END: body_2

: <<OUTPUT
# ANCHOR: output_2
-rw-rw-r-- 1 user user   0 Jun  2 16:54 curl.1000.shimlog
-rw-r--r-- 1 user user   0 Jun  2 16:54 curl.1000.stderr
-rw-r--r-- 1 user user 542 Jun  2 16:54 curl.1000.stdout
# ANCHOR_END: output_2
OUTPUT

# ANCHOR: body_3
cat shadow.data/hosts/client1/curl.1000.stdout
# ANCHOR_END: body_3

: <<OUTPUT
# ANCHOR: output_3
<!DOCTYPE HTML PUBLIC "-//W3C//DTD HTML 4.01//EN" "http://www.w3.org/TR/html4/strict.dtd">
<html>
<head>
<meta http-equiv="Content-Type" content="text/html; charset=utf-8">
<title>Directory listing for /</title>
</head>
<body>
<h1>Directory listing for /</h1>
...
# ANCHOR_END: output_3
OUTPUT
