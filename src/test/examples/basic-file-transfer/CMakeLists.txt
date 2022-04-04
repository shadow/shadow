add_shadow_tests(BASENAME basic-file-transfer-example
                 ARGS
                   --strace-logging-mode off
                 PROPERTIES
                   # use a longer timeout since old versions of curl have a
                   # spinloop
                   TIMEOUT 10
                   CONFIGURATIONS extra)
