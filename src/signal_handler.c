#include "../include/signal_handler.h"
#include "../include/utils.h"
#include "../include/openfiles.h"
#include "../include/scfiles.h"
#include <pthread.h>
#include <errno.h>
#include <stdio.h>
#include <unistd.h>

/* Fuse channel and fuse structure. Used to unmount the filesystem when a signal arrives */
static struct fuse_chan *chan;
static struct fuse *fuse;

/* Thread signal handler */
static pthread_t sig_handler_tid;
static int pipefd[2];  // used to communicate with main thread

/** Function executed by the signal handler. The argument is the set of signal that should be caught */
static void *signal_handler_thread(void *arg) {
    sigset_t *set = arg;
    int err, sig;

    /* Wait for SIGINT or SIGTERM */
    PTHERR(err, sigwait(set, &sig), return NULL)

    /* Stop all the operations on file */
    err = netpipefs_exit_all();
    if (err == -1) perror("signal handler");

    /* Exit from loop */
    fuse_exit(fuse);

    DEBUG("Exit...\n");
    msleep(500); // TODO this is a workaround. All the operations should be ended after 500ms

    /* Unmount the filesystem */
    if (netpipefs_options.mountpoint && chan)
        fuse_unmount(netpipefs_options.mountpoint, chan);

    /* Close the write end of the pipe */
    close(pipefd[1]);
    pipefd[1] = -1;

    return 0;
}

int netpipefs_set_signal_handlers(sigset_t *set, struct fuse_chan *ch, struct fuse *fu) {
    int err;
    MINUS1(pipe(pipefd), return -1)

    /* Set SIGINT, SIGTERM and intr_signal */
    MINUS1(sigemptyset(set), return -1)
    MINUS1(sigaddset(set, SIGINT), return -1)
    MINUS1(sigaddset(set, SIGTERM), return -1)
    MINUS1(sigaddset(set, SIGPIPE), return -1)
    if (netpipefs_options.intr)
        MINUS1(sigaddset(set, netpipefs_options.intr_signal), return -1)

    /* Block SIGINT, SIGTERM and SIGPIPE for main thread */
    PTH(err, pthread_sigmask(SIG_BLOCK, set, NULL), return -1)

    /* Do not handle SIGPIPE */
    MINUS1(sigdelset(set, SIGPIPE), return -1)

    fuse = fu;
    chan = ch;
    /* Run signal handler thread */
    PTH(err, pthread_create(&sig_handler_tid, NULL, &signal_handler_thread, set), if (fuse) fuse = NULL; if (chan) chan = NULL; return -1)

    PTH(err, pthread_detach(sig_handler_tid), return -1)

    return 0;
}

int netpipefs_remove_signal_handlers(void) {
    int err, unused;
    if (pipefd[0] == -1) return 0; // already stopped

    // Send SIGINT to stop the thread
    if ((err = pthread_kill(sig_handler_tid, SIGINT)) != 0) {
        if (err == ESRCH) return 0; // already ended
        errno = err;
        return -1;
    }

    err = readn(pipefd[0], &unused, sizeof(int));

    /* Close read end */
    MINUS1(close(pipefd[0]), return -1)
    pipefd[0] = -1;

    //PTH(err, pthread_join(sig_handler_tid, NULL), return -1)
    /*err = pthread_join(sig_handler_tid, NULL);
    if (err != 0 && err != EINVAL) {
        errno = err;
        return -1;
    }*/

    return err;
}