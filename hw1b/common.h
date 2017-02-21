// Eric Mueller -- emueller@hmc.edu -- 40160869
//
// Common functions
#pragma once

// for dprintf
#define _POSIX_C_SOURCE 200809L

#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
#include <pthread.h>
#include <sched.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <termios.h>
#include <unistd.h>

#include <mcrypt.h>

#include <arpa/inet.h>

#include <sys/stat.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/wait.h>

// http://www.ascii-code.com/
#define CR_CHAR 0x0d
#define LF_CHAR 0x0a
#define CTRL_C_CHAR 0x03
#define CTRL_D_CHAR 0x04

void die(const char *reason, int err);
MCRYPT encryption_setup(const char *key_fname);
