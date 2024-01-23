/* tosh -- a very simple shell. */

#include <stdio.h> /* printf(), etc. */
#include <stdlib.h> /* malloc(), exit(), etc. */
#include <unistd.h> /* POSIX syscall stuff */
#include <string.h> /* strtok() and strcmp() */
#include <sys/wait.h> /* waitpid() */
#include <signal.h> /* signal(), various macros, etc. */
#include <glob.h>
#include <ctype.h>
#include "tosh.h"

#define MAX_PROMPT 128
#define TOSH_COMMENT_CHAR '#'

#define RED    "\x1B[31m"
#define GRN    "\x1B[32m"
#define YEL    "\x1B[33m"
#define BLU    "\x1B[34m"
#define MAG    "\x1B[35m"
#define CYN    "\x1B[36m"
#define WHT    "\x1B[37m"
#define BLD    "\033[1m"
#define BLDRS  "\033[0m"
#define RESET  "\x1B[0m"

#define UPARRW "^[[A"

#define DEBUG_LOG(A, ...) if (strcmp(TOSH_DEBUG, "ON") == 0) {\
			  	printf(BLD "log: " A BLDRS "\n", __VA_ARGS__);\
			  }	

// Global history file stream
FILE *TOSH_HIST_FILE;

// Pointer to the global struct used for receiving globbing information
glob_t *TOSH_GLOB_STRUCT_PTR;

char *tosh_colours[] = {
	RED,
	GRN,
	YEL,
	BLU,
	MAG,
	CYN,
	WHT
};
int tosh_num_colours(void) {
	return sizeof(tosh_colours) / sizeof(char *);
}

// Global shell options/variables (these are their defaults)
char *TOSH_VERBOSE = "OFF";
char *TOSH_PROMPT = "%n@%h %p2r ⟡ ";
char *TOSH_HIST_PATH = "~/.tosh_history";
char *TOSH_HIST_LEN = "10000";
char *TOSH_CONFIG_PATH = "~/.toshrc";
char *TOSH_DEBUG = "OFF";

