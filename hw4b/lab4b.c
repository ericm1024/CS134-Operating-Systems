// lab4b.c
//
// NAME: Eric Mueller
// EMAIL: emueller@hmc.edu
// ID: 40160869

#define _POSIX_C_SOURCE 199309L
#define _GNU_SOURCE

#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
#include <limits.h>
#include <math.h>
#include <poll.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include <sys/types.h>
#include <sys/stat.h>

#include <mraa.h>

#define PERIOD_OPT_RET 'p'
#define SCALE_OPT_RET 's'
#define LOG_OPT_RET 'l'
#define USAGE_STR "lab2b [--period=n] [--scale={F,C}] [--log=file]\n"

static const struct option lab4b_opts[] =
{
        { // --period=<number>
                .name = "period",
                .has_arg = required_argument,
                .flag = NULL,
                .val = PERIOD_OPT_RET,
        },
        { // --scale=<number>
                .name = "scale",
                .has_arg = required_argument,
                .flag = NULL,
                .val = SCALE_OPT_RET
        },
        { // --log=<logfile>
                .name = "log",
                .has_arg = required_argument,
                .flag = NULL,
                .val = LOG_OPT_RET
        },
        {0, 0, 0, 0} // end of array
};

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

enum temp_scale {
        FAHRENHEIT,
        CELSIUS
};

static int log_fd = -1;
static int button_fd = -1;
static unsigned period = 1;
static enum temp_scale scale = FAHRENHEIT;
static mraa_gpio_context button_pin;
static mraa_aio_context adc_pin;
static bool reading = true;
static struct timespec read_time;

// log a formatted message to the log file in the given context.
// Acquires and releases ctx->log_mutex. Returns 0 on success and
// err numbers on failure.
//
// NB: the next three functions have __attribute__((format(printf...))) so
// that the compiler checks our format strings for type sanity. However,
// it seems likes attributes can only go on declarations, so all three of
// these are declared right before they're defined.

static int __log_message(bool to_stdout, const char *fmt, va_list args)
{
        int ret = 0;

        char buf[512];
        size_t size = sizeof buf;
        char *bufptr = buf;
        time_t t;
        struct tm t_parts;
        struct tm *tm_tmp;
        size_t bytes;
        ssize_t sbytes;
                
        memset(buf, 0, sizeof buf);
        memset(&t_parts, 0, sizeof t_parts);

        // get the time in seconds
        t = time(NULL);
        if (t == -1)
                return errno;

        // convert it to parts
        tm_tmp = localtime_r(&t, &t_parts);
        if (!tm_tmp)
                return errno;

        // write the time
        bytes = strftime(bufptr, size, "%H:%M:%S ", &t_parts);
        if (bytes == 0)
                return ENOMEM;

        size -= bytes;
        bufptr += bytes;

        // write the user's message
        ret = vsnprintf(bufptr, size, fmt, args);
        if (ret < 0)
                return -ret;

        size -= ret;
        bufptr += ret;

        // write a newline
        ret = snprintf(bufptr, size, "\n");
        if (ret < 0)
                return -ret;

        size -= ret;
        bufptr += ret;

        // finally write the whole buffer to the logfile and stdout
        if (log_fd != -1) {
                sbytes = write(log_fd, buf, bufptr - buf);
                if (sbytes < 0)
                        ret = errno;
        }

        if (to_stdout) {
                sbytes = write(STDOUT_FILENO, buf, bufptr - buf);
                if (sbytes < 0)
                        ret = errno;
        }
        return ret;
}

static int log_message(const char *fmt, ...)
        __attribute__ ((format (printf, 1, 2)));

static int log_message(const char *fmt, ...)
{
        va_list args;
        va_start(args, fmt);
        int ret = __log_message(false, fmt, args);
        va_end(args);
        return ret;
}

static int log_message_with_stdout(const char *fmt, ...)
        __attribute__ ((format (printf, 1, 2)));

static int log_message_with_stdout(const char *fmt, ...)
{
        va_list args;
        va_start(args, fmt);
        int ret = __log_message(true, fmt, args);
        va_end(args);
        return ret;
}

