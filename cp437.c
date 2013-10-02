/* cp437 - Emulate a CP437 (IBMPC) character set terminal.  Works best on a UTF-8 terminal.
 * 
 * Usage: cp437 <command> [args...]
 *
 * Kevin Easton, June 2011.
 * Released under the 3-clause BSD license - see COPYRIGHT file for details.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <assert.h>
#include <locale.h>
#include <unistd.h>
#include <signal.h>
#if defined(__FreeBSD__)
 #include <libutil.h>
#elif defined (__OpenBSD__) || defined (__NetBSD__) || defined (__APPLE__)
 #include <util.h>
#else
 #include <pty.h>
#endif
#include <sys/select.h>
#include <sys/wait.h>
#include <termios.h>
#include <sys/ioctl.h>
#include <iconv.h>

struct conv {
	iconv_t cd;
	size_t bytesleft;
	char buf[4096];
};

/* copy_converted()
 *
 * Copies bytes from one file descriptor to another, passing them through iconv() on the way.
 * 'conv' stores the conversion state, including any unconverted partial multibyte sequences.
 */
ssize_t copy_converted(int to_fd, int from_fd, struct conv *conv)
{
	ssize_t nbytes;
	char *inptr = conv->buf;
	int retry;

	nbytes = read(from_fd, conv->buf + conv->bytesleft, sizeof conv->buf - conv->bytesleft);
	if (nbytes < 1)
		return nbytes;
	conv->bytesleft += nbytes;

	do {
		char buf[4096];
		char *outptr = buf;
		size_t outbytesleft = sizeof buf;

		retry = 0;
		if (iconv(conv->cd, &inptr, &conv->bytesleft, &outptr, &outbytesleft) == (size_t)-1) {
			if (errno == EILSEQ) {
				assert(conv->bytesleft > 0);
				inptr++;
				conv->bytesleft--;
				retry = 1;
			} else if (errno == E2BIG) {
				retry = 1;
			}
		}
	
		if (outptr > buf)	
			write(to_fd, buf, outptr - buf);
	} while (retry);

	if (conv->bytesleft > 0)
		memmove(conv->buf, inptr, conv->bytesleft);

	return nbytes;
}

/* A pipe and a signal handler to allow a signal to be handled by the main select() loop. */
int sigwinch_pipe[2];

void sigwinch(int sig)
{
	write(sigwinch_pipe[1], "W", 1);
}

int main(int argc, char *argv[])
{
	int master;
	int status;
	pid_t childpid;
	struct termios term;
	struct termios term_orig;
	struct winsize win;
	struct conv to_child, from_child;
	struct sigaction sa;

	/* Set up the LC_CTYPE locale - the character set of the real terminal */
	setlocale(LC_CTYPE, "");

	if (argc < 2) {
		fprintf(stderr, "Usage: %s <command> [args...]\n", argv[0]);
		return 2;
	}

	/* Obtain initial terminal parameters */
	if (tcgetattr(STDIN_FILENO, &term) != 0) {
		perror("tcgetattr");
		return 1;
	}
	if (ioctl(STDIN_FILENO, TIOCGWINSZ, &win) < 0) {
		perror("tty_ioctl(TIOCGWINSZ)");
		return 1;
	}

	/* Set up iconv conversion descriptors.  We use "" for the real terminal, to request the
	 * character set defined by the locale.  This isn't guaranteed by POSIX, but then _nothing_
	 * about iconv is guaranteed by POSIX.
	 */
	if ((to_child.cd = iconv_open("CP437//TRANSLIT", "")) == (iconv_t)-1) {
		perror("iconv_open(CP437//TRANSLIT, \"\")");
		return 1;
	}
	if ((from_child.cd = iconv_open("//TRANSLIT", "CP437")) == (iconv_t)-1) {
		perror("iconv_open(//TRANSLIT, \"CP437\")");
		return 1;
	}
	to_child.bytesleft = 0;
	from_child.bytesleft = 0;

	/* Create and execute the child process */
	childpid = forkpty(&master, NULL, &term, &win);
	
	switch(childpid)
	{
		case -1:
			perror("forkpty");
			return 1;

		case 0:
			/* Munge the locale for the child - probably not the best way to do this. */
			putenv("LANG=C");
			execvp(argv[1], argv + 1);
			perror("exec");
			return 1;

		default:
			break;
	}

	/* Set the real tty into raw mode */
	term_orig = term;
	cfmakeraw(&term);
	tcsetattr(STDIN_FILENO, TCSANOW, &term);

	/* Establish SIGWINCH handler and pipe */
	pipe(sigwinch_pipe);

	sa.sa_handler = &sigwinch;
	sigemptyset(&sa.sa_mask);
	sa.sa_flags = SA_RESTART;
	sigaction(SIGWINCH, &sa, NULL);

	/* Main loop - we stop on any error or EOF */
	while (1) {
		ssize_t nbytes;
		fd_set readfds;
	
		FD_ZERO(&readfds);
		FD_SET(STDIN_FILENO, &readfds);
		FD_SET(master, &readfds);
		FD_SET(sigwinch_pipe[0], &readfds);

		if (select(sigwinch_pipe[0] + 1, &readfds, NULL, NULL, NULL) < 0) {
			if (errno == EINTR)
				continue;
			else
				break;
		}

		if (FD_ISSET(STDIN_FILENO, &readfds)) {
			nbytes = copy_converted(master, STDIN_FILENO, &to_child);
			if (nbytes < 1)
				break;
		}

		if (FD_ISSET(master, &readfds)) {
			nbytes = copy_converted(STDOUT_FILENO, master, &from_child);
			if (nbytes < 1)
				break;
		}

		if (FD_ISSET(sigwinch_pipe[0], &readfds)) {
			char x;
			
			read(sigwinch_pipe[0], &x, 1);
			ioctl(STDIN_FILENO, TIOCGWINSZ, &win);
			ioctl(master, TIOCSWINSZ, &win);
		}
	}
	
	/* Close the pty and wait for the child to exit */
	close(master);
	while (waitpid(childpid, &status, 0) < 0 && errno == EINTR)
		;

	/* Try to restore the original tty settings */
	tcsetattr(STDIN_FILENO, TCSANOW, &term_orig);
	tcsetattr(STDOUT_FILENO, TCSANOW, &term_orig);
	tcsetattr(STDERR_FILENO, TCSANOW, &term_orig);

	return WIFEXITED(status) ? WEXITSTATUS(status) : 127;
}

