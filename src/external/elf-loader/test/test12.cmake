file(RENAME libl.so libl.so.deleted)
execute_process(COMMAND /bin/bash ${SOURCE_DIR}/runtest.sh test12 ${SOURCE_DIR}/ RESULT_VARIABLE CMD_RESULT)
file(RENAME libl.so.deleted libl.so)
if(CMD_RESULT)
  message(FATAL_ERROR "test12 error")
endif()

