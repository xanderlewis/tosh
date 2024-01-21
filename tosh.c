/* tosh -- a very simple shell. */

#include <stdio.h> /* printf(), etc. */
#include <stdlib.h> /* malloc(), exit(), etc. */
#include <unistd.h> /* POSIX syscall stuff */
#include <string.h> /* strtok() and strcmp() */
#include <sys/wait.h> /* waitpid() */
#include <signal.h> /* signal(), various macros, etc. */

#define MAX_PROMPT 128

#define RED   "\x1B[31m"
#define GRN   "\x1B[32m"
#define YEL   "\x1B[33m"
#define BLU   "\x1B[34m"
#define MAG   "\x1B[35m"
#define CYN   "\x1B[36m"
#define WHT   "\x1B[37m"
#define BLD   "\033[1m"
#define BLDRS "\033[0m"
#define RESET "\x1B[0m"

#define UPARRW "^[[A"

// Global variables
FILE *TOSH_HIST_FILE;

// Global shell options (these are their defaults)
char *TOSH_VERBOSE = "OFF";
char *TOSH_PROMPT = "%n@%h %p 𝕋 ";
char *TOSH_HIST_PATH = "/Users/xml/.tosh_history"; // "~/.tosh_history";
char *TOSH_HIST_LEN = "1000";
char *TOSH_CONFIG_PATH = "/Users/xml/.toshrc";

// List of global shell options (that can be get and set via environment variables)
char *glob_vars_str[] = {
	"TOSH_VERBOSE",
	"TOSH_PROMPT",
	"TOSH_HIST_PATH",
	"TOSH_HIST_LEN",
	"TOSH_CONFIG_PATH"
};
char **glob_vars[] = {
	&TOSH_VERBOSE,
	&TOSH_PROMPT,
	&TOSH_HIST_PATH,
	&TOSH_HIST_LEN,
	&TOSH_CONFIG_PATH
};

// Number of global shell options
int tosh_num_glob(void) {
	return sizeof(glob_vars_str) / sizeof(char *);
}

// List of builtin command names.
char *builtin_str[] = {
	"cd",
	"exec",
	"help",
	"quit" };

// Forward declarations of builtins, and pointers to them.
int tosh_cd(char **);
int tosh_exec(char **);
int tosh_help(char **);
int tosh_quit(char **);
int (*builtin_func[]) (char **) = {
	&tosh_cd,
	&tosh_exec,
	&tosh_help,
	&tosh_quit
};

// Number of builtins.
int tosh_num_builtins(void) {
	return sizeof(builtin_str) / sizeof(char *);
}

// Forward declarations for main()
void tosh_loop(void);
void tosh_parse_args(int, char **);
void tosh_bind_signals(void);
void tosh_open_hist(void);
void tosh_close_hist(void);
void tosh_sync_env_vars(void);
void tosh_load_config(void);

int main(int argc, char **argv) {
	// Parse arguments to tosh
	tosh_parse_args(argc, argv);

	// Load config file. [none, for now.]
	tosh_load_config();
	
	// Set up signal handlers
	tosh_bind_signals();

	// Sync with environment variables
	tosh_sync_env_vars();

	// Open history file
	tosh_open_hist();
	
	// Run command loop.
	tosh_loop();

	// Close history file
	tosh_close_hist();

	// GREAT SUCCESS!!!
	return EXIT_SUCCESS;
}

// Forward declarations for tosh_loop()
char *tosh_read_line(void);
char **tosh_split_line(char *);
int tosh_execute(char **);
void tosh_prompt(void);
void tosh_expand(char **);
void tosh_sync_env_vars(void);
void tosh_record_line(char *);

