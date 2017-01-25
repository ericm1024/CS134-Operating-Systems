// lab0.c
//
// Eric Mueller -- emueller@hmc.edu
//
// Implementation of "Project 0: Warm-Up" for CS134 Operating Systems as described here:
// http://www.cs.pomona.edu/classes/cs134/projects/P0.html

#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
#include <linux/limits.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#define INPUT_OPT_RET 'i'
#define OUTPUT_OPT_RET 'o'
#define SEGFAULT_OPT_RET 's'
#define CATCH_OPT_RET 'c'
#define USE_CFR_RET 'u'

// based off of examples in `man getopt`
static const struct option lab0_options[] =
{
        { // --input=<fname>
                .name = "input",
                .has_arg = required_argument,
                .flag = NULL,
                .val = INPUT_OPT_RET
        },
        { // --output=<fname>
                .name = "output",
                .has_arg = required_argument,
                .flag = NULL,
                .val = OUTPUT_OPT_RET
        },
        { // --segfault
                .name = "segfault",
                .has_arg = no_argument,
                .flag = NULL,
                .val = SEGFAULT_OPT_RET
        },
        { // --catch
                .name = "catch",
                .has_arg = no_argument,
                .flag = NULL,
                .val = CATCH_OPT_RET
        },
        {0, 0, 0, 0} // end of array
};

static void segv_handler(int arg)
{
        (void)arg;
        fprintf(stderr, "caught a segfault!\n");
        exit(3);
}

// helper function to segfault so that our stack trace is a little more interesting
static void do_segfault()
{
        // volatile to keep the optimizer out of our hair
        volatile char *ptr = NULL;
        *ptr = 'a';        
}

int main(int argc, char **argv)
{
        const char *input_fname = "stdin";
        const char *output_fname = "stdout";
        int input_fd = STDIN_FILENO;
        int output_fd = STDOUT_FILENO;
        bool should_segfault = false;
        int ret;

        // `man getopt`
        while (-1 != (ret = getopt_long(argc, argv, "", lab0_options, NULL))) {
                switch (ret) {
                // --input
                case INPUT_OPT_RET:
                        input_fname = optarg;
                        input_fd = open(input_fname, O_RDONLY);
                        if (input_fd < 0) {
                                fprintf(stderr, "could not open input file %s: %s\n",
                                        input_fname, strerror(errno));
                                exit(1);
                        }
                        break;

                // --output
                case OUTPUT_OPT_RET:
                        output_fname = optarg;
                        output_fd = open(output_fname, O_WRONLY|O_CREAT|O_TRUNC, S_IRUSR|S_IWUSR);
                        if (output_fd < 0) {
                                fprintf(stderr, "could not create output file %s: %s\n",
                                        output_fname, strerror(errno));
                                exit(2);
                        }
                        break;

                // --catch
                case CATCH_OPT_RET:
                        if (signal(SIGSEGV, segv_handler) == SIG_ERR) {
                                fprintf(stderr, "failed to set sighandler: %s\n", strerror(errno));
                                exit(4);
                        }
                        break;
                        
                // --segfault
                case SEGFAULT_OPT_RET:
                        should_segfault = true;
                        break;

                default:
                        // getopt_long will print an error message for us
                        exit(4);
                }
        }

        char buf[4096];
        
        for (;;) {
                ssize_t ret = read(input_fd, buf, sizeof buf);

                // EOF
                if (ret == 0) {
                        break;
                } if (ret < 0) {
                        fprintf(stderr, "read: %s", strerror(errno));
                        exit(4);
                }

                ssize_t rbytes = ret;
                ret = write(output_fd, buf, rbytes);
                if (ret != rbytes) {
                        if (ret < 0)
                                fprintf(stderr, "write: %s", strerror(errno));
                        else
                                fprintf(stderr, "short write");
                        exit(4);
                }
        }

        // close can fail, but there's not much we can do about it
        close(output_fd);
        close(input_fd);

        if (should_segfault)
                do_segfault();

        exit(0);
}
