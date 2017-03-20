// lab2b.c
//
// NAME: Eric Mueller
// EMAIL: emueller@hmc.edu
// ID: 40160869

#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
#include <limits.h>
#include <math.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include <readline/readline.h>

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

enum temp_scale {
        FAHRENHEIT,
        CELSIUS
};

// the state of the thread that polls the ADC and writes to the log file
enum thread_state {
        NOT_SPAWNED,
        
        // the thread exists and is running, i.e. polling the ADC and and
        // writing to the log file
        RUNNING,

        // the thread exists, but we have told it to temporarily stop polling
        // the ADC and just sleep
        STOPPED,

        // the thread is dead. As soon as the thread sees this, it should exit
        DEAD,
};

struct lab4b_ctx {
        // the thread
        pthread_t thread;
        
        // protect the log file from concurrent appends
        pthread_mutex_t log_mutex;

        // log file, possibly -1 if we're not logging
        int log_fd;

        // protext everything else
        pthread_mutex_t lock;

        // state of the thread. Controlled by the master thread
        enum thread_state thread_state;
        
        // condition variable to tell the thread that thread_state has changed
        pthread_cond_t cond;
        
        // sampling period in seconds
        unsigned period;

        // F or C
        enum temp_scale scale;

        // gpio pin for button
        mraa_gpio_context gpio_pin;

