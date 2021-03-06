#include <errno.h>
#include <unistd.h>
#include "../include/openfiles.h"
#include "../include/utils.h"
#include "../include/icl_hash.h"

#define NBUCKETS 128 // number of buckets used for the open files hash table

extern struct netpipefs_socket netpipefs_socket;

static icl_hash_t *open_files_table = NULL; // hash table with all the open files. Each file has its path as key
static pthread_mutex_t open_files_mtx = PTHREAD_MUTEX_INITIALIZER;

int netpipefs_open_files_table_init(void) {
    // destroys the table if it already exists
    if (open_files_table != NULL) MINUS1(netpipefs_open_files_table_destroy(), return -1)

    open_files_table = icl_hash_create(NBUCKETS, NULL, NULL);
    if (open_files_table == NULL) return -1;

    return 0;
}

int netpipefs_shutdown(void) {
    int i, err;
    struct icl_entry_s *entry; // hash table entry
    char *path; // entry's key
    struct netpipe *file; // entry's value

    PTH(err, pthread_mutex_lock(&open_files_mtx), return -1)

    if (open_files_table != NULL) {
        icl_hash_foreach(open_files_table, i, entry, path, file) {
            err = netpipe_force_exit(file, &netpipefs_poll_notify);
            if (err == -1) {
                pthread_mutex_unlock(&open_files_mtx);
                return -1;
            }
        }
    }

    PTH(err, pthread_mutex_unlock(&open_files_mtx), return -1)

    return 0;
}

void netpipefs_poll_destroy(void *ph) {
    fuse_pollhandle_destroy((struct fuse_pollhandle *) ph);
}

void netpipefs_poll_notify(void *ph) {
    fuse_notify_poll((struct fuse_pollhandle *) ph);
    netpipefs_poll_destroy(ph);
}

static void openfiles_free_netpipe(void *np) {
    struct netpipe *file = (struct netpipe *) np;
    netpipe_free(file, &netpipefs_poll_destroy);
}

int netpipefs_open_files_table_destroy(void) {
    if (open_files_table == NULL) return 0;

    if (icl_hash_destroy(open_files_table, NULL, &openfiles_free_netpipe) == -1)
        return -1;
    open_files_table = NULL;
    return 0;
}

struct netpipe *netpipefs_get_open_file(const char *path) {
    int err;
    struct netpipe *file = NULL;

    PTH(err, pthread_mutex_lock(&open_files_mtx), return NULL)

    if (open_files_table == NULL) {
        errno = EPERM;
    } else {
        file = icl_hash_find(open_files_table, (char *) path);
    }

    PTH(err, pthread_mutex_unlock(&open_files_mtx), return NULL)

    return file;
}

int netpipefs_remove_open_file(const char *path) {
    int deleted, err;
    PTH(err, pthread_mutex_lock(&open_files_mtx), return -1)

    if (open_files_table == NULL) {
        errno = EPERM;
        deleted = -1;
    } else {
        deleted = icl_hash_delete(open_files_table, (char *) path, NULL, NULL);
    }

    PTH(err, pthread_mutex_unlock(&open_files_mtx), return -1)
    return deleted;
}

struct netpipe *netpipefs_get_or_create_open_file(const char *path, int *just_created) {
    int err;
    struct netpipe *file;
    *just_created = 0;

    PTH(err, pthread_mutex_lock(&open_files_mtx), return NULL)
    if (open_files_table == NULL) {
        errno = EPERM;
        pthread_mutex_unlock(&open_files_mtx);
        return NULL;
    }

    file = icl_hash_find(open_files_table, (char*) path);
    EQNULL(file, file = netpipe_alloc(path); *just_created = 1)

    if (file != NULL && *just_created) {
        if (icl_hash_insert(open_files_table, (void*) file->path, file) == NULL) {
            netpipe_free(file, NULL); // there are no poll handle
            file = NULL;
        }
    }

    PTH(err, pthread_mutex_unlock(&open_files_mtx), return NULL)

    return file;
}