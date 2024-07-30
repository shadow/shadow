#!/usr/bin/env bash
set -euo pipefail

# first argument is the path to shadow
if [ "$#" -ge 1 ]; then
    echo "Prepending $1 to PATH"
    export PATH="$1:${PATH}"
fi

# ANCHOR: body
if [ ! -d jetty-home-12.0.12/ ]; then
  wget https://repo1.maven.org/maven2/org/eclipse/jetty/jetty-home/12.0.12/jetty-home-12.0.12.zip
  echo "2dc2c60a8a3cb84df64134bed4df1c45598118e9a228604eaeb8b9b42d80bc07  jetty-home-12.0.12.zip" | sha256sum -c
  unzip -q jetty-home-12.0.12.zip && rm jetty-home-12.0.12.zip
fi

rm -rf shadow.data; shadow shadow.yaml > shadow.log
cat shadow.data/hosts/client1/curl.1000.stdout
# ANCHOR_END: body
