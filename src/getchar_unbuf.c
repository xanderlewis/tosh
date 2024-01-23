#include "tosh.h"
#include <termios.h>
#include <stdio.h>
#include <unistd.h>

/* An unbuffered equivalent of <stdio.h>'s getchar(). */
int getchar_unbuf(void) {
	// Make some termios structures.
	struct termios old;
	struct termios new;

	// Get attributes of terminal currently connected to stdin.
	tcgetattr(STDIN_FILENO, &old);
	new = old;
	// Modify flags.
	//new.c_iflag = new.c_iflag & ~(ICANON | ECHO);
	//new.c_iflag = new.c_iflag & ~(ICANON);
	// Write back these changes (immediately).
	tcsetattr(STDIN_FILENO, TCSANOW, &new);
	
	// Get a character (now using this newly-configured terminal).
	int c = getchar();

	// Revert to original attributes and return character.
	tcsetattr(STDIN_FILENO, TCSANOW, &old);
	return c;
}
