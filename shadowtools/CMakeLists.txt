add_test(
    NAME shadowtools-tests
    COMMAND python3 -m unittest discover ${CMAKE_CURRENT_SOURCE_DIR}
    CONFIGURATIONS extra
)
# Set PYTHONPATH so that the modules can be loaded.
set_property(
    TEST shadowtools-tests
    APPEND
    PROPERTY
    ENVIRONMENT "PYTHONPATH=${CMAKE_CURRENT_SOURCE_DIR}/src")
# Force tests that run a shadow binary to use our locally built one, instead of
# trying to find it on the PATH.
set_property(
    TEST shadowtools-tests
    APPEND
    PROPERTY
    ENVIRONMENT "SHADOW_BIN=${CMAKE_BINARY_DIR}/src/main/shadow")
