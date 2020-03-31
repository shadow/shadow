/usr/bin/env grep "ObjectCounter: counter diffs:" ../../Testing/Temporary/LastTest.log.tmp | /usr/bin/env cut -d' ' -f8- > leakcheck.log
