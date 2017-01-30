// lab1a.c
//
// Eric Mueller -- emueller@hmc.edu
//
// Implementation of lab1a for CS134 as described here:
// http://www.cs.pomona.edu/classes/cs134/projects/P1A.html

// for kill(2)
#define _POSIX_SOURCE
#define _DEFAULT_SOURCE

#include <assert.h>
#include <errno.h>
#include <pthread.h>
#include <signal.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <termios.h>
#include <unistd.h>

#include <sys/types.h>
#include <sys/wait.h>

// http://www.ascii-code.com/
#define CR_CHAR 0x0d
#define LF_CHAR 0x0a
#define CTRL_C_CHAR 0x03
#define CTRL_D_CHAR 0x04

#define USAGE_STR "usage: lab1a [--shell]\n"

static struct termios old_termios;
static pid_t child_pid = -1;

// exit, possibly with a message
static void die(const char *reason, int err)
{
        if (reason) {
                fprintf(stderr, "%s: %s\n", reason, strerror(err));
                exit(1);
        } else {
                exit(0);
        }
}

// called at exit: clean up the mess we made of the terminal
static void termfix()
{
        tcsetattr(STDIN_FILENO, TCSAFLUSH, &old_termios);
}

// called at exit: print the child's exit status
static void reap_child()
{
        int status;
        printf("about to wait\r\n");
        pid_t pid = wait(&status);
        if (pid == -1)
                die("wait", errno);
        printf("SHELL EXIT SIGNAL=%d STATUS=%d\r\n",
               WIFSIGNALED(status) ? WTERMSIG(status) : 0,
               WEXITSTATUS(status));
}

// setup the terminal in non-canonical no-echo mode
static void terminal_setup()
{
        struct termios new_termios;
        int err;
        
        // save the old state
        err = tcgetattr(STDIN_FILENO, &old_termios);
        if (err)
                die("tcgetattr", errno);

        // clean up our mess when we exit
        err = atexit(termfix);
        if (err) {
                termfix();
                die("atexit", err);
        }

        // put the console into character-at-a-time, no-echo mode
        // https://www.gnu.org/software/libc/manual/html_node/Noncanon-Example.html
        // Credit to Jeff Milling for the above link
        err = tcgetattr(STDIN_FILENO, &new_termios);
        if (err)
                die("tcgetattr", errno);

        // honestly I have no idea what any of this does
        new_termios.c_oflag = 0;
        new_termios.c_iflag = ISTRIP;
        new_termios.c_lflag = 0;

        err = tcsetattr(STDIN_FILENO, TCSAFLUSH, &new_termios);
        if (err)
                die("tcsetattr", errno);
}

// thread to read from child process's stdout and write to our stdout
static void *thread(void *arg)
{
        int in_fd = *((int*)arg);
        
        for (;;) {
                char buf[2];
                ssize_t ret = read(in_fd, buf, 1);

                // EOF from child
                if (ret == 0)
                        exit(2); 
                else if (ret < 0)
                        die("thread read", errno);

                if (buf[0] == CTRL_D_CHAR) {
                        die(NULL, 0);
                } else if (buf[0] == LF_CHAR) {
                        buf[0] = CR_CHAR;
                        buf[1] = LF_CHAR;
                        ++ret;
                }

                ret = write(STDOUT_FILENO, buf, ret);
                if (ret < 0)
                        die("thread write", errno);
        }
        return NULL;
}

// handler for SIGPIPE
static void pipe_handler(int sig)
{
        (void)sig;
        exit(2);
}

int main(int argc, char **argv)
{
        int err;
        int child_in_fd = -1;
        int child_out_fd = -1;
        bool have_child = false;
        pthread_t tid = -1;

        // parse args
        if (argc > 2) {
                fprintf(stderr, USAGE_STR);
                die("usage", EINVAL);
        } else if (argc == 2) {
                // getopt is overkill here
                if (strcmp(argv[1], "--shell") != 0) {
                        fprintf(stderr, USAGE_STR);
                        die("usage", EINVAL);
                }
                have_child = true;
        }

        terminal_setup();

        if (have_child) {
                // set up sigpipe handler
                if (signal(SIGPIPE, pipe_handler) == SIG_ERR)
                        die("signal", errno);

                err = atexit(reap_child);
                if (err)
                        die("atexit", err);

                // set up two pipes
#define READ_END 0
#define WRITE_END 1

                int child_in[2];
                int child_out[2];

                err = pipe(child_in);
                if (err)
                        die("child_in pipe", errno);

                err = pipe(child_out);
                if (err)
                        die("child_out pipe", errno);

                child_pid = fork();
                if (child_pid < 0)
                        die("fork", errno);

                if (child_pid == 0) {                        
                        // okay we're in the child: we have some pipe dancing to do. First, close
                        // the write end our input pipe, then make our stdin the read end of the
                        // pipe
                        err = close(child_in[WRITE_END]);
                        if (err)
                                die("child: close child_in", errno);

                        err = dup2(child_in[READ_END], STDIN_FILENO);
                        if (err < 0)
                                die("child: dup2 child_in", errno);
                        
                        // next close the read end of our stdout pipe, and make that pipe our
                        // stdout and stderr
                        err = close(child_out[READ_END]);
                        if (err)
                                die("child: close child_out", errno);

                        err = dup2(child_out[WRITE_END], STDOUT_FILENO);
                        if (err < 0)
                                die("child: dup2 child_out -> stdout", errno);

                        err = dup2(child_out[WRITE_END], STDERR_FILENO);
                        if (err < 0)
                                die("child: dup2 child_out -> stderr", errno);

                        // we've set up all of input and output, we're ready to spawn a shell
                        // http://stackoverflow.com/q/4204915/3775803
                        err = execl("/bin/bash", "/bin/bash", NULL);
                        if (err)
                                die("child: execl", errno);
                } else {
                        // okay we're in the parent, do the opposite file descriptor dance
                        err = close(child_in[READ_END]);
                        if (err)
                                die("close child_in", errno);

                        err = close(child_out[WRITE_END]);
                        if (err)
                                die("close child_out", errno);

                        child_in_fd = child_in[WRITE_END];
                        child_out_fd = child_out[READ_END];

                        // start a thread to copy from child out to our out
                        err = pthread_create(&tid, NULL, &thread, &child_out_fd);
                        if (err)
                                die("pthread_create", err);
                }
        }

        for (;;) {
                char buf[2];
                ssize_t ret = read(STDIN_FILENO, buf, 1);
                if (ret < 0)
                        die("read", errno);

                if (buf[0] == CTRL_C_CHAR && have_child) {
                        err = kill(child_pid, SIGINT);
                        if (err)
                                die("kill", errno);
                } else if (buf[0] == CTRL_D_CHAR) {
                        if (have_child)
                                close(child_in_fd);
                        die(NULL, 0);
                } else if (buf[0] == CR_CHAR || buf[0] == LF_CHAR) {
                        buf[0] = CR_CHAR;
                        buf[1] = LF_CHAR;
                        ++ret;
                }

                ret = write(STDOUT_FILENO, buf, ret);
                if (ret < 0)
                        die("write to stdout", errno);

                if (have_child) {
                        if (buf[0] == CR_CHAR)
                                buf[0] = LF_CHAR;

                        ret = write(child_in_fd, buf, 1);
                        if (ret < 0)
                                die("write to child stdin", errno);
                }
        }
}
