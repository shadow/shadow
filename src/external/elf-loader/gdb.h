#ifndef GDB_H
#define GDB_H

struct VdlFile;

void gdb_initialize (struct VdlFile *main);

void gdb_notify (void);

#endif /* GDB_H */
