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

// Various global constants
#define TOSH_MAX_PROMPT        128
#define TOSH_MAX_CHILD         128
#define TOSH_COMMENT_CHAR '#'
#define TOSH_MAX_PATH     4096

// (There is often a symbolic constant PATH_MAX defined in
// <limits.h> on POSIX systems, but on macOS there either
// isn't a limit or it isn't defined in this way.)

// Colours
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


#define DEBUG_LOG(A, ...) if (strcmp(TOSH_DEBUG, "ON") == 0) {\
			  	fprintf(stderr, BLD "log: " A BLDRS "\n", __VA_ARGS__);\
			  }	

// Global history file stream
FILE *TOSH_HIST_FILE;

// Pointer to the global struct used for receiving globbing information
glob_t *TOSH_GLOB_STRUCT_PTR;

// (Chronologically) previous directory
char TOSH_LAST_DIR[TOSH_MAX_PATH]; // [note: 256 bytes is the max length of a dirname in Unix]

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
char *TOSH_FORCE_INTERACTIVE = "OFF";
char *ENV_PATH;
char *ENV_MANPATH;
char *ENV_SHLVL;

// List of global shell options/variables (that can be get and set via environment variables)
char *glob_vars_str[] = {
	"TOSH_VERBOSE",
	"TOSH_PROMPT",
	"TOSH_HIST_PATH",
	"TOSH_CONFIG_PATH",
	"TOSH_DEBUG",
	"TOSH_FORCE_INTERACTIVE",
	"PATH",
	"MANPATH",
	"SHLVL"
};
char **glob_vars[] = {
	&TOSH_VERBOSE,
	&TOSH_PROMPT,
	&TOSH_HIST_PATH,
	&TOSH_CONFIG_PATH,
	&TOSH_DEBUG,
	&TOSH_FORCE_INTERACTIVE,
	&ENV_PATH,
	&ENV_MANPATH,
	&ENV_SHLVL
};

// Number of global shell options
int tosh_num_glob(void) {
	return sizeof(glob_vars_str) / sizeof(char *);
}

// List of builtin command names.
char *builtin_str[] = {
	"cd",
	"showenv",
	"exec",
	"readconfig",
	"help",
	"quit" };

// Forward declarations of builtins, and pointers to them.
int tosh_cd(char **);
int tosh_showenv(char **);
int tosh_exec(char **);
int tosh_readconfig(char **);
int tosh_help(char **);
int tosh_quit(char **);
int (*builtin_func[]) (char **) = {
	&tosh_cd,
	&tosh_showenv,
	&tosh_exec,
	&tosh_readconfig,
	&tosh_help,
	&tosh_quit
};

// Number of builtins.
int tosh_num_builtins(void) {
	return sizeof(builtin_str) / sizeof(char *);
}

// Forward declarations for main()
void tosh_loop(int);
void tosh_parse_args(int, char **);
void tosh_bind_signals(void);
void tosh_open_hist(void);
void tosh_close_hist(void);
void tosh_sync_env_vars(void);
void tosh_load_config(void);
void tosh_init(void);


int main(int argc, char **argv) {
	// Parse (external) arguments to tosh.
	tosh_parse_args(argc, argv);

	// Load config file. [none, for now.]
	tosh_load_config();
	
	// Set up signal handlers.
	tosh_bind_signals();

	// Sync with environment variables.
	tosh_sync_env_vars();

	// Do general initialisation stuff.
	tosh_init();

	// Open history file.
	tosh_open_hist();

	// Run command loop.
	tosh_loop(1);

	// Close history file.
	tosh_close_hist();

	// GREAT SUCCESS!!!
	return EXIT_SUCCESS;
}

// Forward declarations for tosh_loop()
char *tosh_read_line(void);
char **tosh_split_line(char *);
//char **tosh_split_line_new(char *);
int tosh_execute(char **);
void tosh_prompt(void);
char **tosh_expand_args(char **);
void tosh_sync_env_vars(void);
void tosh_record_line(char *);
void tosh_glob_free(void);

