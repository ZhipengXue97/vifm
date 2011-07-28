/* vifm
 * Copyright (C) 2001 Ken Steen.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 2 of the License, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.	See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc., 59 Temple
 * Place - Suite 330, Boston, MA 02111-1307, USA
 */

#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <time.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <fcntl.h>

#include "background.h"
#include "commands.h"
#include "config.h"
#include "menus.h"
#include "status.h"
#include "utils.h"

struct Jobs_List *jobs = NULL;
struct Finished_Jobs *fjobs = NULL;

static void
add_background_job(pid_t pid, const char *cmd, int fd)
{
	Jobs_List *new;

	new = (Jobs_List *)malloc(sizeof(Jobs_List));
	new->pid = pid;
	new->cmd = strdup(cmd);
	new->next = jobs;
	new->fd = fd;
	new->skip_errors = 0;
	new->error_buf = NULL;
	new->running = 1;
	jobs = new;
}

void
add_finished_job(pid_t pid, int status)
{
	Finished_Jobs *new;

	new = (Finished_Jobs *)malloc(sizeof(Finished_Jobs));
	new->pid = pid;
	new->remove = 0;
	new->next = fjobs;
	new->exit_code = WEXITSTATUS(status);
	fjobs = new;
}

void
check_background_jobs(void)
{
	Jobs_List *p = jobs;
	Jobs_List *prev = 0;
	Finished_Jobs *fj = NULL;
	sigset_t new_mask;
	fd_set ready;
	int maxfd;
	int nread;
	struct timeval ts;

	if(!p)
		return;

	/*
	 * SIGCHLD	needs to be blocked anytime the Finished_Jobs list
	 * is accessed from anywhere except the received_sigchld().
	 */
	sigemptyset(&new_mask);
	sigaddset(&new_mask, SIGCHLD);
	sigprocmask(SIG_BLOCK, &new_mask, NULL);

	fj = fjobs;

	ts.tv_sec = 0;
	ts.tv_usec = 1000;

	while (p)
	{
		/* Mark any finished jobs */
		while (fj)
		{
			if (p->pid == fj->pid)
			{
				p->running = 0;
				fj->remove = 1;
				p->exit_code = fj->exit_code;
			}
			fj = fj->next;
		}

		/* Setup pipe for reading */

		FD_ZERO(&ready);
		maxfd = 0;
		FD_SET(p->fd, &ready);
		maxfd = (p->fd > maxfd ? p->fd : maxfd);

		while(select(maxfd + 1, &ready, NULL, NULL, &ts) > 0)
		{
			char buf[256];
			nread = read(p->fd, buf, sizeof(buf) - 1);

			if(nread == 0)
			{
				break;
			}
			else if(nread > 0)
			{
				p->error_buf = (char *)realloc(p->error_buf, nread + 1);

				strncpy(p->error_buf, buf, nread);
			}
			if(p->error_buf != NULL)
			{
				if(!p->running && p->exit_code == 127 && cfg.fast_run)
				{
					char *buf = fast_run_complete(p->cmd);
					if(buf == NULL)
						curr_stats.save_msg = 1;
					else
						start_background_job(buf);
				}
				else
				{
					if(!p->skip_errors)
						p->skip_errors = show_error_msg("Background Process Error",
								p->error_buf);
					free(p->error_buf);
					p->error_buf = NULL;
				}
			}
		}

		/* Remove any finished jobs. */
		if(!p->running)
		{
			Jobs_List *j = p;
			if (prev)
				prev->next = p->next;
			else
				jobs = p->next;

			p = p->next;
			free(j->cmd);
			free(j->error_buf);
			free(j);
		}
		else
		{
			prev = p;
			p = p->next;
		}
	}

	/* Clean up Finished Jobs list */
	fj = fjobs;
	if (fj)
	{
		Finished_Jobs *prev = 0;
		while (fj)
		{
			if (fj->remove)
			{
				Finished_Jobs *j = fj;

				if (prev)
					prev->next = fj->next;
				else
					fjobs = fj->next;

				fj = fj->next;
				free(j);
			}
			else
			{
				prev = fj;
				fj = fj->next;
			}
		}
	}

	/* Unblock SIGCHLD signal */
	sigprocmask(SIG_UNBLOCK, &new_mask, NULL);
}

/* Used for fusezip mounting of files */
int
background_and_wait_for_status(char *cmd)
{
	int pid;
	int status;
	extern char **environ;

	if(cmd == 0)
		return 1;

	pid = fork();
	if(pid == -1)
		return -1;
	if(pid == 0)
	{
		char *args[4];

		args[0] = "sh";
		args[1] = "-c";
		args[2] = cmd;
		args[3] = NULL;
		execve("/bin/sh", args, environ);
		exit(127);
	}
	do
	{
		if(waitpid(pid, &status, 0) == -1)
		{
			if(errno != EINTR)
				return -1;
		}
		else
			return status;
	}while(1);
}

/* Only used for deleting and putting of files so that the changes show
 * up immediately in the file lists.
 */
int
background_and_wait_for_errors(char *cmd)
{
	pid_t pid;
	int error_pipe[2];
	int error = 0;

	if(pipe(error_pipe) != 0)
	{
		show_error_msg("File pipe error", "Error creating pipe");
		return -1;
	}

	if((pid = fork()) == -1)
		return -1;

	if(pid == 0)
	{
		run_from_fork(error_pipe, 1, cmd);
	}
	else
	{
		FILE *ef;

		close(error_pipe[1]); /* Close write end of pipe. */

		ef = fdopen(error_pipe[0], "r");
		error = print_errors(ef);
	}

	if(error)
		return -1;

	return 0;
}

int
start_background_job(const char *cmd)
{
	pid_t pid;
	char *args[4];
	int error_pipe[2];

	if(pipe(error_pipe) != 0)
	{
		show_error_msg("File pipe error", "Error creating pipe");
		return -1;
	}

	if((pid = fork()) == -1)
		return -1;

	if(pid == 0)
	{
		extern char **environ;

		int nullfd;
		close(2);                    /* Close stderr */
		if(dup(error_pipe[1]) == -1) /* Redirect stderr to write end of pipe. */
		{
			perror("dup");
			exit(-1);
		}
		close(error_pipe[0]); /* Close read end of pipe. */
		close(0); /* Close stdin */
		close(1); /* Close stdout */

		/* Send stdout, stdin to /dev/null */
		if((nullfd = open("/dev/null", O_RDONLY)) != -1)
		{
			dup2(nullfd, 0);
			dup2(nullfd, 1);
		}

		args[0] = "sh";
		args[1] = "-c";
		args[2] = (char *)cmd;
		args[3] = NULL;

		setpgid(0, 0);

		execve("/bin/sh", args, environ);
		exit(-1);
	}
	else
	{
		close(error_pipe[1]); /* Close write end of pipe. */

		add_background_job(pid, cmd, error_pipe[0]);
	}
	return 0;
}

/* vim: set tabstop=2 softtabstop=2 shiftwidth=2 noexpandtab cinoptions-=(0 : */
/* vim: set cinoptions+=t0 : */