static float read_temp()
{
	const int B = 4275;
	float a = mraa_aio_read_float(adc_pin);

	float R = 1.0/a-1.0;
    	R = 100000.0*R;

    	float temperature=1.0/(log(R/100000.0)/B+1/298.15)-273.15;

        // we read in C, so convert to F if necessary
        if (scale == FAHRENHEIT)
                temperature = 32.0 + temperature*9.0/5.0;

        return temperature;
}

// parse a command and execute it
static void do_command(const char *cmd)
{
        int err = 0;

        log_message("%s", cmd);

        if (strcmp(cmd, "OFF") == 0) {
                log_message_with_stdout("SHUTDOWN");
                exit(0);
        } else if (strcmp(cmd, "STOP") == 0) {
                reading = false;
        } else if (strcmp(cmd, "START") == 0) {
		if (!reading) {
                	reading = true;
			err = clock_gettime(CLOCK_MONOTONIC, &read_time);
			if (err)
				die("clock_gettime", errno);
		}
        } else if (strncmp(cmd, "SCALE=", strlen("SCALE=")) == 0) {
                char *eq_ptr;

                eq_ptr = strchr(cmd, '=');
                ++eq_ptr;

                if (strcmp(eq_ptr, "F") == 0) {
                        scale = FAHRENHEIT;
                } else if (strcmp(eq_ptr, "C") == 0) {
                        scale = CELSIUS;
                } else {
                        err = EINVAL;
                }
        } else if (strncmp(cmd, "PERIOD=", strlen("PERIOD=")) == 0) {
                char *eq_ptr;
                char *end;
                long val;

                eq_ptr = strchr(cmd, '=');
                ++eq_ptr;

                errno = 0;
                val = strtol(eq_ptr, &end, 10);
                if (errno) {
                        err = errno;
                } else if (val < 1 || val > 1000 || *end != 0) {
                        err = EINVAL;
                } else {
                        // period is stored as an unsigned int, hence
                        // the check above, so we know this cast is safe
                        period = val;
                }
        }

        // if the user puts in an invalid command, we just want to yell at
        // them and make them try again, we don't want to return an error
        if (err)
                fprintf(stderr, "Invalid command: %s\n", cmd);
}

// not my code. Stolen from https://gist.github.com/diabloneo/9619917
static void timespec_diff(struct timespec *start, struct timespec *stop,
                          struct timespec *result)
{
        if ((stop->tv_nsec - start->tv_nsec) < 0) {
                result->tv_sec = stop->tv_sec - start->tv_sec - 1;
                result->tv_nsec = stop->tv_nsec - start->tv_nsec + 1000000000;
        } else {
                result->tv_sec = stop->tv_sec - start->tv_sec;
                result->tv_nsec = stop->tv_nsec - start->tv_nsec;
        }
}

