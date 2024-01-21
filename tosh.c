/* tosh -- a very simple shell. */

#include <stdio.h> /* printf(), etc. */
#include <stdlib.h> /* malloc(), exit(), etc. */
#include <unistd.h> /* POSIX syscall stuff */
#include <string.h> /* strtok() and strcmp() */
#include <sys/wait.h> /* waitpid() */
#include <signal.h> /* signal(), various macros, etc. */

#define OFF 0
#define ON 1
#define MAX_PROMPT 128

// Global variables
int TOSH_VERBOSE = OFF;
char TOSH_PROMPT[MAX_PROMPT] = "%n@%h %p λ ";

// List of builtin command names.
char *builtin_str[] = {
	"cd",
	"help",
	"quit" };

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
void tosh_parse_args(int, char **);
void tosh_bind_signals(void);

int main(int argc, char **argv) {
	// Parse arguments to tosh
	tosh_parse_args(argc, argv);

	// Load config file.
	// [none, for now.]
	
	// Set up signal handlers
	tosh_bind_signals();
	
	// Run command loop.
	tosh_loop();

	// GREAT SUCCESS!!!
	return EXIT_SUCCESS;
}

// Forward declarations for tosh_loop()
char *tosh_read_line(void);
char **tosh_split_line(char *);
int tosh_exec(char **);
void tosh_prompt(void);
void tosh_expand(char **);

void tosh_loop(void) {
	char *line;
	char **args;
	int status, i;

	do {
		// Show the prompt
		tosh_prompt();

		// Read in a line from stdin
		line = tosh_read_line();

		// Split line into arguments
		args = tosh_split_line(line);

		// Perform expansions on arguments
		tosh_expand(args);

		// Run command (builtin or not)
		status = tosh_exec(args);

		// Free memory used to store command line and arguments (on the heap).
		free(line);
		free(args);

		// Free memory used for individual arguments (some may have been expanded).
		/*for (i = 0; args[i] != NULL; i++) {
			free(args[i]);
		}*/
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
		if ((c = getchar()) == '\n' || c == EOF) {
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

/* (Attempt to) fork and exec a requested program */
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

/* Execute a command line (and either call an external program or a builtin).*/
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
			if (TOSH_VERBOSE) {
				printf("[launching builtin %s]\n", args[0]);
			}
			return (*builtin_func[i])(args);
		}
	}

	// Otherwise, launch the (non-builtin) program.
	return tosh_launch(args);
}

// Increment for buffers related to showing the prompt.
#define PROMPT_BUF_INC 1024

/* show the prompt according to the global variable TOSH_PROMPT */
void tosh_prompt(void) {
	int i, c;
	int bufsize = PROMPT_BUF_INC;
	char *buf = malloc(bufsize * sizeof(char));
	if (buf == NULL) {
		fprintf(stderr, "tosh: memory allocation failed. :(\n");
		exit(EXIT_FAILURE);
	}
	char *username;

	for (i = 0; (c = TOSH_PROMPT[i]) != '\0'; i++) {
		if (c == '%') {
			// Parse specifiers.
			switch (TOSH_PROMPT[++i]) {
				case 'p':
					// CURRENT WORKING DIRECTORY (FOR NOW, ABSOLUTE PATH)
					while (getcwd(buf, bufsize) == NULL) {
						// Buffer overflow; reallocate.
						bufsize += PROMPT_BUF_INC;
						buf = realloc(buf, bufsize);
						if (buf == NULL) {
							fprintf(stderr, "tosh: memory allocation failed. :(\n");
							exit(EXIT_FAILURE);
						}
					}
					printf("%s", buf);
					break;

				case 'n':
					// USERNAME
					if ((username = getenv("USER")) != NULL) {
						printf("%s", username);
					} else {
						fprintf(stderr, "tosh: I couldn't find your username. :(\n");
					}
					break;
				case 'h':
					// HOSTNAME
					while (gethostname(buf, bufsize) != 0) {
						// Buffer overflow; reallocate.
						bufsize += PROMPT_BUF_INC;
						buf = realloc(buf, bufsize);
						if (buf == NULL) {
							fprintf(stderr, "tosh: memory allocation failed. :(\n");
							exit(EXIT_FAILURE);
						}

					}
					printf("%s", buf);
					break;
			}
		} else {
			printf("%c", c);
		}
	}
	free(buf);
	fflush(stdout);
}

void tosh_expand(char **args) {
	int c, i, j, homedirlen, arglen;
	char *homedir, *original_arg;
	for (i = 0; args[i] != NULL; i++) {
		for (j = 0; (c = args[i][j]) != '\0'; j++) {
			switch (c) {
				case '~':
					// Get $HOME.
					if ((homedir = getenv("HOME")) == NULL) {
						fprintf(stderr, "tosh: I couldn't find your home directory. :(\n");
					} else {
						// Expand tilde.
						homedirlen = strlen(homedir);
						arglen = strlen(args[i]);

						original_arg = malloc(arglen * sizeof(char));
						strcpy(original_arg, args[i]);

						args[i] = malloc((arglen - 1 + homedirlen + 1) * sizeof(char));

						memcpy(args[i], original_arg, j);
						memcpy(args[i] + j, homedir, homedirlen);
						memcpy(args[i] + j + homedirlen, original_arg + j + 1, arglen - j - 1);
						args[i][j + homedirlen + arglen + j + 1] = '\0';
						free(original_arg);
					}
					break;
				default:
					break;
			}
		}
	}
}

void tosh_parse_args(int argc, char **argv) {
	int i, j;
	if (argc == 1) {
		return;
	}
	// Iterate over arguments.
	for (i = 1; i < argc; i++) {
		if (argv[i][0] == '-') {
			// Parse flags.
			for (j = 1; argv[i][j] != '\0'; j++) {
				switch (argv[i][j]) {
					case 'v':
						TOSH_VERBOSE = ON;
						break;
					default:
						fprintf(stderr, "tosh: I don't know the option '%c'.\n", argv[i][j]);
						break;
				}
			}
		}
	}
}

void tosh_sigint(int sig) {
	if (TOSH_VERBOSE) {
		printf("\nRecieved a SIGINT!\n");
	}
	exit(EXIT_SUCCESS);
}

void tosh_bind_signals() {
	signal(SIGINT, tosh_sigint);
}

/* -- BUILTINS BELOW -- */

int tosh_cd(char **args) {
	if (args[1] == NULL) {
		char *homedir = getenv("HOME");
		if (homedir != NULL) {
			args[1] = homedir;
			return tosh_cd(args);
		}
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
