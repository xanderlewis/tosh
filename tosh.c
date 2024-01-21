/* tosh -- a very simple shell. */

#include <stdio.h> /* printf(), etc. */
#include <stdlib.h> /* malloc(), exit(), etc. */
#include <unistd.h> /* POSIX syscall stuff */
#include <string.h> /* strtok() and strcmp() */
#include <sys/wait.h> /* waitpid() */

#define OFF 0
#define ON 1

/* Some settings that, for now, are defined as macros. Later, I will add
 * the ability to edit these from the shell (or, equivalently, from a
 * config file.) */
#define PS1 "yes? "

// Global variables
int TOSH_VERBOSE = OFF;

// List of builtin command names.
char *builtin_str[] = {
	"cd",
	"help",
	"quit"
};

// Forward declarations of builtins, and pointers to them.
int tosh_cd(char **);
int tosh_help(char **);
int tosh_quit(char **);
int (*builtin_func[]) (char **) = {
	&tosh_cd,
	&tosh_help,
	&tosh_quit
};

// Number of builtins.
int tosh_num_builtins() {
	return sizeof(builtin_str) / sizeof(char *);
}

// Forward declarations for main()
void tosh_loop(void);

int main(int argc, char **argv) {
	// Load config file.
	// [none, for now.]
	
	// Run command loop.
	tosh_loop();

	// Shutdown/cleanup.
	return EXIT_SUCCESS;
}

// Forward declarations for tosh_loop()
char *tosh_read_line(void);
char **tosh_split_line(char *);
int tosh_exec(char **);

void tosh_loop(void) {
	char *line;
	char **args;
	int status;

	do {
		// Show the prompt
		printf(PS1);

		// Read in a line from stdin
		line = tosh_read_line();

		// Split line into arguments
		args = tosh_split_line(line);

		// Run command (builtin or not)
		status = tosh_exec(args);

		// (these strings live on the heap; we must free them.)
		free(line);
		free(args);
	} while (status);
}

// Buffer increment for read_line function.
#define READ_BUF_INC 1024

char *tosh_read_line(void) {
	int bufsize = READ_BUF_INC;
	int i = 0;
	// Allocate first READ_BUF_INC bytes.
	char *buf = malloc(sizeof(char) * bufsize);
	int c;

	// If we get a null pointer back from malloc...
	if (!buf) {
		fprintf(stderr, "tosh: memory allocation failed. :(\n");
		exit(EXIT_FAILURE);
	}
	while (1) {
		// Read in a character; if we reach EOF or a newline, return the string.
		if ((c = getchar()) == EOF || c == '\n') {
			buf[i++] = '\0';
			return buf;
		} else {
			buf[i++] = c;
		}

		// If we've exceed the buffer...
		if (i >= bufsize) {
			// Ask for READ_BUF_INC more bytes.
			bufsize += READ_BUF_INC;
			buf = realloc(buf, bufsize);
			if (!buf) {
				fprintf(stderr, "tosh: memory allocation failed. :(\n");
				exit(EXIT_FAILURE);
			}
		}
	}
}

// Buffer increment for split function.
#define SPLIT_BUF_INC 64;
// What we consider whitespace between program arguments.
#define SPLIT_DELIM " \t\r\n\a"

char **tosh_split_line(char *line) {
	// Start with SPLIT_BUF_INC bytes.
	int bufsize = SPLIT_BUF_INC;
	int i = 0;
	char **tokens = malloc(bufsize * sizeof(char *));
	char *token;

	// If malloc fails...
	if (!tokens) {
		fprintf(stderr, "tosh: memory allocation failed. :(\n");
		exit(EXIT_FAILURE);
	}

	// Tokenise the line, and get the first token back (or NULL).
	token = strtok(line, SPLIT_DELIM);
	while (token != NULL) {
		tokens[i++] = token;

		// If we've exceeded the buffer, attempt to reallocate.
		if (i >= bufsize) {
			bufsize += SPLIT_BUF_INC;
			tokens = realloc(tokens, bufsize * sizeof(char *));
			if (!tokens) {
				fprintf(stderr, "tosh: memory allocation failed. :(\n");
				exit(EXIT_FAILURE);
			}
		}
		// Get next token from strtok.
		token = strtok(NULL, SPLIT_DELIM);
	}
	tokens[i] = NULL;
	return tokens;
}

int tosh_launch(char **args) {
	pid_t id, wpid;
	int status;

	// Attempt to fork.
	id = fork();
	if (id == 0) {
		// In the child process... exec, passing in argument vector.
		// (also, use the PATH environment variable to find specified program.)
		if (execvp(args[0], args) == -1) {
			perror("tosh");
		}
		// (if we reach this point, the exec() call failed.)
		exit(EXIT_FAILURE);
	} else if (id < 0) {
		// Failed to fork.
		perror("tosh");
	} else {
		// In the parent proces... wait for child.
		if (TOSH_VERBOSE) {
			printf("[launching %s with pid %d]\n", args[0], id);
		}
		do {
			wpid = waitpid(id, &status, WUNTRACED);
		} while (!WIFEXITED(status) && !WIFSIGNALED(status));

		if (TOSH_VERBOSE) {
			printf("[%s terminated with exit code %d]\n", args[0], status);
		}
	}

	return 1;
}

int tosh_exec(char **args) {
	int i;

	if (args[0] == NULL) {
		// Didn't type anything in...
		if (TOSH_VERBOSE) {
			printf("What do you want to do?\n");
		}
		return 1;
	}

	// Check if it's a builtin.
	for (i = 0; i < tosh_num_builtins(); i++) {
		if (strcmp(args[0], builtin_str[i]) == 0) {
			// Run the builtin, and return.
			return (*builtin_func[i])(args);
		}
	}

	// Otherwise, launch the (non-builtin) program.
	return tosh_launch(args);
}

int tosh_cd(char **args) {
	if (args[1] == NULL) {
		fprintf(stderr, "tosh: expected argument to \"cd\"\n");
	} else {
		// Attempt to change working directory of (this) process.
		if (chdir(args[1]) != 0) {
			perror("tosh");
		}
	}
	return 1;
}

int tosh_help(char **args) {
	int i;
	printf("-- TOSH --\n");
	printf("Type program names and arguments, and hit enter.\n");
	printf("The following are built in:\n");

	// List the builtins, according to the strings stored.
	for (i = 0; i < tosh_num_builtins(); i++) {
		printf("  %s\n", builtin_str[i]);
	}

	return 1;
}

int tosh_quit(char **args) {
	if (TOSH_VERBOSE) {
		printf("Bye bye! :)\n");
	}
	return 0;
}
