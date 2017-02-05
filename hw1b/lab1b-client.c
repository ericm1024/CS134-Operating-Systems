// lab1b-client.c
//
// Eric Mueller -- emueller@hmc.edu
//
// Implementation of the client for Pomona College CS134's lab1b, as described here:
// http://www.cs.pomona.edu/classes/cs134/projects/P1B.html

#include "common.h"

static struct termios old_termios;

// http://www.ascii-code.com/
#define CR_CHAR 0x0d
#define LF_CHAR 0x0a
#define CTRL_C_CHAR 0x03
#define CTRL_D_CHAR 0x04

// called at exit: clean up the mess we made of the terminal
static void termfix()
{
        tcsetattr(STDIN_FILENO, TCSAFLUSH, &old_termios);
}

#define LOG_OPT_RET 'l'
#define ENCRYPT_OPT_RET 'e'
#define PORT_OPT_RET 'p'

#define USAGE_STR \
        "lab1b-client --port=PORTNUM [--log=FILE] [--encrypt=KEYFILE]"

// parse arguments: --log, --encrypt, --port
static void parse_args(int argc, char **argv, const char **log_fname_out,
                       int *port_out, const char **key_fname_out)
{
        static const struct option lab1b_options[] =
        {
                { // --log=<fname>
                        .name = "log",
                        .has_arg = required_argument,
                        .flag = NULL,
                        .val = LOG_OPT_RET
                },
                { // --encrypt=<keyfile>
                        .name = "encrypt",
                        .has_arg = required_argument,
                        .flag = NULL,
                        .val = ENCRYPT_OPT_RET
                },
                { // --port
                        .name = "port",
                        .has_arg = required_argument,
                        .flag = NULL,
                        .val = PORT_OPT_RET
                },
                {0, 0, 0, 0} // end of array
        };

        const char *log_fname = NULL;
        int port = -1;
        const char *key_fname = NULL;
        int ret;

        for (;;) {
                ret = getopt_long(argc, argv, "", lab1b_options, NULL);
                if (ret == -1)
                        break;

                switch (ret) {
                case LOG_OPT_RET:
                        log_fname = optarg;
                        break;

                case ENCRYPT_OPT_RET:
                        key_fname = optarg;
                        break;

                case PORT_OPT_RET:
                        errno = 0;
                        long p = strtol(optarg, NULL, 10);
                        if (errno) {
                                perror("main: bad port");
                                fprintf(stderr, USAGE_STR);
                                exit(1);
                        }
                        if (p <= 1024 || p >= 65536) {
                                errno = ERANGE;
                                perror("main: bad port");
                                fprintf(stderr, USAGE_STR);
                                exit(1);
                        }
                        port = p;
                        break;

                default:
                        // getopt_long will print an error message for us
                        fprintf(stderr, USAGE_STR);
                        exit(1);
                }
        }

        if (port == -1) {
                fprintf(stderr, USAGE_STR);
                exit(1);
        }

        *log_fname_out = log_fname;
        *port_out = port;
        *key_fname_out = key_fname;
}

// open the logfile, yell loudly if that does not work 
static int logfile_setup(const char *log_fname)
{
        // XXX: should we append to the log or nuke it?
        int log_fd = open(log_fname, O_WRONLY|O_CREAT|O_TRUNC|O_APPEND,
                          S_IRUSR|S_IWUSR);
        if (log_fd < 0)
                die("failed to open log", errno);

        return log_fd;
}

// setup a client socket and connect it to local host on the provided port
static int setup_client_socket(int port)
{
        struct sockaddr_in serv_addr;
        int err, sockfd;

        sockfd = socket(AF_INET, SOCK_STREAM, 0);
        if (sockfd < 0)
                die("socket", errno);

        memset(&serv_addr, 0, sizeof serv_addr);
        serv_addr.sin_family = AF_INET;
        serv_addr.sin_port = htons(port);
        serv_addr.sin_addr.s_addr = htonl((127 << 24) | 1); // 127.0.0.1

        err = connect(sockfd, (struct sockaddr *)&serv_addr, sizeof serv_addr);
        if (err)
                die("connect", errno);

        return sockfd;
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

struct lab1b_client_thread_data {
        int sock_fd;
        int log_fd;
        const char *key_fname;
};

static void *thread(void *arg)
{
        struct lab1b_client_thread_data *data = arg;
        int sock_fd = data->sock_fd;
        int log_fd = data->log_fd;
        MCRYPT td = data->key_fname ? encryption_setup(data->key_fname)
                                    : MCRYPT_FAILED;
        bool encrypted = td != MCRYPT_FAILED;
        bool logging = log_fd >= 0;
        int err;
        free(data);
        
        for (;;) {
                char buf[2];
                ssize_t ret = read(sock_fd, buf, 1);

                // EOF from child
                if (ret == 0)
                        exit(2); 
                else if (ret < 0)
                        die("thread read", errno);

                if (logging) {
                        err = dprintf(log_fd, "RECEIVED %d bytes: %c\n", 1, buf[0]);
                        if (err < 0)
                                die("thread write to log file", -err);
                }

                if (encrypted) {
                        err = mdecrypt_generic(td, buf, 1);
                        if (err)
                                die("mdecrypt_generic", EINVAL);
                }

                if (buf[0] == LF_CHAR) {
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
        bool encrypting = false;
        bool logging = false;
        int port = -1;
        const char *log_fname = NULL;
        const char *key_fname = NULL;
        int err = 0;
        int log_fd = -1;
        int sock_fd = -1;
        MCRYPT td = MCRYPT_FAILED;
        pthread_t tid;

        parse_args(argc, argv, &log_fname, &port, &key_fname);

        // (maybe) open the log file
        if (log_fname) {
                log_fd = logfile_setup(log_fname);
                logging = true;
        }

        // (maybe) set up our encryption stream
        if (key_fname) {
                td = encryption_setup(key_fname);
                encrypting = true;
        }

        sock_fd = setup_client_socket(port);

        // set up terminal modes
        terminal_setup();

        // create thread
        struct lab1b_client_thread_data *data = malloc(sizeof *data);
        if (!data)
                die("malloc", ENOMEM);

        data->sock_fd = sock_fd;
        data->log_fd = log_fd;
        data->key_fname = key_fname;
        err = pthread_create(&tid, NULL, &thread, data);
        // data free()'d by thread

        // read from stdin, echo, write to socket
        for (;;) {
                char buf[2];
                ssize_t ret = read(STDIN_FILENO, buf, 1);
                if (ret < 0)
                        die("read from stdin", errno);

                // input processing
                if (buf[0] == CTRL_D_CHAR) {
                        close(sock_fd);
                        exit(0);
                } else if (buf[0] == CR_CHAR || buf[0] == LF_CHAR) {
                        buf[0] = CR_CHAR;
                        buf[1] = LF_CHAR;
                        ++ret;
                }

                // echo
                ret = write(STDOUT_FILENO, buf, ret);
                if (ret < 0)
                        die("write to stdout", errno);

                // CR->LF transformation
                if (buf[0] == CR_CHAR)
                        buf[0] = LF_CHAR;

                // encrypt if we need to
                if (encrypting) {
                        err = mcrypt_generic(td, buf, 1);
                        if (err)
                                die("mcrypt_generic", EINVAL);
                }

                // log if we need to
                if (logging) {
                        err = dprintf(log_fd, "SENT %d bytes: %c\n", 1, buf[0]);
                        if (err < 0)
                                die("write to log file", -err);
                }

                // write to the server
                ret = write(sock_fd, buf, 1);
                if (ret != 1)
                        die("write to socket", errno);
        }
}