/* The main loop: get command line, interpret and act on it, repeat. */
void tosh_loop(int loop) {
	char *line;
	char **args;
	int status, i;

	do {
		// Show the prompt (if we're talking to a tty).
		if (isatty(fileno(stdin)) || strcmp(TOSH_FORCE_INTERACTIVE, "ON") == 0)
			tosh_prompt();

		// Read in a line from stdin.
		line = tosh_read_line();

		// Record line in history.
		tosh_record_line(line);

		// Split line into arguments.
		args = tosh_split_line(line);

		if (args != NULL) {
			// Perform expansions on arguments.
			args = tosh_expand_args(args);

			// Run command (builtin or not).
			status = tosh_execute(args);
			// Sync with environment variables.
			tosh_sync_env_vars();

			// Free memory used to store command line and arguments (on the heap).
			free(line);
			for (i = 0; args[i] != NULL; i++) {
				free(args[i]);
			}
			free(args);
		}

	} while (status && loop); // Once tosh_execute returns zero, the shell terminates.
				  // We also terminate if loop is false.
}

// Buffer increment for read_line function.
#define READ_BUF_INC 1024

char *tosh_read_line(void) {
	int bufsize = READ_BUF_INC;
	int i, c;
	char *buf;
	// Allocate first READ_BUF_INC bytes.
	buf = malloc(sizeof(char) * bufsize);

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

	i = 0;

	while (1) {
		// Read in a character; if we reach EOF or a newline, return the string.
		if ((c = getchar_unbuf()) == '\n' || c == EOF || c == '\0') {
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

// Buffer increments for splitting lines.
#define ARG_BUF_INC 128
#define LINE_BUF_INC 64

/* Convert a given line (string) into a list of (string) arguments. */
char **tosh_split_line(char *line) {
	int bl, q, i, j, c, argbufsize, linebufsize, num_args;
	char **tokens, **tp;

	argbufsize = ARG_BUF_INC;
	linebufsize = LINE_BUF_INC;
	i = j = 0;
	q = 0;
	bl = 0;
	num_args = 0;
	tokens = malloc(linebufsize * sizeof(char *));
	tp = tokens;

	*tp = malloc(argbufsize * sizeof(char));

	if (!tokens || !tp) {
		fprintf(stderr, "tosh: memory allocation failed. :(\n");
		exit(EXIT_FAILURE);
	}

	while (bl >= 0) {
		c = line[i++];
		switch (c) {
			case '(':
				if (!q)
					bl++;
				(*tp)[j++] = c;
				break;
			case ')':
				if (!q)
					bl--;
				(*tp)[j++] = c;
				break;
			case '\'':
				q = (q) ? 0 : 1;
				break;
			case '\\':
				if (line[i] == '\'') {
					printf("we have an escaped quote.\n");
					(*tp)[j++] = '\'';
					i++;
				} else if (line[i] == '\\') {
					printf("we have an escaped backslash.\n");
					(*tp)[j++] = '\\';
					i++;
				} 
				break;
			case ' ':
				if (bl == 0 && q == 0) {
					// Allocate more memory for line if needed.
					while (num_args * sizeof(char *) >= linebufsize) {
						linebufsize += LINE_BUF_INC;
						DEBUG_LOG("realloc'ing whilst parsing line (%d more bytes)...", LINE_BUF_INC)
						tokens = realloc(tokens, linebufsize * sizeof(char *));
						if (!tokens) {
							fprintf(stderr, "tosh: memory allocation failed. :(\n");
							exit(EXIT_FAILURE);
						}
					}
					// Point to next token.
					argbufsize = ARG_BUF_INC;
					tp++;
					*tp = malloc(argbufsize * sizeof(char));
					if (!*tp) {
						fprintf(stderr, "tosh: memory allocation failed. :(\n");
						exit(EXIT_FAILURE);
					}
					j = 0;
					num_args++;
				} else {
					(*tp)[j++] = c;	
				}
				break;
			case EOF:
			case '\n':
			case '\0':
			case TOSH_COMMENT_CHAR:
				if (bl != 0) {
					fprintf(stderr, "tosh: mismatched brackets. :(\n");
					return NULL;
				} else if (q != 0) {
					fprintf(stderr, "tosh: mismatched quotes. :(\n");
					return NULL;
				}
				if (num_args == 0 && strlen(*tp) == 0) {
					DEBUG_LOG("no arguments.", NULL)
					return NULL;
				}
				tp++;
				tp = NULL;
				return tokens;
			default:
				// Allocate more memory for argument if needed.
				while (j * sizeof(char) >= argbufsize) {
					argbufsize += ARG_BUF_INC;
					DEBUG_LOG("realloc'ing whilst parsing argument (%d more bytes)...", ARG_BUF_INC)
					tp = realloc(tp, argbufsize * sizeof(char));
					if (!tp) {
						fprintf(stderr, "tosh: memory allocation failed. :(\n");
						exit(EXIT_FAILURE);
					}
				}
				(*tp)[j++] = c;
				break;
		}
	}

	fprintf(stderr, "tosh: mismatched brackets. :(\n");
	return NULL;
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
		// This child process inherits stdin and stdout file descriptors, and so
		// can still talk to whoever/whatever the original shell was connected to.
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
			printf("[%s terminated with exit code %d]\n", args[0], status / 256);
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
	if (!buf) {
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
						if (!buf) {
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
						if (!buf) {
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
char *tosh_expand_tilde(char *str) {
	char *homedir, *tildeloc, *remstr;

	if ((homedir = getenv("HOME")) == NULL) {
		fprintf(stderr, "tosh: I couldn't find your home directory. :(\n");
		return str;
	}

	str = realloc(str, (strlen(str) - 1 + strlen(homedir) + 1) * sizeof(char));
	if (!str) {
		fprintf(stderr, "tosh: memory allocation failed. :(\n");
		exit(EXIT_FAILURE);
	}

	// If there isn't a tilde...
	if ((tildeloc = strchr(str, '~')) == NULL) {
		return str;
	}

	// Copy remainder of string.
	remstr = malloc((strlen(tildeloc) + 1) * sizeof(char));
	if (!remstr) {
		fprintf(stderr, "tosh: memory allocation failed. :(\n");
		exit(EXIT_FAILURE);
	}
	strcpy(remstr, tildeloc + 1);
	// Copy value of HOME from tilde onwards.
	strcpy(tildeloc, homedir);
	// Copy rest of string after HOME.
	strcpy(tildeloc + strlen(homedir), remstr);
	free(remstr);
	
	return tosh_expand_tilde(str);
}

/* Expand expressions that are intended to be evaluated (e.g. '$PATH') in the given string.
 * As with tosh_expand_tilde(), takes a pointer that is assumed to have been malloc'd earlier;
 * requires freeing later. */
char *tosh_expand_expressions(char *str) {
	return "";
}


// Forward declarations for tosh_expand_args.
char **tosh_glob_string(char *);
void tosh_glob_free(void);
char *tosh_expand_expression(char *);

#define TOSH_EXPAND_BUF_INC 64

/* Perform expansion on each of the arguments in the argument vector. */
char **tosh_expand_args(char **args) {
	int i, j, k = 0;
	int bufsize = TOSH_EXPAND_BUF_INC;
	char **globbed, **newargs, *matchedstr, *newarg;

	newargs = malloc(bufsize * sizeof(char *));
	// Iterate through args, replacing them with their expansions.
	for (i = 0; args[i] != NULL; i++) {
		DEBUG_LOG("expanding arg: %s...", args[i]);

		// Expand tilde.
		args[i] = tosh_expand_tilde(args[i]);
		DEBUG_LOG("tilde expanded into %s.", args[i]);

		// Expand any $(EXPRESSION)s (for now, only the first occurrence we find).
		if ((newarg = tosh_expand_expression(args[i])) != NULL) {
			args[i] = newarg;
		} else {
			DEBUG_LOG("no expression to expand.", NULL)
		}
		DEBUG_LOG("further expanded into %s.", args[i]);

		// Perform globbing using metacharacters.
		if ((globbed = tosh_glob_string(args[i])) == NULL) {
			// If nothing matched, leave it as it was. (this behaviour is perhaps debatable?)
			DEBUG_LOG("nothing matched.", NULL)
			newargs[k++] = args[i];

			// Increase buffer size (total number of arguments) if necessary.
			if (k >= bufsize) {
				bufsize += TOSH_EXPAND_BUF_INC;
				newargs = realloc(newargs, bufsize * sizeof(char));
				if (!newargs) {
					fprintf(stderr, "tosh: memory allocation failed. :(\n");
					exit(EXIT_FAILURE);
				}
			}
		} else {
			// If matched, add in matches as new args.
			for (j = 0; (matchedstr = globbed[j]) != NULL; j++) {
				// Copy globbed filename (this will be deallocated in tosh_loop()).
				DEBUG_LOG("found %s.", matchedstr)
				newargs[k] = malloc((strlen(matchedstr) + 1) * sizeof(char));
				if (!newargs[k]) {
					fprintf(stderr, "tosh: memory allocation failed. :(\n");
					exit(EXIT_FAILURE);
				}
				strcpy(newargs[k++], matchedstr);

				// Increase buffer size (total number of arguments) if necessary.
				if (k >= bufsize) {
					bufsize += TOSH_EXPAND_BUF_INC;
					newargs = realloc(newargs, bufsize * sizeof(char));
					if (!newargs) {
						fprintf(stderr, "tosh: memory allocation failed. :(\n");
						exit(EXIT_FAILURE);
					}
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
					case 'i':
						TOSH_FORCE_INTERACTIVE = "ON";
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

/* Print the given path (going back n levels) to stdout, possibly colouring it. */
void tosh_show_path(char *path, int n, int rainbow) {
	char *comp = strtok(path, "/");
	char *components[strlen(path)]; // (this is obviously usually too long, but whatever)
	int i, j = 0;

	// Split path into components
	for (i = 0; comp != NULL; i++) {
		components[i] = comp;
		comp = strtok(NULL, "/");
	}
	components[i] = NULL;

	// Show initial slash if path goes back to root.
	if (i <= n)
		printf("/");

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

// Forward declarations for tosh_expand_expression().
int tosh_locate_expression(char *, int *, int *, int *, int *);
char *tosh_eval_inline(char *);
char *tosh_str_substitute(char *, int, int, char *);

/* Expand the first expression to be substituted found in the string str.
 * Returns a null pointer if no expression to be evaluated was found. 
 * Returned string is dynamically allocated; requires freeing later. */
char *tosh_expand_expression(char *str) {
	int si, ei, rsi, rei;
	char *expr, *result, *newstr;

	DEBUG_LOG("expanding expression in line '%s'...", str);

	// Locate expression.
	if (!tosh_locate_expression(str, &si, &ei, &rsi, &rei)) {
		// Not found.
		DEBUG_LOG("didn't find an expression to be evaluated.", NULL)
		return NULL;
	} 

	// Evaluate expression in a subshell.
	expr = malloc((strlen(&str[si]) + 1) * sizeof(char));
	if (!expr) {
		fprintf(stderr, "tosh: memory allocation failed. :(\n");
		exit(EXIT_FAILURE);
	}
	memcpy(expr, &str[si], strlen(str) - si);
	expr[ei - si] = '\0';
	DEBUG_LOG("evaluating: '%s'...", expr);
	result = tosh_eval_inline(expr);
	free(expr);
	DEBUG_LOG("evaluated to: '%s'", result);
	
	// Substitute back into str.
	newstr = tosh_str_substitute(str, rsi, rei, result);
	free(str);
	free(result);
	DEBUG_LOG("substitution yields: '%s'", newstr);

	return newstr;
}

/* Find the first expression to be evaluated and substituted in the string str.
 * Returns the start and end indices of the expression as si and ei, and the
 * start and end indices of the whole substring to be replaced as rsi and rei.
 * Actual return value is 1 if an expression was found, and 0 if not. */
int tosh_locate_expression(char *str, int *si, int *ei, int *rsi, int *rei) {
	int i;
	char *str2, *substr;

	// Find a dollar sign...
	for (i = 0; str[i] != '$' && str[i] != '\0'; i++)
		;
	// Didn't find one.
	if (str[i] == '\0')
		return 0;
	*si = i + 1;
	*rsi = i;

	if (str[++i] == '(') {
		// If next char is an opening bracket, look for a closing one.
		(*si)++;
		for (; str[i] != ')' && str[i] != '\0'; i++)
			;
		// Didn't find one.
		if (str[i] == '\0')
			return 0;
		*ei = i;
		*rei = i + 1;

	
	} else {
		// Otherwise, take rest of string up to whitespace or end (null byte).
		for (; str[i] != ' ' && str[i] != '\t' && str[i] != '\n' && str[i] != '\0'; i++)
			;
		*ei = i;
		*rei = i;
	}

	return 1;
}

/* Substitute substr for the substring of str delimited by the indices si and ei.
 * (start index and end index respectively: si included; ei not.)
 * Returns a dynamically allocated string; requires freeing later. */
char *tosh_str_substitute(char *str, int si, int ei, char *substr) {
	char *newstr = malloc((strlen(str) - (ei - si + 1) + strlen(substr) + 1) * sizeof(char));
	if (!newstr) {
		fprintf(stderr, "tosh: memory allocation failed. :(\n");
		exit(EXIT_FAILURE);
	}

	DEBUG_LOG("substituting %s in the string %s...\n", substr, str);
	DEBUG_LOG("start index: %d, end index: %d.\n", si, ei);

	// Copy the original string (all of it).
	strcpy(newstr, str);
	DEBUG_LOG("newstr: %s\n", newstr);
	// Insert substring
	strcpy(&newstr[si], substr);
	DEBUG_LOG("newstr: %s\n", newstr);
	// Insert rest of string
	strcpy(&newstr[si + strlen(substr)], &str[ei]);
	DEBUG_LOG("newstr: %s\n", newstr);

	return newstr;
}

// Buffer increment for receiving the data returned by a subshell.
#define RESULT_BUF_INC 2048

/* Spawn a subshell to execute a given command and return the outputted string,
 * ready for substitution (usually).
 * Returns a dynamically allocated string; requires freeing later.
 * For now, we strip the final newline in the result, but don't worry about others. */
char *tosh_eval_inline(char *line) {
	pid_t id;
	int backpipe_fd[2];
	int topipe_fd[2];
	char *buf, c;
	int bufsize = RESULT_BUF_INC, bytes_read;


	// Create pipes to transfer data to and from the subshell.
	// (x[0] is the read end; x[1] the write end.)
	if (pipe(backpipe_fd) == -1) {
		fprintf(stderr, "tosh: I couldn't make the backpipe. :(\n");
	}
	if (pipe(topipe_fd) == -1) {
		fprintf(stderr, "tosh: I couldn't make the topipe. :(\n");
	}

	// Fork shell
	id = fork();
	DEBUG_LOG("%d: forked.", id)

	// Write command line to topipe's input.
	write(topipe_fd[1], line, (strlen(line) + 1) * sizeof(char));
	close(topipe_fd[1]);


	if (id == 0) {
		// [In the child...]
		close(backpipe_fd[0]);

		// Connect stdin to topipe's output; stdout to backpipe's input.
		dup2(topipe_fd[0], fileno(stdin));
		dup2(fileno(stdout), fileno(stderr));
		dup2(backpipe_fd[1], fileno(stdout)); 

		// Execute command line (non-looping).
		TOSH_DEBUG = "OFF";
		TOSH_VERBOSE = "OFF";
		tosh_loop(0);

		// (We usually shouldn't end up here.)
		close(topipe_fd[0]);
		DEBUG_LOG("child: exiting...", NULL)
		exit(EXIT_SUCCESS);

	} else {
		// [In the parent...]
		close(backpipe_fd[1]);
		close(topipe_fd[0]);

		buf = malloc(bufsize * sizeof(char));
		if (!buf) {
			fprintf(stderr, "tosh: memory allocation failed. :(\n");
		}

		// Wait for child.
		DEBUG_LOG("parent: waiting for child with pid %d...", id)
		waitpid(id, NULL, 0);

		// Read from pipe (the data is buffered to a certain extent by kernel until we do so).
		// ...but we wait above, because for longer output the buffer isn't always large enough.
		bytes_read = read(backpipe_fd[0], buf, bufsize * sizeof(char));
		close(backpipe_fd[0]);
		DEBUG_LOG("parent: finished reading %d bytes from child.", bytes_read)

		// Kill subshell.
		kill(id, SIGTERM);

		/* [NOTE: we really ought to implement dynamic buffer size adjustment here!!] */
	
		// Strip trailing newline.
		if (buf[bytes_read - 1] == '\n')
			buf[bytes_read - 1] = '\0';
		else
			buf[bytes_read - 1] = '\0';

		return buf;
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
	if (!path) {
		fprintf(stderr, "tosh: memory allocation failed. :(\n");
		exit(EXIT_FAILURE);
	}
	strcpy(path, TOSH_HIST_PATH);
	expanded_path = tosh_expand_tilde(path);

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
	// This has yet to be implemented. ;-)
	;
}

/* Perform some general initialisation tasks.
 * (Usually) called once at startup. */
void tosh_init(void) {
	char *cwd, *str;

	// Store current directory (as previous directory for later).
	cwd = malloc(TOSH_MAX_PATH * sizeof(char));
	getcwd(cwd, TOSH_MAX_PATH * sizeof(char));
	strcpy(TOSH_LAST_DIR, cwd);
	free(cwd);

	// Increment shell level count.
	str = malloc(128 * sizeof(char));
	sprintf(str, "%d", atoi(ENV_SHLVL) + 1);
	setenv("SHLVL", str , 1);
	free(str);
}

/* ---- BUILTINS BELOW ---- */

int tosh_cd(char **args) {
	int argc;
	char *arg, *cwd, *lastdir;

	for (argc = 0; args[argc] != NULL; argc++)
		;

	// Save current directory for later.
	cwd = malloc(TOSH_MAX_PATH * sizeof(char));
	if (!cwd) {
		fprintf(stderr, "tosh: memory allocation failed. :(\n");
		exit(EXIT_FAILURE);
	}
	getcwd(cwd, TOSH_MAX_PATH * sizeof(char));

	// No arguments to cd; go home.
	if ((arg = args[1]) == NULL) {
		char *homedir = getenv("HOME");
		if (homedir != NULL) {
			args[1] = malloc((strlen(homedir) + 1) * sizeof(char));
			if (!args[1]) {
				fprintf(stderr, "tosh: memory allocation failed. :(\n");
				exit(EXIT_FAILURE);
			}
			strcpy(args[1], homedir);
			return tosh_cd(args);
		} else {
			fprintf(stderr, "tosh: I couldn't find your home directory. :(\n");
		}

	// We have some arguments...
	} else if (argc == 2) {
		if (strcmp(args[1], "-") == 0) {
			// Get last dir
			lastdir = malloc(TOSH_MAX_PATH * sizeof(char));
			if (!lastdir) {
				fprintf(stderr, "tosh: memory allocation failed. :(\n");
				exit(EXIT_FAILURE);
			}
			strcpy(lastdir, TOSH_LAST_DIR);
			strcpy(TOSH_LAST_DIR, cwd);

			// Go to (chronologically) previous directory.
			if (chdir(lastdir) != 0) {
				perror("tosh");
			}
			free(lastdir);

		} else {
			// Store current working directory for later.
			if (strcmp(cwd, TOSH_LAST_DIR) != 0) {
				strcpy(TOSH_LAST_DIR, cwd);
			}
			// Change working directory of process to specified directory.
			if (chdir(args[1]) != 0) {
				perror("tosh");
			}
		}
	} else {
		// More than one argument to cd.
		fprintf(stderr, "tosh: Where do you want to go?\n");
	}

	free(cwd);
	
	// Signal to continue.
	return 1;
}

int tosh_showenv(char **args) {
	printf("Environment variables that tosh cares about ⤵︎\n");
	int i;
	for (i = 0; i < tosh_num_glob(); i++) {
		printf("%s=%s\n", glob_vars_str[i], *glob_vars[i]);
	}
	// Signal to continue.
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

int tosh_readconfig(char **args) {
	tosh_load_config();
	
	// Signal to continue.
	return 1;
}

int tosh_help(char **args) {
	int i;
	printf(BLD "\n---=== TOSH — a very simple shell. ===---\n" BLDRS);
	printf("\nType program names and arguments, and hit enter.\n");
	printf("The following are built in ⤵︎\n");

	// List the builtins, according to the strings stored.
	for (i = 0; i < tosh_num_builtins(); i++) {
		printf("- %s\n", builtin_str[i]);
	}
	printf("\n");

	// Signal to continue.
	return 1;
}

int tosh_quit(char **args) {
	if (strcmp(TOSH_VERBOSE, "ON") == 0) {
		printf("Bye bye! :)\n");
	}
	// Signal to exit.
	return 0;
}