        // adc pin for temperature sensor
        mraa_aio_context adc_pin;
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

static void button_isr(void *);

// Create and initialize a context structure for this lab given the provided
// args.
//
// @log: the name of the log file to open. NULL means no log file, and
//       ctx->log_fd is set to -1.
// @period: the period in seconds to poll. Must be > 0
// @scale: the temperature scale. FAHRENHEIT or CELSIUS.
// @out: Somewhere to put the context. Must be freed by caller.
//
// Return 0 on success or an error number on failure. The allocated context is
// put into `out`. If allocation fails, NULL is put into `out`.
//
// We implement this function using the stack-unwinding resource acquisition
// cleanup patern to cleanly release all resources if we encounter an error.
static struct lab4b_ctx *
create_lab4b_ctx(const char *log, const unsigned period,
                 const enum temp_scale scale)
{
        int err = ENOMEM;
	mraa_result_t mrr;	

        if (!period)
                die("create_lab4b_ctx", EINVAL);
        
        struct lab4b_ctx *ctx = calloc(1, sizeof *ctx);
        if (!ctx)
                die("calloc", ENOMEM);

        ctx->thread = 0;

        err = pthread_mutex_init(&ctx->log_mutex, NULL);
        if (err)
                die("pthread_mutex_init", err);

        // if we don't have a log file that's okay
        if (log) {
                ctx->log_fd = open(log, O_WRONLY|O_APPEND|O_CREAT, 0600);
                if (ctx->log_fd < 0)
                        die("open", errno);

        } else {
                ctx->log_fd = -1;
        }
                
        err = pthread_mutex_init(&ctx->lock, NULL);
        if (err)
                die("pthread_mutex_init", err);

        ctx->thread_state = NOT_SPAWNED;

        ctx->period = period;
        ctx->scale = scale;

        ctx->gpio_pin = mraa_gpio_init(3);
        if (ctx->gpio_pin == NULL)
                die("mraa_gpio_init", EIO);

        mrr = mraa_gpio_dir(ctx->gpio_pin, MRAA_GPIO_IN);
        if (mrr != MRAA_SUCCESS)
                die("mraa_gpio_dir", EIO);
        
        mraa_gpio_isr(ctx->gpio_pin, MRAA_GPIO_EDGE_RISING, button_isr, ctx);

        ctx->adc_pin = mraa_aio_init(0);

        return ctx;
}

// log a formatted message to the log file in the given context.
// Acquires and releases ctx->log_mutex. Returns 0 on success and
// err numbers on failure.
//
// NB: the next three functions have __attribute__((format(printf...))) so
// that the compiler checks our format strings for type sanity. However,
// it seems likes attributes can only go on declarations, so all three of
// these are declared right before they're defined.
static int __log_message(struct lab4b_ctx *ctx, bool to_stdout,
                         const char *fmt, ...)
        __attribute__ ((format (printf, 3, 4)));

static int __log_message(struct lab4b_ctx *ctx, bool to_stdout,
                         const char *fmt, ...)
{
        va_list args;        
        int ret = 0;

        va_start(args, fmt);

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
        pthread_mutex_lock(&ctx->log_mutex);
        if (ctx->log_fd != -1) {
                sbytes = write(ctx->log_fd, buf, bufptr - buf);
                if (sbytes < 0)
                        ret = errno;
        }

        if (to_stdout) {
                sbytes = write(STDOUT_FILENO, buf, bufptr - buf);
                if (sbytes < 0)
                        ret = errno;
        }
        pthread_mutex_unlock(&ctx->log_mutex);

        va_end(args);
        return ret;
}

static int log_message(struct lab4b_ctx *ctx, const char *fmt, ...)
        __attribute__ ((format (printf, 2, 3)));

static int log_message(struct lab4b_ctx *ctx, const char *fmt, ...)
{
        va_list args;
        va_start(args, fmt);
        int ret = __log_message(ctx, false, fmt, args);
        va_end(args);
        return ret;
}

static int log_message_with_stdout(struct lab4b_ctx *ctx,
                                   const char *fmt, ...)
        __attribute__ ((format (printf, 2, 3)));

static int log_message_with_stdout(struct lab4b_ctx *ctx,
                                   const char *fmt, ...)
{
        va_list args;
        va_start(args, fmt);
        int ret = __log_message(ctx, true, fmt, args);
        va_end(args);
        return ret;
}

// called with ctx->lock held
static float read_temp(struct lab4b_ctx *ctx)
{
        const int B = 4275;
        const int R0 = 100000;
        enum temp_scale scale = ctx->scale;

        float R = 1023.0/mraa_aio_read_float(ctx->adc_pin) - 1.0;
        R = 100000.0*R;
        float temperature = 1.0/(log(R/100000.0)/B+1/298.15)-273.15;

        // we read in C, so convert to F if necessary
        if (scale == FAHRENHEIT)
                temperature = 32.0 + temperature*9.0/5.0;

        return temperature;
}

static void *thread(void *arg)
{
        struct lab4b_ctx *ctx = arg;
        struct timespec ts;
        int err;

        pthread_mutex_lock(&ctx->lock);
        for (;;) {
                if (ctx->thread_state == RUNNING) {
                        clock_gettime(CLOCK_REALTIME, &ts);
                        ts.tv_sec += ctx->period;

                        log_message_with_stdout(ctx, "%.2f", read_temp(ctx));

                        // keep waiting on the condition until we timeout or
                        // the state changes
                        for (;;) {
                                err = pthread_cond_timedwait(&ctx->cond,
                                                             &ctx->lock, &ts);
                                if (err == ETIMEDOUT
                                    || ctx->thread_state != RUNNING)
                                        break;
                        }
                } else if (ctx->thread_state == STOPPED) {
                        // keep waiting until we're not stopped
                        for (;;) {
                                pthread_cond_wait(&ctx->cond, &ctx->lock);
                                if (ctx->thread_state != STOPPED)
                                        break;
                        }
                } else if (ctx->thread_state == DEAD) {
                        pthread_mutex_unlock(&ctx->lock);
                        return NULL;
                } else {
                        // we should never see thread_state == NOT_SPAWNED in
                        // this thread
                        assert(false);
                }
        }
}

/*
 * The follwing three functions carefully update the state of the thread.
 * These function has to exist and be nasty to avoid a few very rare but
 * gross races.
 *
 * 1. We need to make sure that the thread does not log a data point after
 *    we log our shutdown message.
 *
 * 2. We need to make sure that if the command-processing thread and the
 *    button interrupt handler run at the same time, they do not deadlock
 *    or print the exit message more than once.
 *
 * 3. We need to make sure that if the button interrupt handler is called
 *    before the thread is spawned, the thread does not get spawned.
 *
 * Our state diagram is thus: 
 *                        ________________ 
 *                       /       \        \ 
 *                      /         v        v
 * NOT_SPAWNED --> RUNNING    STOPPED --> DEAD
 *      \               ^         /        ^
 *       \               \_______/        /
 *        \______________________________/
 *
 * Note: a transition directly from NOT_SPAWNED to dead means that
 * the shutdown button was pressed before we were able to spawn
 * the thread. Unlikely, but possible.
 */
static enum thread_state start_thread(struct lab4b_ctx *ctx)
{
        pthread_mutex_lock(&ctx->lock);
        enum thread_state current = ctx->thread_state;
        int err;

        if (current == DEAD)
                goto out_unlock;

        if (current == NOT_SPAWNED) {
                err = pthread_create(&ctx->thread, NULL, thread, ctx);
                if (err)
                        die("pthread_create", err);

        } else if (current == STOPPED) {
                err = pthread_cond_signal(&ctx->cond);
                if (err)
                        die("pthread_cond_signal", err);
        }

        // mark that the thread is running
        ctx->thread_state = RUNNING;

out_unlock:
        pthread_mutex_unlock(&ctx->lock);
        return current;
}

static enum thread_state stop_thread(struct lab4b_ctx *ctx)
{
        pthread_mutex_lock(&ctx->lock);
        enum thread_state current = ctx->thread_state;
        int err;

        if (current == DEAD) 
                goto out_unlock;

        if (current == RUNNING) {
                err = pthread_cond_signal(&ctx->cond);
                if (err)
                        die("pthread_cond_signal", err);
        }

