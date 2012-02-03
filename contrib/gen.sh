#! /bin/bash

for N in 200; do
    rm -rf tiny${N}
    mkdir tiny${N}
    cd tiny${N}

    python ../../contrib/generate.py --nclients=${N} --nrelays=20 --nservers=20 /storage/projects/shadowtor/alexa-top-1000-ips_2012-01-26.csv /storage/projects/shadowtor/consensus.txt /storage/projects/shadowtor/server-descriptors-2012-01 /storage/projects/shadowtor/direct-users.csv

    cp ../authority.torrc .
    cp ../relay.torrc .
    cp ../client.torrc .
    cp ../exit.torrc .
    cp -R ../keys .

    cd ../
done

for N in 750; do
    rm -rf small${N}
    mkdir small${N}
    cd small${N}

    python ../../contrib/generate.py --nclients=${N} --nrelays=50 --nservers=50 /storage/projects/shadowtor/alexa-top-1000-ips_2012-01-26.csv /storage/projects/shadowtor/consensus.txt /storage/projects/shadowtor/server-descriptors-2012-01 /storage/projects/shadowtor/direct-users.csv

    cp ../authority.torrc .
    cp ../relay.torrc .
    cp ../client.torrc .
    cp ../exit.torrc .
    cp -R ../keys .

    cd ../
done

for N in 1500; do
    rm -rf medium${N}
    mkdir medium${N}
    cd medium${N}

    python ../../contrib/generate.py --nclients=${N} --nrelays=100 --nservers=100 /storage/projects/shadowtor/alexa-top-1000-ips_2012-01-26.csv /storage/projects/shadowtor/consensus.txt /storage/projects/shadowtor/server-descriptors-2012-01 /storage/projects/shadowtor/direct-users.csv

    cp ../authority.torrc .
    cp ../relay.torrc .
    cp ../client.torrc .
    cp ../exit.torrc .
    cp -R ../keys .

    cd ../
done

for N in 4500; do
    rm -rf large${N}
    mkdir large${N}
    cd large${N}

    python ../../contrib/generate.py --nclients=${N} --nrelays=300 --nservers=300 /storage/projects/shadowtor/alexa-top-1000-ips_2012-01-26.csv /storage/projects/shadowtor/consensus.txt /storage/projects/shadowtor/server-descriptors-2012-01 /storage/projects/shadowtor/direct-users.csv

    cp ../authority.torrc .
    cp ../relay.torrc .
    cp ../client.torrc .
    cp ../exit.torrc .
    cp -R ../keys .

    cd ../
done

for N in 9000; do
    rm -rf jumbo${N}
    mkdir jumbo${N}
    cd jumbo${N}

    python ../../contrib/generate.py --nclients=${N} --nrelays=600 --nservers=600 /storage/projects/shadowtor/alexa-top-1000-ips_2012-01-26.csv /storage/projects/shadowtor/consensus.txt /storage/projects/shadowtor/server-descriptors-2012-01 /storage/projects/shadowtor/direct-users.csv

    cp ../authority.torrc .
    cp ../relay.torrc .
    cp ../client.torrc .
    cp ../exit.torrc .
    cp -R ../keys .

    cd ../
done

