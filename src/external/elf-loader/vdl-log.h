#ifndef VDL_LOG_H
#define VDL_LOG_H

// for system_exit in VDL_LOG_ASSERT
#include "system.h"

enum VdlLog {
  VDL_LOG_FUNC     = (1<<0),
  VDL_LOG_DBG      = (1<<1),
  VDL_LOG_ERR      = (1<<2),
  VDL_LOG_AST      = (1<<3),
  VDL_LOG_SYM_FAIL = (1<<4),
  VDL_LOG_REL      = (1<<5),
  VDL_LOG_SYM_OK   = (1<<6),
  VDL_LOG_PRINT    = (1<<7)
};

void vdl_log_printf (enum VdlLog log, const char *str, ...);
#ifdef DEBUG
#define VDL_LOG_FUNCTION(str,...)                                       \
  vdl_log_printf (VDL_LOG_FUNC, "%s:%d, %s (" str ")\n",                \
                  __FILE__, __LINE__, __FUNCTION__, ##__VA_ARGS__)
#define VDL_LOG_DEBUG(str,...)                          \
  vdl_log_printf (VDL_LOG_DBG, str, ##__VA_ARGS__)
#else
#define VDL_LOG_FUNCTION(...)
#define VDL_LOG_DEBUG(...)
#endif
#define VDL_LOG_ERROR(str,...)                                          \
  vdl_log_printf (VDL_LOG_ERR, "%s:%d:%s: " str,                        \
                  __FILE__, __LINE__, __FUNCTION__, ##__VA_ARGS__)
#define VDL_LOG_SYMBOL_FAIL(symbol,file)                                \
  vdl_log_printf (VDL_LOG_SYM_FAIL, "Could not resolve symbol=%s, file=%s\n", \
                  symbol, file->filename)

#define VDL_LOG_SYMBOL_OK(symbol_name,from,match)                       \
  vdl_log_printf (VDL_LOG_SYM_OK, "Resolved symbol=%s, from file=\"%s\", in file=\"%s\":0x%x\n", \
                  symbol_name, from->filename, match->file->filename,   \
                  match->file->load_base + match->symbol.st_value)
#define VDL_LOG_RELOC(rel)                                              \
  vdl_log_printf (VDL_LOG_REL, "Unhandled reloc type=0x%x at=0x%x\n",   \
                  ELFW_R_TYPE (rel->r_info), rel->r_offset)
#define VDL_LOG_ASSERT(predicate,str,...)                               \
  if (!(predicate))                                                     \
    {                                                                   \
      vdl_log_printf (VDL_LOG_AST, "%s:%d, %s, " str "\n",              \
                      __FILE__, __LINE__, __FUNCTION__, ## __VA_ARGS__); \
      {                                                                 \
        char *p = 0;                                                    \
        *p = 0;                                                         \
      }                                                                 \
      system_exit (-1);                                                 \
    }

// expect a ':' separated list
void vdl_log_set (const char *debug_str);


#endif /* VDL_LOG_H */
