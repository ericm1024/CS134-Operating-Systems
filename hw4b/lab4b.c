// lab2b.c
//
// NAME: Eric Mueller
// EMAIL: emueller@hmc.edu
// ID: 40160869

#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
#include <math.h>
#include <pthread.h>
#include <semaphore.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include <readline/readline.h>

#include <sys/types.h>
#include <sys/stat.h>

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
        }
        {0, 0, 0, 0} // end of array
};

enum temp_scale {
        FAHRENHEIT,
        CELSIUS
};

// the state of the thread that polls the ADC and writes to the log file
enum thread_state {
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
static int create_lab4b_ctx(const char *log, const unsigned period,
                            const enum temp_scale scale, struct lab4b_ctx **out)
{
        int err = ENOMEM;
        enum mraa_result_t mrr;

        if (!out || !period)
                return EINVAL;
        
        struct lab4b_ctx *ctx = calloc(1, sizeof *ctx);
        if (!ctx)
                return err;

        // if we don't have a log file that's okay
        if (log) {
                ctx->log_fd = open(log, O_WRONLY|O_APPEND|O_CREAT, 0600);
                if (ctx->log_fd < 0) {
                        err = errno;
                        goto out_free;
                }
                
        } else {
                ctx->log_fd = -1;
        }

        err = pthread_mutex_init(&ctx->data_lock, NULL);
        if (err)
                goto out_close;

        ctx->period = period;
        ctx->scale = scale;

        err = sem_init(&ctx->run_sem, 0, 0);
        if (err) {
                err = errno;
                goto out_destroy_dlock;
        }

        ctx->gpio_pin = mraa_gpio_init(3);
        if (ctx->gpio_pin == NULL) {
                err = EIO; // no good error here
                goto out_sem_destroy;
        }

        mrr = mraa_gpio_dir(ctx->gpio_pin, MRAA_GPIO_IN);
        if (mrr != MRAA_SUCCESS) {
                err = EIO;
                goto out_gpio_close;
        }
        
        mraa_gpio_isr(ctx->gpio_pin)
        ctx->adc_pin = mraa_aio_init(0);

        *out = ctx;
        return 0;


out_gpio_close:
        mraa_gpio_close(ctx->gpio_pin);
out_sem_destroy:
        sem_destroy(&ctx->run_sem);
out_destroy_dlock:
        pthread_mutex_destroy(&ctx->data_lock);
out_close:
        close(ctx->log_fd);
        ctx->log_fd = 0;
out_free:
        free(ctx);
        *out = NULL;
        return err;
}

// log a formatted message to the log file in the given context.
// Acquires and releases ctx->data_lock. Returns 0 on success and
// err numbers on failure.
static int __log_message(struct lab4b_ctx *ctx, bool to_stdout,
                         const char *fmt, ...)
        __attribute__ ((format (printf, 3, 4)))
{
        va_list args;        
        int ret = 0;

        va_start(args, fmt);

        char buf[512];
        size_t size = sizeof buf;
        char *bufptr = buf;
        time_t t;
        struct tm t_parts;
        struct tm *ret;
        size_t bytes;
        ssize_t sbytes;
        const sigset_t ss;
                
        memset(buf, 0, sizeof buf);
        memset(t_parts, 0, sizeof t_parts);
        sigfillset(&ss);

        // get the time in seconds
        t = time();
        if (t == -1)
                return errno;

        // convert it to parts
        ret = localtime_r(&t, &t_parts)
                if (!ret)
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
        __attribute__ ((format (printf, 2, 3)))
{
        va_list args;
        va_start(args, fmt);
        int ret = __log_message(ctx, false, fmt, args);
        va_end(args);
        return ret;
}

static int log_message_with_stdout(struct lab4b_ctx *ctx,
                                   const char *fmt, ...)
        __attribute__ ((format (printf, 2, 3)))
{
        va_list args;
        va_start(args, fmt);
        int ret = __log_message(ctx, true, fmt, args);
        va_end(args);
        return ret;
}

// called with ctx->lock held
static void *thread(void *arg)
{
        struct lab4b_ctx *ctx = arg;
        const int B = 4275;
        const int R0 = 100000;
        unsigned period;
        enum temp_scale scale;
        struct timespec ts;
        int err;

        err = clock_gettime(CLOCK_REALTIME, &ts);
        if (err)
                die("clock_gettime", errno);

        for (;;) {

                ts.

                period = ctx->period;
                scale = ctx->scale;

                float R = 1023.0/mraa_aio_read_float(ctx->adc_pin) - 1.0;
                R = 100000.0*R;
                float temperature = 1.0/(log(R/100000.0)/B+1/298.15)-273.15;
                // we read in C, so convert to F if necessary
                if (scale == FAHRENHEIT)
                        temperature = 32.0 + temperature*9.0/5.0;

                log_message_with_stdout(ctx, "%.2f", temperature);

                for (;;) {
                        pthread_cond_wait(&ctx->cond, &ctx->lock);
                        if (ctx->thread_state == DEAD)
                                return NULL;
                        if (ctx->thread_state == RUNNING)
                                break;
                        // else the state is STOPPED, so keep sleeping
        }
}

// spawn the thread but don't let it run
static int spawn_thread(struct lab4b_ctx *ctx, void *(*func)(void *))
{
        if (ctx->thread != 0)
                return -EINVAL;

        int err = pthread_create(&ctx->thread, NULL, func, ctx);
}

static int __mod_thread_state(struct lab4b_ctx *ctx,
                              const enum thread_state new)
{
        int err = 0;
        
        pthread_mutex_lock(&ctx->lock);
        const enum thread_state current = ctx->thread_state;

        // make sure the caller is actually requesting a meaningful state
        // change. Our state diagram is thus: 
        //          _______  
        //         /       \
        //        /         v
        // --> RUNNING    STOPPED --> DEAD
        //        ^         /
        //         \_______/
        //         
        if ((current != new) && (current != DEAD)) {
                ctx->thread_state = new;
                err = pthread_cond_signal(&ctx->cond);
                pthread_mutex_unlock(&ctx->lock);

                // if the thread is now dead, reap it.
                if (new == DEAD)
                        err = pthread_join(&ctx->thread, NULL);
        } else
                pthread_mutex_unlock(&ctx->lock);

        return err;
}

static int start_thread(struct lab4b_ctx *ctx)
{
        return __mod_thread_state(ctx, RUNNING);
}

static int stop_thread(struct lab4b_ctx *ctx)
{
        return __mod_thread_state(ctx, STOPPED);
}

static int kill_thread(struct lab4b_ctx *ctx)
{
        return __mod_thread_state(ctx, DEAD);
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
static void *button_isr(void *arg)
{
        struct lab4b_ctx *ctx = arg;
        int err;

        err = stop_thread();
        if (err)
                die("button_isr", err);

        log_message(ctx, "SHUTDOWN", cmd);
        exit(0);
}

// parse a command and execute it
// return 0 on success, error number on failure
static int do_command(const char *cmd, struct lab4b_ctx *ctx)
{
        int err = 0;

        log_message(ctx, "%s", cmd);

        if (strcmp(cmd, "OFF") == 0) {
                // log a SHUTDOWN message and exit.
                kill_thread(ctx);
                log_message(ctx, "SHUTDOWN", cmd);
                exit(0);
        } else if (strcmp(cmd, "STOP") == 0) {
                return stop_thread(ctx);
        } else if (strcmp(cmd, "START") == 0) {
                return start_thread(ctx);
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
                } else if (val < 1 || val > UINT_MAX || *end != NULL) {
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

        return 0;
}

int main(int argc, char **argv)
{
        int ret;
        unsigned period = 1;
        enum temp_scale scale = FAHRENHEIT;
        const char *logname = NULL;
        
        while (-1 != (ret = getopt_long(argc, argv, "", labb_opts, NULL))) {
                switch (ret) {
                case PERIOD_OPT_RET:
                        errno = 0;
                        long val = strtol(optarg, NULL, 10);
                        if (errno) {
                                fprintf(stderr, USAGE_STR);
                                die("main: bad period", errno);
                        } else if (val < 0 || val > UINT_MAX) {
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

        struct lab4b_ctx *ctx;

        err = create_lab4b_ctx(logname, period, scale, &ctx);
        if (err)
                die("failed to create context", err);

        err = spawn_thread(ctx, thread_func);
        if (err)
                die("failed to spawn thread", err);

        err = start_thread(ctx);
        if (err)
                die("failed to start thread");

        for (;;) {
                char *line = readline(NULL);
                err = do_command(cmd, ctx);
                free(line);
                if (err)
                        die("do_command", err);
        }
}


// thread:
//   run_sem
//     data_lock

// stop_thread:
//   run_sem
//     data_lock