int main(int argc, char **argv)
{
        int ret, err;
        mraa_result_t mrr;
        char buf[512];

        memset(buf, 0, sizeof buf);
        
        while (-1 != (ret = getopt_long(argc, argv, "", lab4b_opts, NULL))) {
                switch (ret) {
                case PERIOD_OPT_RET:
                        errno = 0;
                        long val = strtol(optarg, NULL, 10);
                        if (errno) {
                                fprintf(stderr, USAGE_STR);
                                die("main: failed to parse period", errno);
                        } else if (val < 0 || val > 1000) {
                                fprintf(stderr, USAGE_STR);
                                die("main: bad period", EINVAL);
                        }
                        period = val;
                        break;

                case SCALE_OPT_RET:
                        if (strcmp(optarg, "C") == 0) {
                                scale = CELSIUS;
                        } else if (strcmp(optarg, "F") == 0) {
                                scale = FAHRENHEIT;
                        } else {
                                fprintf(stderr, USAGE_STR);
                                die("main: bad scale", EINVAL);
                        }
                        break;

                case LOG_OPT_RET:
                        log_fd = open(optarg, O_WRONLY|O_APPEND|O_CREAT,0600);
                        if (log_fd < 0)
                                die("open", errno);
                        break;

                default:
                        exit(1);
                }

        }

        // setup button
        button_pin = mraa_gpio_init(3);
        if (!button_pin)
                die("mraa_gpio_init", EIO);

        mrr = mraa_gpio_dir(button_pin, MRAA_GPIO_IN);
        if (mrr != MRAA_SUCCESS)
                die("mraa_gpio_dir", EIO);

	mrr = mraa_gpio_edge_mode(button_pin, MRAA_GPIO_EDGE_RISING);
	if (mrr != MRAA_SUCCESS)
		die("mraa_gpio_edge_mode", EIO);

        // open the gpio pin file directly so we can poll(2) it
        snprintf(buf, sizeof buf, "/sys/class/gpio/gpio%d/value",
                 mraa_gpio_get_pin_raw(button_pin));

        button_fd = open(buf, O_RDONLY);
        if (button_fd < 0)
                die("open(/sys/class/gpio/...) (are you root?)", errno);

        // clear pin
        char c;
        lseek(button_fd, 0, SEEK_SET);
        read(button_fd, &c, 1);

        // setup 
        adc_pin = mraa_aio_init(0);
        if (!adc_pin)
                die("mraa_aio_init", EIO);

        // find the start time
        err = clock_gettime(CLOCK_MONOTONIC, &read_time);
        if (err)
                die("clock_gettime", errno);

        // make stdin non-blocking
        int flags = fcntl(STDIN_FILENO, F_GETFL);
        if (flags == -1)
                die("fcntl(F_GETFL)", errno);

        err = fcntl(STDIN_FILENO, F_SETFL, flags | O_NONBLOCK);
        if (err)
                die("fcntl(F_SETFL)", errno);

        // clear the buffer (to be used for reading from stdin)
        memset(buf, 0, sizeof buf);
        size_t bidx = 0;
        size_t size = sizeof buf;

        for (;;) {
                struct pollfd fds[2] = {
                        {
                                .fd = STDIN_FILENO,
                                .events = POLLIN,
                                .revents = 0
                        },
                        {
                                .fd = button_fd,
                                .events = POLLPRI,
                                .revents = 0
                        }
                };

                // grab current time
                struct timespec now;
                err = clock_gettime(CLOCK_MONOTONIC, &now);
                if (err)
                        die("clock_gettime", errno);
                
                // if we're reading and we're past the end of the period
                if (reading &&
                    ((now.tv_sec > read_time.tv_sec)
                     || (now.tv_sec == read_time.tv_sec
                         && now.tv_nsec >= read_time.tv_nsec))) {

                        // read ad
                        double temp = read_temp();
			printf("%s: %f\n", __func__, temp);
                        log_message_with_stdout("%s: %f", __func__, temp);

                        // incrememt period
                        read_time.tv_sec += period;
                }

                // calculate timeout
                struct timespec tmo;
                timespec_diff(&now, &read_time, &tmo);
                
                // sleep until IO
                ret = ppoll(fds, 2, reading ? &tmo : NULL, NULL);
                if (ret < 0)
                        die("ppoll", errno);

                // process button
                if (fds[1].revents & POLLPRI) {
                        log_message_with_stdout("SHUTDOWN");
                        exit(0);
                }

                // process command
                if (fds[0].revents & POLLIN) {
                        // hmm, someone typed a very long line.
                        if (size <= 1)
                                die("stdin buffer", ENOMEM);

                        // read as much as we can into the buffer
                        ssize_t ret = read(STDIN_FILENO, buf + bidx,
                                           size - 1);
                        if (ret < 0) {
                                // we polled, so we better not get EAGAIN.
                                // (this is a programmer error on my part
                                // if this trips)
                                assert(ret != EAGAIN);

                                die("read(STDIN_FILENO)", errno);
                        }

                        // adjust our first-free index and buffer size
                        bidx += ret;
                        size -= ret;
                again:
                        // look for complete lines and process them
                        for (size_t i = 0; i < bidx; ++i) {
                                if (buf[i] == '\n') {
                                        buf[i] = 0;
                                        do_command(buf);

                                        // did we process the whole buffer?
                                        if (i == bidx - 1) {
                                                memset(buf, 0, sizeof buf);
                                                bidx = 0;
                                                size = sizeof buf;
                                        }
                                        // otherwise there is another command
                                        // (or partial command) in the buffer
                                        // so just move everything to the
                                        // beginning of the buffer and go
                                        // again
                                        else {
                                                bidx -= i + 1;
                                                size += i + 1;
                                                memmove(buf, buf + i + 1,
                                                        bidx);
                                                goto again;
                                        }
                                }
                                // if we never hit a newline, we just
                                // go back to sleep and wait for more data
                                // to be ready on stdin
                        }
                }
        }
}
