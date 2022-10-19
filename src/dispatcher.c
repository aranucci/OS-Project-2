#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <errno.h>
#include <sys/stat.h>
#include <fcntl.h>
#include "dispatcher.h"
#include "shell_builtins.h"
#include "parser.h"

static int finish_external_command(struct command *pipeline, int fd) 
{
	int output_fd = STDOUT_FILENO;
	int fd_ar[2];

	if (pipeline->output_type == COMMAND_OUTPUT_STDOUT) {
		output_fd = STDOUT_FILENO;
	} else if (pipeline->output_type == COMMAND_OUTPUT_FILE_TRUNCATE) {
		output_fd = open(pipeline->output_filename, O_RDWR | O_CREAT | O_TRUNC, S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH);
	} else if (pipeline->output_type == COMMAND_OUTPUT_FILE_APPEND) {
		output_fd = open(pipeline->output_filename, O_RDWR | O_CREAT | O_APPEND, S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH);
	} else if (pipeline->output_type == COMMAND_OUTPUT_PIPE) {
		if (pipe(fd_ar)){
			fprintf(stderr, "Error with pipe\n");
			return 1;
		} 
		output_fd = fd_ar[1];
	}

	// if output_fd is <0, we failed opening the output file
	if (output_fd < 0) {
		fprintf(stderr, "could not open file.\n");
		return -1;
	}
	
	pid_t pid = fork();
	int waitStatus;

	if (pid == 0) {

		if (fd != STDIN_FILENO) {
			dup2(fd, STDIN_FILENO);
			close(fd);
		}
		if (output_fd != STDOUT_FILENO) {
			dup2(output_fd, STDOUT_FILENO);
			close(output_fd);
		}

		int returnStatus = execvp(pipeline->argv[0], pipeline->argv);
		if (returnStatus != 0) {
			if (errno == EACCES)
				fprintf(stderr, "Permission denied: %s\n", pipeline->argv[0]);
			else if (errno == ENOENT)
				fprintf(stderr, "Command not found: %s\n", pipeline->argv[0]);
			else if (errno == E2BIG)
				fprintf(stderr, "Command input is too big\n");
			else if (errno == ENOENT)
				fprintf(stderr, "Error opening process files: \n");
			else
				fprintf(stderr, "An error occurred processing command: %s\n", pipeline->argv[0]);
		}
		exit(returnStatus);
		return -1;
	} else {

		if (output_fd != STDOUT_FILENO) {
			close(output_fd);
		}

		if (pipeline->output_type == COMMAND_OUTPUT_PIPE) {
			waitStatus = finish_external_command(pipeline->pipe_to, fd_ar[0]);
			if(waitStatus != 0) {
				return waitStatus;
			}
		}
		waitpid(pid, &waitStatus, 0);
		return waitStatus;
	}
}

/**
 * dispatch_external_command() - run a pipeline of commands
 *
 * @pipeline:   A "struct command" pointer representing one or more
 *              commands chained together in a pipeline.  See the
 *              documentation in parser.h for the layout of this data
 *              structure.  It is also recommended that you use the
 *              "parseview" demo program included in this project to
 *              observe the layout of this structure for a variety of
 *              inputs.
 *
 * Note: this function should not return until all commands in the
 * pipeline have completed their execution.
 *
 * Return: The return status of the last command executed in the
 * pipeline.
 */
static int dispatch_external_command(struct command *pipeline)
{
	/*
	 * Note: this is where you'll start implementing the project.
	 *
	 * It's the only function with a "TODO".  However, if you try
	 * and squeeze your entire external command logic into a
	 * single routine with no helper functions, you'll quickly
	 * find your code becomes sloppy and unmaintainable.
	 *
	 * It's up to *you* to structure your software cleanly.  Write
	 * plenty of helper functions, and even start making yourself
	 * new files if you need.
	 *
	 * For D1: you only need to support running a single command
	 * (not a chain of commands in a pipeline), with no input or
	 * output files (output to stdout only).  In other words, you
	 * may live with the assumption that the "input_file" field in
	 * the pipeline struct you are given is NULL, and that
	 * "output_type" will always be COMMAND_OUTPUT_STDOUT.
	 *
	 * For D2: you'll extend this function to support input and
	 * output files, as well as pipeline functionality.
	 *
	 * Good luck!
	 */

	int input_fd = STDIN_FILENO;

	if (pipeline->input_filename == NULL) {
		input_fd = STDIN_FILENO;
	} else {
		input_fd = open(pipeline->input_filename, O_RDONLY);
		// check if fd < 0, then file failed to open
		if (input_fd < 0) {
			fprintf(stderr, "could not open file.\n");
			return -1;
		}
	}

	return finish_external_command(pipeline, input_fd);
}

/**
 * dispatch_parsed_command() - run a command after it has been parsed
 *
 * @cmd:                The parsed command.
 * @last_rv:            The return code of the previously executed
 *                      command.
 * @shell_should_exit:  Output parameter which is set to true when the
 *                      shell is intended to exit.
 *
 * Return: the return status of the command.
 */
static int dispatch_parsed_command(struct command *cmd, int last_rv,
				   bool *shell_should_exit)
{
	/* First, try to see if it's a builtin. */
	for (size_t i = 0; builtin_commands[i].name; i++) {
		if (!strcmp(builtin_commands[i].name, cmd->argv[0])) {
			/* We found a match!  Run it. */
			return builtin_commands[i].handler(
				(const char *const *)cmd->argv, last_rv,
				shell_should_exit);
		}
	}

	/* Otherwise, it's an external command. */
	return dispatch_external_command(cmd);
}

int shell_command_dispatcher(const char *input, int last_rv,
			     bool *shell_should_exit)
{
	int rv;
	struct command *parse_result;
	enum parse_error parse_error = parse_input(input, &parse_result);

	if (parse_error) {
		fprintf(stderr, "Input parse error: %s\n",
			parse_error_str[parse_error]);
		return -1;
	}

	/* Empty line */
	if (!parse_result)
		return last_rv;

	rv = dispatch_parsed_command(parse_result, last_rv, shell_should_exit);
	free_parse_result(parse_result);
	return rv;
}
