// Expose the legacy Worker struct (now WorkerC) for use in the Rust Worker.
#ifndef SHD_WORKER_C_H_
#define SHD_WORKER_C_H_

// Legacy Worker struct. Eventually everything here should be moved into the
// Rust Worker.
typedef struct WorkerC WorkerC;

void workerc_free(WorkerC*);

#endif