        // mark that the thread is stopped
        ctx->thread_state = STOPPED;
        
out_unlock:
        pthread_mutex_unlock(&ctx->lock);
        return current;
}

static enum thread_state kill_thread(struct lab4b_ctx *ctx)
{
        pthread_mutex_lock(&ctx->lock);
        enum thread_state current = ctx->thread_state;
        int err;

        if (current == DEAD) {
                pthread_mutex_unlock(&ctx->lock);
                return current;
        }
        
        // we have to do a bit of a dance here:
        // mark the thread as dead, signal it, drop the lock,
        // and wait for it to exit
        ctx->thread_state = DEAD;
        err = pthread_cond_signal(&ctx->cond);
        if (err)
                die("pthread_cond_signal", err);
        
        pthread_mutex_unlock(&ctx->lock);
        
        err = pthread_join(ctx->thread, NULL);
        if (err)
                die("pthread_join", err);

        return current;
}

// This function gets called when the button is pressed. The documentation
// calls this function an "interrup service routine" (isr), but since we're
// in userspace we obviously can't take a real hardirq, so one might assume
// it's a signal handler, which would make synchronization difficult, but
// it turns out it's just a function that runs in a thread. This is tragically
// not documented, but the implementation is here.
// 
// https://github.com/intel-iot-devkit/mraa/blob/master/src/gpio/gpio.c#L378
// 
static void button_isr(void *arg)
{
        struct lab4b_ctx *ctx = arg;
        enum thread_state prev_state;

        prev_state = kill_thread(ctx);

        // if we won the race to kill the thread, print the shutdown message
        // and exit
        if (prev_state != DEAD) {
                log_message(ctx, "SHUTDOWN");
                exit(0);
        }
}

// parse a command and execute it
static void do_command(const char *cmd, struct lab4b_ctx *ctx)
{
        int err = 0;

        log_message(ctx, "%s", cmd);

        if (strcmp(cmd, "OFF") == 0) {
                // log a SHUTDOWN message and exit.
                enum thread_state prev_state = kill_thread(ctx);

                // if we won the race to kill the thread, print the shutdown
                // msg and exit
                if (prev_state != DEAD) {
                        log_message(ctx, "SHUTDOWN");
                        exit(0);
                } else {
                        // otherwise we don't want to continue processing
                        // commands, so just chill until the ISR exits the
                        // program
                        for (;;)
                                sched_yield();
                }

        } else if (strcmp(cmd, "STOP") == 0) {
                stop_thread(ctx);
        } else if (strcmp(cmd, "START") == 0) {
                start_thread(ctx);
        } else if (strncmp(cmd, "SCALE=", strlen("SCALE=")) == 0) {
                char *eq_ptr;
                enum temp_scale scale;

                eq_ptr = strchr(cmd, '=');
                ++eq_ptr;

                if (strcmp(eq_ptr, "F") == 0) {
                        scale = FAHRENHEIT;
                } else if (strcmp(eq_ptr, "C") == 0) {
                        scale = CELSIUS;
                } else {
                        err = EINVAL;
                }

                if (!err) {
                        pthread_mutex_lock(&ctx->lock);
                        ctx->scale = scale;
                        pthread_mutex_unlock(&ctx->lock);
                }
                
        } else if (strncmp(cmd, "PERIOD=", strlen("PERIOD=")) == 0) {
                char *eq_ptr;
                char *end;
                unsigned period;
                long val;

                eq_ptr = strchr(cmd, '=');
                ++eq_ptr;

                errno = 0;
                val = strtol(eq_ptr, &end, 10);
                if (errno) {
                        err = errno;
                } else if (val < 1 || val > (long)UINT_MAX || *end != 0) {
                        err = EINVAL;
                } else {
                        // period is stored as an unsigned int, hence
                        // the check above, so we know this cast is safe
                        period = val;

                        pthread_mutex_lock(&ctx->lock);
                        ctx->period = period;
                        pthread_mutex_unlock(&ctx->lock);
                }
        }

        // if the user puts in an invalid command, we just want to yell at
        // them and make them try again, we don't want to return an error
        if (err)
                fprintf(stderr, "Invalid command: %s\n", cmd);
}

int main(int argc, char **argv)
{
        int ret;
        unsigned period = 1;
        enum temp_scale scale = FAHRENHEIT;
        const char *logname = NULL;
        
        while (-1 != (ret = getopt_long(argc, argv, "", lab4b_opts, NULL))) {
                switch (ret) {
                case PERIOD_OPT_RET:
                        errno = 0;
                        long val = strtol(optarg, NULL, 10);
                        if (errno) {
                                fprintf(stderr, USAGE_STR);
                                die("main: bad period", errno);
                        } else if (val < 0 || val > (long)UINT_MAX) {
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
                        logname = optarg;
                        break;

                default:
                        exit(1);
                }

        }

        struct lab4b_ctx *ctx = create_lab4b_ctx(logname, period, scale);

        start_thread(ctx);

        for (;;) {
                char *line = readline(NULL);
                do_command(line, ctx);
                free(line);
        }
}
