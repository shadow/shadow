/usr/bin/env grep "ObjectCounter: counter diffs:" ../../Testing/Temporary/LastTest.log.tmp | head -n 20 | /usr/bin/env cut -d' ' -f8- > leakcheck.log
