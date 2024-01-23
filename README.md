# tosh &mdash; a very simple shell

tosh / (tɒʃ) / noun. *slang, mainly British* nonsense; rubbish.

A very simple shell written for fun (and learning) in C, initially heavily following Stephen Brennan's nice [blog post](https://brennan.io/2015/01/16/write-a-shell-in-c/).

## Usage
Compile for your chosen architecture, and invoke however you like: from your preferred shell as `./tosh`, or run it on its own (like a *proper* shell!) with `exec ./tosh` (which is a builtin wrapper for the `exec()` system call that most shells seem to have).

Pass data in via standard input like `./tosh < file` or pass a file (several lines of commmands for tosh) as an argument like `./tosh file`.
Options: `-v` (start in verbose mode) `-d` (start in debug mode). Can be combined as `-dv`, for example. These options are also adjustable via environment variables; run `env` from within tosh to have a look.

Type `help` to see some more info.
