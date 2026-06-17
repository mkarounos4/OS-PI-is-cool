#ifndef JOB_H_
#define JOB_H_

#include <stdint.h>
#include "./parser.h"
#include "Vec.h"

#define RUNNING_STATE 0
#define STOPPED_STATE 1

// define new type "job id"
typedef uint64_t jid_t;

/**
 * @brief One shell job: a parsed command line (possibly a pipeline),
 * the OS process ids backing each stage, and bookkeeping for job
 * control (foreground vs background, running vs stopped).
 */
typedef struct job_st {
  /** @brief Unique job id used by jobs / bg / fg. */
  uint64_t id;
  /** @brief Parsed representation of the command line (argv per stage, redirections, etc.). */
  struct parsed_command* cmd;
  /** @brief Parallel array of child pids; length matches cmd->num_commands. */
  pid_t* pids;
  /** @brief Original command string as typed (without a trailing `&`). */
  char *full_cmd;
  /** @brief Count of pipeline processes not yet reaped (still running or zombied). */
  int num_procs_running;
  /** @brief RUNNING_STATE or STOPPED_STATE (see defines above). */
  int status;
  /** @brief How many processes in this job are currently stopped. */
  int num_procs_stopped;
} job;

#endif  // JOB_H_

job *get_job_by_pid(pid_t pid, int *idx, Vec *background_jobs);
int vec_remove_job_by_id(Vec *self, jid_t id);
void free_job(void *j);
job *get_job_bg_fg(char *job_id_str, Vec *stopped_background_jobs, Vec *background_jobs);
void free_dtor(void* ptr);
