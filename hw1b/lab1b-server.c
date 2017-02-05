// lab1b-server.c
//
// Eric Mueller -- emueller@hmc.edu
//
// Implementation of the server for Pomona College CS134's lab1b, as described here:
// http://www.cs.pomona.edu/classes/cs134/projects/P1B.html

#include "common.h"

static pid_t child_pid = -1;

// called at exit: print the child's exit status
static void reap_child()
{
        int status;
        pid_t pid = wait(&status);
        if (pid == -1)
                die("wait", errno);

        fprintf(stderr, "SHELL EXIT SIGNAL=%d STATUS=%d\r\n",
                WIFSIGNALED(status) ? WTERMSIG(status) : 0,
                WEXITSTATUS(status));
}

// handle a sigpipe from the child
static void sigpipe_handler(int sig)
{
        (void)sig;
        exit(0);
}

#define ENCRYPT_OPT_RET 'e'
#define PORT_OPT_RET 'p'

#define USAGE_STR \
        "lab1b-server --port=PORTNUM [--encrypt=KEYFILE]"

// parse arguments: --encrypt, --port
static void parse_args(int argc, char **argv, int *port_out,
                       const char **key_fname_out)
{
        static const struct option lab1b_options[] =
        {
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

        int port = -1;
        const char *key_fname = NULL;
        int ret;

        for (;;) {
                ret = getopt_long(argc, argv, "", lab1b_options, NULL);
                if (ret == -1)
                        break;

                switch (ret) {
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

        *port_out = port;
        *key_fname_out = key_fname;
}

static int setup_server_socket(int port)
{
        struct sockaddr_in serv_addr;
        int err, sockfd;

        sockfd = socket(AF_INET, SOCK_STREAM, 0);
        if (sockfd < 0)
                die("socket", errno);

        memset(&serv_addr, 0, sizeof serv_addr);
        serv_addr.sin_family = AF_INET;
        serv_addr.sin_port = htons(port);
        serv_addr.sin_addr.s_addr = INADDR_ANY;

        err = bind(sockfd, (struct sockaddr *)&serv_addr, sizeof serv_addr);
        if (err)
                die("bind", errno);

        return sockfd;
}

struct lab1b_server_thread_data {
        int sock_fd;
        int child_out_fd;
        const char *key_fname;
};

// read from child pipe, write to socket
static void *thread(void *arg)
{
        struct lab1b_server_thread_data *data = arg;
        int sock_fd = data->sock_fd;
        int child_out_fd = data->child_out_fd;
        MCRYPT td = data->key_fname ? encryption_setup(data->key_fname)
                                    : MCRYPT_FAILED;
        bool encrypted = td != MCRYPT_FAILED;
        int err;
        free(data);

        for (;;) {
                char buf[2];
                ssize_t ret = read(child_out_fd, buf, 1);

                // EOF from child
                if (ret == 0)
                        exit(0); 
                else if (ret < 0)
                        die("thread read", errno);

                // encrypt if we need to
                if (encrypted) {
                        err = mcrypt_generic(td, buf, 1);
                        if (err)
                                die("mcrypt_generic", EINVAL);
                }

                // write to the client
                ret = write(sock_fd, buf, 1);
                if (ret < 0)
                        die("thread write", errno);
        }
        return NULL;
}

int main(int argc, char **argv)
{
        bool encrypting = false;
        int port = -1;
        const char *key_fname = NULL;
        int err = 0;
        int sock_fd = -1;
        MCRYPT td = MCRYPT_FAILED;
        pthread_t tid;
        int child_in_fd = -1;
        int child_out_fd = -1;

        // parse arguments: --port, --encrypt
        parse_args(argc, argv, &port, &key_fname);

        // setup encryption state
        if (key_fname) {
                td = encryption_setup(key_fname);
                encrypting = true;
        }

        // setup socket
        sock_fd = setup_server_socket(port);

        // listen for connections
        err = listen(sock_fd, 1);
        if (err)
                die("listen", errno);

        // accept a connection
        err = accept(sock_fd, NULL, NULL);
        if (err)
                die("accept", errno);
        
        // setup shell
        if (signal(SIGPIPE, sigpipe_handler) == SIG_ERR)
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
                // okay we're in the child: we have some pipe dancing to do.
                // First, close the write end our input pipe, then make our
                // stdin the read end of the pipe
                err = close(child_in[WRITE_END]);
                if (err)
                        die("child: close child_in", errno);

                err = dup2(child_in[READ_END], STDIN_FILENO);
                if (err < 0)
                        die("child: dup2 child_in", errno);
                        
                // next close the read end of our stdout pipe, and make that
                // pipe our stdout and stderr
                err = close(child_out[READ_END]);
                if (err)
                        die("child: close child_out", errno);

                err = dup2(child_out[WRITE_END], STDOUT_FILENO);
                if (err < 0)
                        die("child: dup2 child_out -> stdout", errno);

                err = dup2(child_out[WRITE_END], STDERR_FILENO);
                if (err < 0)
                        die("child: dup2 child_out -> stderr", errno);

                // we've set up all of input and output, we're ready to spawn
                // a shell
                // http://stackoverflow.com/q/4204915/3775803
                err = execl("/bin/bash", "/bin/bash", NULL);
                if (err)
                        die("child: execl", errno);
        } else {
                // okay we're in the parent, do the opposite file descriptor
                // dance
                err = close(child_in[READ_END]);
                if (err)
                        die("close child_in", errno);

                err = close(child_out[WRITE_END]);
                if (err)
                        die("close child_out", errno);

                child_in_fd = child_in[WRITE_END];
                child_out_fd = child_out[READ_END];

                // start a thread to copy from child out to our out
                struct lab1b_server_thread_data *data = malloc(sizeof *data);
                if (!data)
                        die("malloc", ENOMEM);

                data->sock_fd = sock_fd;
                data->child_out_fd = child_out_fd;
                data->key_fname = key_fname;
                
                err = pthread_create(&tid, NULL, &thread, &child_out_fd);
                if (err)
                        die("pthread_create", err);
        }

        // read from socket, write to child
        for (;;) {
                char buf[2];
                ssize_t ret = read(sock_fd, buf, 1);

                // read error or EOF from client
                if (ret <= 0) {
                        kill(child_pid, SIGTERM);
                        close(child_in_fd);
                        close(child_out_fd);
                        if (ret < 0)
                                fprintf(stderr, "read from socket: %s\n",
                                        strerror(errno));
                        exit(2);
                }

                // decrypt if we need to
                if (encrypting) {
                        err = mdecrypt_generic(td, buf, 1);
                        if (err)
                                die("mdecrypt_generic", EINVAL);
                }

                // ^C processing
                if (buf[0] == CTRL_C_CHAR) {
                        err = kill(child_pid, SIGINT);
                        if (err)
                                die("kill", errno);
                }

                // NB: we let the client to input processing
                // (i.e. CR -> LF transformation). Technically we shouldn't
                // trust the client, but that's a little overkill here

                ret = write(child_in_fd, buf, 1);
                if (ret < 0)
                        die("write to child stdin", errno);
        }
// spin off thread
}
