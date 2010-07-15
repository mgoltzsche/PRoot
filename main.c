/* -*- c-set-style: "K&R"; c-basic-offset: 8 -*-
 *
 * This file is part of PRoot: a PTrace based chroot alike.
 *
 * Copyright (C) 2010 STMicroelectronics
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 * 02110-1301 USA.
 *
 * Author: Cedric VINCENT (cedric.vincent@st.com)
 * Inspired by: strace.
 */

#include <unistd.h>     /* fork(2), */
#include <stdlib.h>     /* exit(3), EXIT_*, */
#include <stdio.h>      /* puts(3), */
#include <sys/wait.h>   /* wait(2), */
#include <sys/ptrace.h> /* ptrace(2), */

#include "syscall.h" /* get_syscall_number(), ARG_SYSNUM, */

static void print_usage()
{
	puts("Usage:\n");
	puts("\tproot [options] <program> [args]\n");
	puts("Where options are:\n");
	puts("\tnot yet supported\n");
}

int main(int argc, char *argv[])
{
	unsigned long syscall = -1;
	int child_status = 0;
	long status = 0;
	int signal = 0;
	pid_t pid = 0;

	void *buffer;

	if (argc < 2) {
		print_usage();
		exit(EXIT_FAILURE);
	}

	pid = fork();
	switch(pid) {
	case -1:
		perror("proot -- fork()");
		exit(EXIT_FAILURE);

	case 0: /* child */

		/* Declare myself as ptraceable before executing the
		 * requested program. */
		status = ptrace(PTRACE_TRACEME, 0, NULL, NULL);
		if (status < 0) {
			perror("proot -- ptrace(TRACEME)");
			exit(EXIT_FAILURE);
		}

		status = execvp(argv[1], argv + 1);
		perror("proot -- execvp()");
		exit(EXIT_FAILURE);

	default: /* parent */
		break;
	}

	/* Wait for the first child's stop (due to a SIGTRAP). */
	pid = waitpid(pid, &child_status, 0);
	if (pid < 0) {
		perror("proot -- wait()");
		exit(EXIT_FAILURE);
	}

	/* Set ptracing options. */
	status = ptrace(PTRACE_SETOPTIONS, pid, NULL,
			PTRACE_O_TRACESYSGOOD |
			/* Supported soon:
			   PTRACE_O_TRACEFORK    |
			   PTRACE_O_TRACEVFORK   |
			   PTRACE_O_TRACECLONE   |
			   PTRACE_O_TRACEEXEC    |
			*/
			PTRACE_O_TRACEEXIT);
	if (status < 0) {
		perror("proot -- ptrace(SETOPTIONS)");
		exit(EXIT_FAILURE);
	}

	while (1) {
		/* Restart the child and stop it at the next
		 * entry or exit of a system call. */
		status = ptrace(PTRACE_SYSCALL, pid, NULL, signal);
		if (status < 0) {
			perror("proot -- ptrace(SYSCALL)");
			exit(EXIT_FAILURE);
		}

		/* Wait for the next child's stop. */
		pid = waitpid(pid, &child_status, 0);
		if (pid < 0) {
			perror("proot -- waitpid()");
			exit(EXIT_FAILURE);
		}

		if (WIFEXITED(child_status)) {
			printf("proot: child exited with status %d\n",
			       WEXITSTATUS(child_status));
			break;
		}
		else if (WIFSIGNALED(child_status)) {
			printf("proot: child terminated by signal %d\n",
			       WTERMSIG(child_status));
			break;
		}
		else if (WIFCONTINUED(child_status)) {
			printf("proot: child continued\n");
			signal = SIGCONT;
		}
		else if (WIFSTOPPED(child_status)) {
			signal = WSTOPSIG(child_status);
			if ((signal & 0x80) == 0) {
				printf("proot: received a signal\n");
				continue;
			}
			signal = 0;

			/* Check if we are either entering or exiting a syscall.
			   Currently it doesn't support multi-process programs. */
			if (syscall == -1) {
				syscall = get_syscall_arg(pid, ARG_SYSNUM);

				buffer = alloc_buffer(pid, 64);
				printf("proot: %p = alloc(%d, %d)\n", buffer, pid, 64);
			}
			else {
				printf("proot: syscall(%ld) = 0x%lx\n",
				       syscall, get_syscall_arg(pid, ARG_RESULT));
				syscall = -1;

				printf("proot: free(%d, %p, %d)\n", pid, buffer, 64);
				free_buffer(pid, buffer, 64);
			}
		}
		else {
			printf("proot: unknown trace event\n");
			signal = 0;
		}
	}

	puts("proot: finished");
	exit(EXIT_SUCCESS);
}
