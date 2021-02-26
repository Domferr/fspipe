#include "../include/socketconn.h"
#include "../include/utils.h"
#include "../include/scfiles.h"
#include <sys/socket.h>
#include <sys/un.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <sys/select.h>
#include <fcntl.h>
#include <stdio.h>

static struct sockaddr_un socket_get_address(void) {
    struct sockaddr_un sa;
    strncpy(sa.sun_path, SOCKNAME, UNIX_PATH_MAX);
    sa.sun_family = AF_UNIX;
    return sa;
}

int socket_listen(void) {
    struct sockaddr_un sa = socket_get_address();
    int fd_skt;
    MINUS1(fd_skt = socket(AF_UNIX, SOCK_STREAM, 0), return -1)
    MINUS1(bind(fd_skt, (struct sockaddr *) &sa, sizeof(sa)), return -1)
    MINUS1(listen(fd_skt, SOMAXCONN), return -1)

    return fd_skt;
}

int socket_accept(int fd_skt, long timeout) {
    int fd_client, err;
    ISNEGATIVE(timeout, timeout = DEFAULT_TIMEOUT)
    struct timeval time_to_wait = { MS_TO_SEC(timeout), MS_TO_USEC(timeout) };
    fd_set set;

    FD_ZERO(&set);
    FD_SET(fd_skt, &set);
    //aspetto di instaurare una connessione ma se scade il timeout termino
    MINUS1(err = select(fd_skt + 1, &set, NULL, NULL, &time_to_wait), return -1)
    if (err == 0) {
        errno = ETIMEDOUT;
        return -1;
    } else {
        MINUS1(fd_client = accept(fd_skt, NULL, 0), return -1)
    }

    return fd_client;
}

int socket_connect(long timeout) {
    int fd_skt, flags, res;
    struct sockaddr_un sa = socket_get_address();
    struct timeval time_to_wait = { MS_TO_SEC(timeout), MS_TO_USEC(timeout) };

    MINUS1(fd_skt = socket(AF_UNIX, SOCK_STREAM, 0), return -1)

    // get socket flags
    ISNEGATIVE(flags = fcntl(fd_skt, F_GETFL, NULL), return -1)

    // set socket non-blocking
    ISNEGATIVE(fcntl(fd_skt, F_SETFL, flags | O_NONBLOCK), return -1);

    // try to connect
    if ((res = connect(fd_skt, (struct sockaddr *) &sa, sizeof(sa))) < 0) {
        if (errno == EINPROGRESS) {
            fd_set wait_set;

            // make file descriptor set with socket
            FD_ZERO (&wait_set);
            FD_SET(fd_skt, &wait_set);

            // wait for socket to be writable; return after given timeout
            res = select(fd_skt + 1, NULL, &wait_set, NULL, &time_to_wait);
        }
    } else {    // connection was successful immediately
        res = 1;
    }

    // reset socket flags
    ISNEGATIVE(fcntl(fd_skt, F_SETFL, flags), return -1)

    if (res < 0) {  // an error occurred in connect or select
        return -1;
    } else if (res == 0) {    // select timed out
        errno = ETIMEDOUT;
        fprintf(stderr, "timeout\n");
        return -1;
    } else {
        socklen_t len = sizeof(flags);

        // check for errors in socket layer
        ISNEGATIVE(getsockopt(fd_skt, SOL_SOCKET, SO_ERROR, &flags, &len), return -1)

        // there was an error
        if (flags) {
            errno = flags;
            return -1;
        }
    }

    return fd_skt;
}

int socket_destroy(void) {
    return unlink(SOCKNAME);
}