// List of global shell options/variables (that can be get and set via environment variables)
char *glob_vars_str[] = {
	"TOSH_VERBOSE",
	"TOSH_PROMPT",
	"TOSH_HIST_PATH",
	"TOSH_CONFIG_PATH",
	"TOSH_DEBUG"
};
char **glob_vars[] = {
	&TOSH_VERBOSE,
	&TOSH_PROMPT,
	&TOSH_HIST_PATH,
	&TOSH_CONFIG_PATH,
	&TOSH_DEBUG
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
char **tosh_expand_args(char **);
void tosh_sync_env_vars(void);
void tosh_record_line(char *);
void tosh_glob_free(void);

/* The main loop: get command line, interpret and act on it, repeat. */
void tosh_loop(void) {
	char *line;
	char **args;
	int status, i;

	do {
		// Sync with environment variables.
		tosh_sync_env_vars();
		
		// Show the prompt (if we're talking to a tty).
		if (isatty(fileno(stdin)))
			tosh_prompt();

		// Read in a line from stdin.
		line = tosh_read_line();

		// Record line in history.
		tosh_record_line(line);

		// Split line into arguments.
		args = tosh_split_line(line);

		// Perform expansions on arguments.
		args = tosh_expand_args(args);

		// Run command (builtin or not).
		status = tosh_execute(args);

		// Free memory used to store command line and arguments (on the heap).
		free(line);
		for (i = 0; args[i] != NULL; i++) {
			free(args[i]);
		}
		free(args);

	} while (status); // Once tosh_execute returns zero, the shell terminates.
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
	if ((c = getchar_unbuf()) == EOF) {
		DEBUG_LOG("first char was EOF.", NULL)
		exit(EXIT_SUCCESS);
	}
	ungetc(c, stdin);
	while (1) {
		// Read in a character; if we reach EOF or a newline, return the string.
		if ((c = getchar_unbuf()) == '\n' || c == EOF) {
			buf[i++] = '\0';
			DEBUG_LOG("finished reading line with %02x.", c)
			return buf;
		} else {
			buf[i++] = c;
			DEBUG_LOG("got char %c.", c)
		}

		// If we've exceed the buffer...
		if (i >= bufsize) {
			// Ask for READ_BUF_INC more bytes.
			DEBUG_LOG("realloc'ing while reading line (%d more bytes)...", READ_BUF_INC)
			bufsize += READ_BUF_INC;
			buf = realloc(buf, bufsize);
			if (!buf) {
				fprintf(stderr, "tosh: memory allocation failed. :(\n");
				exit(EXIT_FAILURE);
			}
		}
	}
}

// Buffer increments for split function.
#define SPLIT_BUF_INC 64
#define ARG_BUF_INC 128
// What we consider whitespace between program arguments.
#define SPLIT_DELIM " \t\r\n\a"

/* Split up the given line into a vector of (string) arguments, ignoring everything after a
 * comment character.
 * NOTE: a # not following whitespace (that is, not separated from what it precedes) will be
 *       taken literally. */
char **tosh_split_line(char *line) {
	// Start with SPLIT_BUF_INC bytes.
	int bufsize = SPLIT_BUF_INC;
	int arg_bufsize = ARG_BUF_INC;
	int i = 0, j;
	char **tokens = malloc(bufsize * sizeof(char *));
	char *token;
	char c;

	// If malloc fails...
	if (!tokens) {
		fprintf(stderr, "tosh: memory allocation failed. :(\n");
		exit(EXIT_FAILURE);
	}

	// Tokenise the line, and get the first token back (or NULL).
	token = strtok(line, SPLIT_DELIM);
	while (token != NULL && token[0] != TOSH_COMMENT_CHAR) {
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
	
	// Replace each element of `tokens` with a dynamically-allocated string (a copy of the original string at that point).
	for (i = 0; tokens[i] != NULL; i++) {
		char *p = malloc(arg_bufsize * sizeof(char));
		for (j = 0; (c = tokens[i][j]) != '\0'; j++) {
			if (j >= arg_bufsize) {
				arg_bufsize += ARG_BUF_INC;
				DEBUG_LOG("realloc'ing whilst reading in argument (%d more bytes)...", ARG_BUF_INC)
				p = realloc(p, arg_bufsize * sizeof(char));
			}
			p[j] = tokens[i][j];
			DEBUG_LOG("copied char %c.", c)
		}
		tokens[i] = p;
	}

	DEBUG_LOG("returning tokens after splitting...", NULL)
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
		if (strcmp(TOSH_VERBOSE, "ON") == 0 && isatty(fileno(stdin))) {
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

// Forward declarations for tosh_prompt()
void tosh_show_path(char *, int, int);

// Increment for buffers related to showing the prompt.
#define PROMPT_BUF_INC 1024

/* show the prompt according to the global variable TOSH_PROMPT */
void tosh_prompt(void) {
	int i, c, levels = 0;
	int bufsize = PROMPT_BUF_INC;
	char *buf = malloc(bufsize * sizeof(char));
	if (buf == NULL) {
		fprintf(stderr, "tosh: memory allocation failed. :(\n");
		exit(EXIT_FAILURE);
	}
	char *username;

	// Parse TOSH_PROMPT string...
	for (i = 0; (c = TOSH_PROMPT[i]) != '\0'; i++) {
		if (c == '%') {
			// Parse specifiers.
			switch (TOSH_PROMPT[++i]) {
				case 'p':
					// CURRENT WORKING DIRECTORY (FOR NOW, THE ABSOLUTE PATH)
					while (getcwd(buf, bufsize) == NULL) {
						// Buffer overflow; reallocate.
						bufsize += PROMPT_BUF_INC;
						buf = realloc(buf, bufsize);
						if (buf == NULL) {
							fprintf(stderr, "tosh: memory allocation failed. :(\n");
							exit(EXIT_FAILURE);
						}
					}
					if (isdigit(TOSH_PROMPT[i+1])) {
						levels = atoi(&TOSH_PROMPT[i+1]);
						i++;
					}
					if (TOSH_PROMPT[i+1] == 'r') {
						tosh_show_path(buf, levels, 1);
						i++;
					} else {
						tosh_show_path(buf, levels, 0);
					}
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

/* Expand the given string (could be a command argument, or else) according to tosh's expansion rules.
 * NOTE: takes a char pointer that is assumed to have been malloc'd earlier; requires freeing later.
 *       this function is recursive; possibly not particularly efficient. */
char *tosh_expand_string(char *str) {
	char *homedir, *tildeloc, *remstr;

	if ((homedir = getenv("HOME")) == NULL) {
		fprintf(stderr, "tosh: I couldn't find your home directory. :(\n");
		return str;
	}

	str = realloc(str, (strlen(str) - 1 + strlen(homedir) + 1) * sizeof(char));

	if ((tildeloc = strchr(str, '~')) == NULL) {
		return str;
	}

	// Copy remainder of string
	remstr = malloc((strlen(tildeloc) + 1) * sizeof(char));
	strcpy(remstr, tildeloc + 1);
	// Copy value of HOME from tilde onwards
	strcpy(tildeloc, homedir);
	// Copy rest of string after HOME
	strcpy(tildeloc + strlen(homedir), remstr);
	free(remstr);
	
	return tosh_expand_string(str);
}


// Forward declarations for tosh_expand_args.
char **tosh_glob_string(char *);
void tosh_glob_free(void);

#define TOSH_EXPAND_BUF_INC 64

/* Perform expansion on each of the arguments in the argument vector. */
char **tosh_expand_args(char **args) {
	int i, j, k = 0;
	int bufsize = TOSH_EXPAND_BUF_INC;
	char **globbed, **newargs, *matchedstr;

	newargs = malloc(bufsize * sizeof(char *));
	// Iterate through args, replacing them with their expansions.
	for (i = 0; args[i] != NULL; i++) {
		DEBUG_LOG("expanding arg: %s...", args[i]);
		// Expand stuff like tilde.
		args[i] = tosh_expand_string(args[i]);
		DEBUG_LOG("expanded into %s.", args[i]);
		// Perform globbing using metacharacters.
		if ((globbed = tosh_glob_string(args[i])) == NULL) {
			// If nothing matched, leave it as it was. (this behaviour is perhaps debatable?)
			DEBUG_LOG("nothing matched.", NULL)
			DEBUG_LOG("copying %s...", args[i])
			newargs[k++] = args[i];

			// Increase buffer size (total number of arguments) if necessary.
			if (k >= bufsize) {
				bufsize += TOSH_EXPAND_BUF_INC;
				newargs = realloc(newargs, bufsize * sizeof(char));
			}
		} else {
			// If matched, add in matches as new args.
			for (j = 0; (matchedstr = globbed[j]) != NULL; j++) {
				// Copy globbed filename (this will be deallocated in tosh_loop()).
				DEBUG_LOG("found %s.", matchedstr)
				newargs[k] = malloc((strlen(matchedstr) + 1) * sizeof(char));
				strcpy(newargs[k++], matchedstr);

				// Increase buffer size (total number of arguments) if necessary.
				if (k >= bufsize) {
					bufsize += TOSH_EXPAND_BUF_INC;
					newargs = realloc(newargs, bufsize * sizeof(char));
				}

			}
		}
	}
	// Free glob structure if we used it.
	if (i > 0)
		tosh_glob_free();

	return newargs;
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
					case 'd':
						TOSH_DEBUG = "ON";
						break;
					default:
						fprintf(stderr, "tosh: I don't know the option '%c'.\n", argv[i][j]);
						break;
				}
			}
		} else {
			// Non-flag arguments...
			// Attempt to read commands from the specified file, and ignore the rest.
			DEBUG_LOG("reading from file '%s'...", argv[i])
			freopen(argv[i], "r", stdin);
			return;	
		}
	}
}

/* Print the given path (going back n levels) to stdout, possibly colouring it.
 * Shows the absolute path if n is zero. */
void tosh_show_path(char *path, int n, int rainbow) {
	char *comp = strtok(path, "/");
	char *components[strlen(path)]; // (this is obviously usually too long, but whatever)
	int i, j = 0;
	if (path[0] == '/')
		printf("/");
	// Split path into components
	for (i = 0; comp != NULL; i++) {
		components[i] = comp;
		comp = strtok(NULL, "/");
	}
	components[i] = NULL;

	if (n == 0) {
		n = i;
	}

	// (at this point, i is the index of the terminating null pointer)
	// Show last n components.
	for (i = (i < n) ? 0 : i - n; components[i] != NULL; i++) {
		printf("%s%s", (rainbow) ? tosh_colours[j++] : "", components[i]);
		printf("%s/", (rainbow) ? RESET : "");
		j = (j + 1) % tosh_num_colours();
	}
}

/* Return a list of matched paths for a given string pattern. */
char **tosh_glob_string(char *arg) {
	char **paths, *buf, c;

	// Create a glob_t structure to hold returned information from system.
	static glob_t gstruct;
	DEBUG_LOG("setting pointer to global glob struct...", NULL)
	TOSH_GLOB_STRUCT_PTR = &gstruct;

	// Glob pattern; return null pointer if nothing matched.
	if (glob(arg, 0, NULL, &gstruct) == GLOB_NOMATCH) {
		return NULL;
	}

	// Return vector of matches.
	return gstruct.gl_pathv;
}

void tosh_glob_free(void) {
	// Free the global glob struct.
	DEBUG_LOG("freeing global glob struct @0x%p...", TOSH_GLOB_STRUCT_PTR)
	globfree(TOSH_GLOB_STRUCT_PTR);
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
	char *path, *expanded_path;

	path = malloc((strlen(TOSH_HIST_PATH) + 1) * sizeof(char));
	strcpy(path, TOSH_HIST_PATH);
	expanded_path = tosh_expand_string(path);

	TOSH_HIST_FILE = fopen(expanded_path, "r+");
	free(expanded_path);

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
	printf(BLD "\n---=== TOSH — a very simple shell. ===---\n" BLDRS);
	printf("\nType program names and arguments, and hit enter.\n");
	printf("The following are built in:\n");

	// List the builtins, according to the strings stored.
	for (i = 0; i < tosh_num_builtins(); i++) {
		printf("- %s\n", builtin_str[i]);
	}
	printf("\n");

	return 1;
}

int tosh_quit(char **args) {
	if (strcmp(TOSH_VERBOSE, "ON") == 0) {
		printf("Bye bye! :)\n");
	}
	return 0;
}
