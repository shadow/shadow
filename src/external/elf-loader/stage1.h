#ifndef STAGE1_H
#define STAGE1_H

struct Stage1InputOutput
{
  // initialized by stage0
  unsigned long entry_point_struct;
  // set by stage1 before returning to stage0
  unsigned long entry_point;
  // set by stage1 before returning to stage0
  unsigned long dl_fini;
};

void stage1 (struct Stage1InputOutput *input_output);

void stage1_freeres (void);

#endif /* STAGE1_H */
