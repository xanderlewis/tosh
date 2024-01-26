# tosh &mdash; a very simple shell

tosh / (tɒʃ) / noun. *slang, mainly British* nonsense; rubbish.

A very simple shell written for fun (and learning) in C, initially heavily following Stephen Brennan's nice [blog post](https://brennan.io/2015/01/16/write-a-shell-in-c/).

## Usage
Compile for your chosen architecture, and invoke however you like: from your preferred shell as `./tosh`, or run it on its own (like a *proper* shell!) with `exec ./tosh` (which is a builtin wrapper for the `exec()` system call that most shells seem to have).

Pass data in via standard input like `./tosh < file` or pass a file (several lines of commmands for tosh) as an argument like `./tosh file`.

### Options:
- `-v` (start in verbose mode)
- `-d` (start in debug mode)
- `-i` (force it to behave as if it were interactive)

Can be combined: `-dv`, for example. These options are also adjustable via environment variables; run `env` from within tosh to have a look.

Type `help` to see some more info.

## Features
- traverse the filesystem with `cd`
- run commands and pass them arguments with the usual syntax (including quotes)
- custom prompt string (with formatting, and optional rainbow colours!)
- filename globbing (`*` and `?` metacharacters)
- inline recursive command substitution (execution in a subshell)
- control behaviour with tosh-specific environment variables
- history file in a chosen location
- read commands from a file (i.e. execute shell scripts)

## Coming soon
- pipes
- I/O redirection (to and from files)
- config files
- environment variable substitution and editing

I wanted to include other nice features like *tab completion, up arrow to browse history, live completion suggestion of paths/filenames, etc.*, but since this requires unbuffered access to the input stream it turns out not only to be highly platform-dependent but also particularly difficult on macOS. 
...but it must be possible with some more effort.