/* The main loop: get command line, interpret and act on it, repeat. */
void tosh_loop(void) {
	char *line;
	char **args;
	int status, i;

	do {
		// Sync with environment variables.
		tosh_sync_env_vars();
		
		// Show the prompt.
		tosh_prompt();

		// Read in a line from stdin.
		line = tosh_read_line();

		// Record line in history.
		tosh_record_line(line);

		// Split line into arguments.
		args = tosh_split_line(line);

		// Perform expansions on arguments.
		tosh_expand(args);

		// Run command (builtin or not).
		status = tosh_execute(args);

		// Free memory used to store command line and arguments (on the heap).
		free(line);
		free(args);

		// Free memory used for individual arguments (some may have been expanded).
		/*for (i = 0; args[i] != NULL; i++) {
			free(args[i]);
		}*/

		// Once tosh_execute returns zero, the shell terminates.
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
	// If the first character is already EOF...
	if ((c = getchar()) == EOF) {
		exit(EXIT_SUCCESS);
	}
	ungetc(c, stdin);
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

/* Fork and exec a requested external program */
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
		if (strcmp(TOSH_VERBOSE, "ON") == 0) {
			printf("[launching %s with pid %d]\n", args[0], id);
		}
		do {
			wpid = waitpid(id, &status, WUNTRACED);
		} while (!WIFEXITED(status) && !WIFSIGNALED(status));

		if (strcmp(TOSH_VERBOSE, "ON") == 0) {
			printf("[%s terminated with exit code %d]\n", args[0], status);
		}
	}

	return 1;
}

/* Execute a command line (and either call an external program or a builtin).*/
int tosh_execute(char **args) {
	int i;

	if (args[0] == NULL) {
		// Didn't type anything in...
		if (strcmp(TOSH_VERBOSE, "ON") == 0) {
			printf("\n...what do you want to do?\n");
		}
		return 1;
	}

	// Check if it's a builtin.
	for (i = 0; i < tosh_num_builtins(); i++) {
		if (strcmp(args[0], builtin_str[i]) == 0) {
			// Run the builtin, and return.
			if (strcmp(TOSH_VERBOSE, "ON") == 0) {
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
						printf(RED "%s" RESET, username);
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
					printf(GRN "%s" RESET, buf);
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
						TOSH_VERBOSE = "ON";
						break;
					default:
						fprintf(stderr, "tosh: I don't know the option '%c'.\n", argv[i][j]);
						break;
				}
			}
		}
	}
}

/* Get and set environment variables to align with global (internal) shell variables.
 * Check for their presence first; use internal defaults if they don't exist. */
void tosh_sync_env_vars(void) {
	int i;
	char *s;
	for (i = 0; i < tosh_num_glob(); i++) {
		if ((s = getenv(glob_vars_str[i])) == NULL) {
			// Couldn't find this environment variable -- we'll create it.
			setenv(glob_vars_str[i], *(glob_vars[i]), 0);
		} else {
			// Found it; set internal value in accordance.
			*(glob_vars[i]) = s;
		}

	}
}

void tosh_sigint(int sig) {
	if (strcmp(TOSH_VERBOSE, "ON") == 0) {
		printf("\nRecieved a SIGINT!\n");
	}
	exit(EXIT_SUCCESS);
}

void tosh_bind_signals(void) {
	signal(SIGINT, tosh_sigint);
}

void tosh_record_line(char *line) {
	int linelen = strlen(line);
	if (linelen == 0) {
		return;
	}
	if (fwrite(line, sizeof(char), linelen, TOSH_HIST_FILE) < linelen) {
		fprintf(stderr, "tosh: I couldn't write everything to the history file. :(\n");
	} else {
		fwrite("\n", sizeof(char), 1, TOSH_HIST_FILE);
	}
	fflush(TOSH_HIST_FILE);
}

void tosh_open_hist(void) {
	TOSH_HIST_FILE = fopen(TOSH_HIST_PATH, "a+");
	if (TOSH_HIST_FILE == NULL) {
		perror("tosh");
		fprintf(stderr, "tosh: I couldn't open the history file. :(\n");
	}
}

void tosh_close_hist(void) {
	if (fclose(TOSH_HIST_FILE) == EOF) {
		fprintf(stderr, "tosh: I couldn't close the history file. :(\n");
	}
}

void tosh_load_config(void) {
	;
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

/* Builtin wrapper for exec() syscall. */
int tosh_exec(char **args) {
	if (args[1] != NULL) {
		if (execvp(args[1], args + 1) == -1) {
			perror("tosh");
		}
	}
	// (We should never end up here!)
	return 0;
}

int tosh_help(char **args) {
	int i;
	printf(BLD "TOSH -- a very simple shell.\n" BLDRS);
	printf("Type program names and arguments, and hit enter.\n");
	printf("The following are built in:\n");

	// List the builtins, according to the strings stored.
	for (i = 0; i < tosh_num_builtins(); i++) {
		printf("  %s\n", builtin_str[i]);
	}

	return 1;
}

int tosh_quit(char **args) {
	if (strcmp(TOSH_VERBOSE, "ON") == 0) {
		printf("Bye bye! :)\n");
	}
	return 0;
}
