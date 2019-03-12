#ifndef MULTILOOKUP_H
#define MULTILOOKUP_H

#include <pthread.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/syscall.h>
#include "util.h"


void * requester(void * ptr);

#endif
