#include <sys/socket.h>
#include <sys/un.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <sys/select.h>
#include <fcntl.h>
#include <stdlib.h>
#include <stdio.h>
#include "../include/utils.h"
#include "../include/socketconn.h"
#include "../include/scfiles.h"

static struct sockaddr_un socket_get_address(int port) {
    struct sockaddr_un sa;
    char sockname[UNIX_PATH_MAX];
    sprintf(sockname, "%s%d.sock", BASESOCKNAME, port);
    strncpy(sa.sun_path, sockname, UNIX_PATH_MAX);
    sa.sun_family = AF_UNIX;
    return sa;
}

int socket_listen(int port) {
    struct sockaddr_un sa = socket_get_address(port);
    int fd_skt;
    MINUS1(fd_skt = socket(AF_UNIX, SOCK_STREAM, 0), return -1)
    MINUS1(bind(fd_skt, (struct sockaddr *) &sa, sizeof(sa)), close(fd_skt); return -1)
    MINUS1(listen(fd_skt, SOMAXCONN), close(fd_skt); return -1)

    return fd_skt;
}

int socket_accept(int fd_skt, long timeout) {
    int fd_client, err;
    ISNEGATIVE(timeout, timeout = DEFAULT_TIMEOUT)
    struct timeval time_to_wait = { MS_TO_SEC(timeout), MS_TO_USEC(timeout) };
    fd_set set;

    FD_ZERO(&set);
    FD_SET(fd_skt, &set);

    // aspetto di instaurare una connessione ma se scade il timeout termino
    MINUS1(err = select(fd_skt + 1, &set, NULL, NULL, &time_to_wait), return -1)
    if (err == 0) {
        errno = ETIMEDOUT;
        return -1;
    } else {
        MINUS1(fd_client = accept(fd_skt, NULL, 0), return -1)
    }

    return fd_client;
}

int socket_connect(int port, long timeout) {
    ISNEGATIVE(timeout, timeout = DEFAULT_TIMEOUT)
    int fd_skt, flags, res;
    struct sockaddr_un sa = socket_get_address(port);

    MINUS1(fd_skt = socket(AF_UNIX, SOCK_STREAM, 0), return -1)

    // get socket flags
    ISNEGATIVE(flags = fcntl(fd_skt, F_GETFL, NULL), close(fd_skt); return -1)
    // set socket to non-blocking
    ISNEGATIVE(fcntl(fd_skt, F_SETFL, flags | O_NONBLOCK), close(fd_skt); return -1)

    // try to connect
    while ((res = connect(fd_skt, (struct sockaddr *) &sa, sizeof(sa))) < 0) {
        if (errno == ENOENT && timeout > 0) {
            long sleeptime = timeout < CONNECT_INTERVAL ? timeout:CONNECT_INTERVAL;
            MINUS1(msleep(sleeptime), close(fd_skt); return -1)
            timeout = timeout - sleeptime;
        } else {
            break;
        }
    }

    if (res < 0) {
        if (errno == EINPROGRESS) {
            fd_set wait_set;

            // make file descriptor set with socket
            FD_ZERO(&wait_set);
            FD_SET(fd_skt, &wait_set);

            // wait for socket to be writable; return after given timeout
            struct timeval time_to_wait = { MS_TO_SEC(timeout), MS_TO_USEC(timeout) };
            res = select(fd_skt + 1, NULL, &wait_set, NULL, &time_to_wait);
        } else if (timeout == 0) {
            res = 0;
        }
    } else {
        res = 1;
    }

    // reset socket flags
    ISNEGATIVE(fcntl(fd_skt, F_SETFL, flags), close(fd_skt); return -1)

    if (res < 0) {  // an error occurred in connect or select
        close(fd_skt);
        return -1;
    } else if (res == 0) {    // select timed out or attempted to connect many times without success
        errno = ETIMEDOUT;
        close(fd_skt);
        return -1;
    } else {
        socklen_t len = sizeof(flags);
        // check for errors in socket layer
        ISNEGATIVE(getsockopt(fd_skt, SOL_SOCKET, SO_ERROR, &flags, &len), return -1)
        // there was an error
        if (flags) {
            errno = flags;
            close(fd_skt);
            return -1;
        }
    }

    return fd_skt;
}

int socket_connect_interval(int fd_skt, int port, long timeout) {
    struct sockaddr_un sa = socket_get_address(port);
    int res;
    while ((res = connect(fd_skt, (struct sockaddr *) &sa, sizeof(sa))) < 0) {
        if (errno == ENOENT && timeout > 0) {
            long sleeptime = timeout < CONNECT_INTERVAL ? timeout:CONNECT_INTERVAL;
            MINUS1(msleep(sleeptime), close(fd_skt); return -1)
            timeout = timeout - sleeptime;
        } else {
            break;
        }
    }

    if (timeout == 0) {
        errno = ETIMEDOUT;
        return -1;
    }

    return res;
}

int socket_write_h(int fd_skt, void *data, size_t size) {
    int bytes = writen(fd_skt, &size, sizeof(size_t));
    if (bytes > 0)
        return writen(fd_skt, data, size);
    return bytes;
}

int socket_read_h(int fd_skt, void **ptr) {
    int bytes;
    size_t size = 0;
    bytes = readn(fd_skt, &size, sizeof(size_t));
    if (bytes <= 0) return bytes;

    if (size <= 0) {
        errno = EINVAL;
        return -1;
    }

    if (*ptr != NULL) free(*ptr);
    EQNULL(*ptr = (void*) malloc(size), return -1)

    bytes = readn(fd_skt, *ptr, size);
    if (bytes <= 0) free(*ptr);

    return bytes;
}

int socket_destroy(int fd, int port) {
    char sockname[UNIX_PATH_MAX];
    close(fd);
    sprintf(sockname, "%s%d.sock", BASESOCKNAME, port);
    return unlink(sockname);
}