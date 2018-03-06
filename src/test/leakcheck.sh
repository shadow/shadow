/usr/bin/grep "ObjectCounter: counter diffs:" ../../Testing/Temporary/LastTest.log.tmp | head -n 20 | /usr/bin/cut -d' ' -f8- > leakcheck.log
