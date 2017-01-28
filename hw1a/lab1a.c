// lab1a.c
//
// Eric Mueller -- emueller@hmc.edu
//
// Implementation of lab1a for CS134 as described here:
// http://www.cs.pomona.edu/classes/cs134/projects/P1A.html

#include <termios.h>
#include <unistd.h>
#include <stddef.h>
#include <stdio.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <stdbool.h>
#include <pthread.h>

static struct termios old_termios;

static void die(const char *src, int err)
{
        if (err) {
                fprintf(stderr, "%s: %s\n", src, strerror(err));
                exit(1);
        } else {
                exit(0);
        }
}

static void termfix()
{
        tcsetattr(STDIN_FILENO, TCSAFLUSH, &old_termios);
}

// http://www.ascii-code.com/
#define CR_CHAR 0x0d
#define LF_CHAR 0x0a
#define CTRL_D_CHAR 0x04

#define USAGE_STR "usage: lab1a [--shell]"

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
        memcpy(&new_termios, &old_termios, sizeof old_termios);
        new_termios.c_iflag = 0;
        new_termios.c_oflag = 0;
        new_termios.c_lflag = 0;
        new_termios.c_cflag = old_termios.c_cflag;
        
        err = tcsetattr(STDIN_FILENO, TCSAFLUSH, &new_termios);
        if (err)
                die("tcsetattr", errno);
}

static void *thread(void *arg)
{
        int in_fd = *((int*)arg);

        printf("thread\n");
        
        for (;;) {
                char buf[2];
                ssize_t ret = read(in_fd, buf, 1);
                if (ret < 0)
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

int main(int argc, char **argv)
{
        int err;
        pid_t child = -1;
        int child_in_fd = -1;
        int child_out_fd = -1;
        bool have_child = false;
        pthread_t tid = -1;

        terminal_setup();

        // parse args
        if (argc > 2) {
                perror(USAGE_STR);
                die("usage", EINVAL);
        }

        if (argc == 2) {
                have_child = true;
                
                // getopt is overkill here
                err = strcmp(argv[1], "--shell");
                if (err) {
                        perror(USAGE_STR);
                        die("usage", EINVAL);
                }

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

                child = fork();
                if (child < 0)
                        die("fork", errno);

                if (child == 0) {                        
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

                if (buf[0] == CTRL_D_CHAR) {
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
