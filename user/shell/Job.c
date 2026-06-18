#include "Job.h"
#include <string.h>

// Removes the job with the given id from the Vec self
int vec_remove_job_by_id(Vec *self, jid_t id) {
	for (int i = 0; i < vec_len(self); i++) {
	    if (((job*)self->data[i])->id == id) {
	        vec_erase(self, i);
		return 1;
	    }
	}
	return 0;
}

// Returns job containing specified pid.
job *get_job_by_pid(pid_t pid, int *idx, Vec *background_jobs) {
    int curr_elem = 0;
    while (curr_elem < vec_len(background_jobs)) {
        job *temp_job = (job*) vec_get(background_jobs, curr_elem);
        for (int i = 0; i < temp_job->cmd->num_commands; i++) {
            if (pid == temp_job->pids[i]) {
                if (idx) {
                    *idx = curr_elem;
                }

                return temp_job;
            }
        }
        curr_elem++;
    }

    return NULL;
}

// frees all memory associated with the job j
void free_job(void *j) {
    job *jb_to_free = (job*) j;
    free(jb_to_free->full_cmd);
    free(jb_to_free->cmd);
    free(jb_to_free->pids);
    free(jb_to_free);
}

// returns the first background job in the queue
// either most recently stopped job, or first created background job
job *get_job_bg_fg(char *job_id_str, Vec *stopped_background_jobs, Vec *background_jobs) {
	job *job_to_continue = NULL;

	// Get next job in queue
	if (job_id_str == NULL) {
		if (vec_len(stopped_background_jobs) > 0) {
			void *temp_job_ptr = &job_to_continue;
			vec_pop_back(stopped_background_jobs, temp_job_ptr);
		} else if (vec_len(background_jobs) > 0) {
			job_to_continue = vec_get(background_jobs, vec_len(background_jobs)-1);
		} else {
			write(2, "No jobs in queue.\n", strlen("No jobs in queue.\n"));
			return NULL;
		}

		return job_to_continue;
	}

	// Convert job id to jid_t
	jid_t jid = strtol(job_id_str, NULL, 10);
	if (jid == 0) {
		write(2, "invalid job_id\n", strlen("invalid job_id\n"));
		return NULL;
	}

	// Get job with specified jid
	for (int i = 0; i < vec_len(background_jobs); i++) {
		job *temp_job = vec_get(background_jobs, i);
		if (temp_job->id == jid) {
			job_to_continue = temp_job;
			break;
		}
	}

	if (job_to_continue == NULL) {
		write(2, "no job with job_id exists\n", strlen("no job with job_id exists\n"));
		return NULL;
	} 

	return job_to_continue;
}

// general free deleter function for job status updates Vec
void free_dtor(void* ptr) {
	free(ptr);
}
