#include "vdl-log.h"
#include "avprintf-cb.h"
#include "system.h"
#include "vdl-utils.h"
#include "vdl-list.h"

uint32_t g_logging = 0;

static void
avprintf_callback (char c, __attribute__((unused)) void *context)
{
  if (c != 0)
    {
      system_write (2, &c, 1);
    }
}

void
vdl_log_set (const char *debug_str)
{
  VDL_LOG_FUNCTION ("debug=%s", debug_str);
  g_logging = VDL_LOG_AST | VDL_LOG_PRINT | VDL_LOG_ERR;
  if (debug_str == 0)
    {
      return;
    }
  struct VdlList *list = vdl_utils_strsplit (debug_str, ':');
  void **cur;
  uint32_t logging = 0;
  for (cur = vdl_list_begin (list);
       cur != vdl_list_end (list); cur = vdl_list_next (cur))
    {
      if (vdl_utils_strisequal (*cur, "debug"))
        {
          logging |= VDL_LOG_DBG;
        }
      else if (vdl_utils_strisequal (*cur, "function"))
        {
          logging |= VDL_LOG_FUNC;
        }
      else if (vdl_utils_strisequal (*cur, "error"))
        {
          logging |= VDL_LOG_ERR;
        }
      else if (vdl_utils_strisequal (*cur, "assert"))
        {
          logging |= VDL_LOG_AST;
        }
      else if (vdl_utils_strisequal (*cur, "symbol-fail"))
        {
          logging |= VDL_LOG_SYM_FAIL;
        }
      else if (vdl_utils_strisequal (*cur, "symbol-ok"))
        {
          logging |= VDL_LOG_SYM_OK;
        }
      else if (vdl_utils_strisequal (*cur, "reloc"))
        {
          logging |= VDL_LOG_REL;
        }
      else if (vdl_utils_strisequal (*cur, "help"))
        {
          VDL_LOG_ERROR ("Available logging levels: debug, "
                         "function, error, assert, symbol-fail, symbol-ok, reloc\n");
        }
    }
  g_logging |= logging;
  vdl_utils_str_list_delete (list);
}



void
vdl_log_printf (enum VdlLog log, const char *str, ...)
{
  va_list list;
  va_start (list, str);
  if (g_logging & log)
    {
      avprintf_cb (avprintf_callback, 0, str, list);
    }
  va_end (list);
}
