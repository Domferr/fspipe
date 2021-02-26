#define FUSE_USE_VERSION 29 //fuse version 2.9. Needed by fuse.h

#include <fuse.h>
#include <string.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <stddef.h>
#include <unistd.h>
#include "../include/utils.h"
#include "../include/socketconn.h"
#include "../include/scfiles.h"

/*
 * Command line options
 *
 * We can't set default values for the char* fields here because
 * fuse_opt_parse would attempt to free() them when the user specifies
 * different values on the command line.
 *
 * From https://github.com/libfuse/libfuse/blob/master/example/hello.c
 */
static struct fspipe {
    const char *host;
    int port;
    int remote_port;
    int show_help;
    int debug;
    long timeout;
} fspipe;

#define FSPIPE_OPT(t, p, v) { t, offsetof(struct fspipe, p), v }

static const struct fuse_opt fspipe_opts[] = {
        FSPIPE_OPT("--host=%s",         host, 0),
        FSPIPE_OPT("--port=%d",         port, 0),
        FSPIPE_OPT("--remote_port=%d",  remote_port, 0),
        FSPIPE_OPT("--timeout=%d",      timeout, 0),
        FSPIPE_OPT("-h",                show_help, 1),
        FSPIPE_OPT("--help",            show_help, 1),
        FSPIPE_OPT("-d",                debug, 1),
        FSPIPE_OPT("debug",             debug, 1),

        FUSE_OPT_END
};

#define DEBUG(...)						\
	do { if (fspipe.debug) fprintf(stderr, ##__VA_ARGS__); } while(0)

/**
 * Initialize filesystem
 *
 * The return value will passed in the `private_data` field of
 * `struct fuse_context` to all file operations, and as a
 * parameter to the destroy() method. It overrides the initial
 * value provided to fuse_main() / fuse_new().
 */
// Undocumented but extraordinarily useful fact:  the fuse_context is
// set up before this function is called, and
// fuse_get_context()->private_data returns the user_data passed to
// fuse_main().  Really seems like either it should be a third
// parameter coming in here, or else the fact should be documented
// (and this might as well return void, as it did in older versions of
// FUSE).
/*static void* init_callback(struct fuse_conn_info *conn)
{
    DEBUG("init() callback\n")

    return 0;
}*/

/**
 * Clean up filesystem
 *
 * Called on filesystem exit.
 */
static void destroy_callback(void *privatedata) {
    DEBUG("\n%s\n", "destroy() callback");
    int *fd_skt = (int*) privatedata;
    if (*fd_skt != -1) {
        close(*fd_skt);
        socket_destroy();
    }
}

/** Get file attributes.
 *
 * Similar to stat().  The 'st_dev' and 'st_blksize' fields are
 * ignored. The 'st_ino' field is ignored except if the 'use_ino'
 * mount option is given. In that case it is passed to userspace,
 * but libfuse and the kernel will still assign a different
 * inode for internal use (called the "nodeid").
 *
 * `fi` will always be NULL if the file is not currently open, but
 * may also be NULL if the file is open.
 */
static int getattr_callback(const char *path, struct stat *stbuf) {
    memset(stbuf, 0, sizeof(struct stat));
    if (strcmp(path, "/") == 0) {
        stbuf->st_mode = S_IFDIR | 0755;
        stbuf->st_nlink = 2;
    }

    stbuf->st_mode = S_IFREG | 0444;
    stbuf->st_nlink = 1;

    return 0;
}

/** Open a file
 *
 * Open flags are available in fi->flags. The following rules
 * apply.
 *
 *  - Creation (O_CREAT, O_EXCL, O_NOCTTY) flags will be
 *    filtered out / handled by the kernel.
 *
 *  - Access modes (O_RDONLY, O_WRONLY, O_RDWR, O_EXEC, O_SEARCH)
 *    should be used by the filesystem to check if the operation is
 *    permitted.  If the ``-o default_permissions`` mount option is
 *    given, this check is already done by the kernel before calling
 *    open() and may thus be omitted by the filesystem.
 *
 *  - When writeback caching is enabled, the kernel may send
 *    read requests even for files opened with O_WRONLY. The
 *    filesystem should be prepared to handle this.
 *
 *  - When writeback caching is disabled, the filesystem is
 *    expected to properly handle the O_APPEND flag and ensure
 *    that each write is appending to the end of the file.
 *
 *  - When writeback caching is enabled, the kernel will
 *    handle O_APPEND. However, unless all changes to the file
 *    come through the kernel this will not work reliably. The
 *    filesystem should thus either ignore the O_APPEND flag
 *    (and let the kernel handle it), or return an error
 *    (indicating that reliably O_APPEND is not available).
 *
 * Filesystem may store an arbitrary file handle (pointer,
 * index, etc) in fi->fh, and use this in other all other file
 * operations (read, write, flush, release, fsync).
 *
 * Filesystem may also implement stateless file I/O and not store
 * anything in fi->fh.
 *
 * There are also some flags (direct_io, keep_cache) which the
 * filesystem may set in fi, to change the way the file is opened.
 * See fuse_file_info structure in <fuse_common.h> for more details.
 *
 * If this request is answered with an error code of ENOSYS
 * and FUSE_CAP_NO_OPEN_SUPPORT is set in
 * `fuse_conn_info.capable`, this is treated as success and
 * future calls to open will also succeed without being send
 * to the filesystem process.
 *
 */
static int open_callback(const char *path, struct fuse_file_info *fi) {
    int *fd_skt = (int*)(fuse_get_context()->private_data), confirm;
    size_t size;
    //TODO read/write with timeouts

    if ((fi->flags & O_ACCMODE) == O_RDONLY) {        // open the file for read access
        if (*fd_skt == -1)
            MINUS1ERR(*fd_skt = socket_listen(), return -ENOENT)
        int fd_client;
        MINUS1(fd_client = socket_accept(*fd_skt, fspipe.timeout), return -ENOENT)

        // read the path requested by the client
        MINUS1(readn(fd_client, &size, sizeof(size_t)), return -ENOENT)
        char *other_path = (char*) malloc(sizeof(char)*size);
        EQNULL(other_path, close(fd_client); return -ENOENT)
        MINUS1(readn(fd_client, other_path, sizeof(char)*size), free(other_path); return -ENOENT)

        // compare its path with mine and send confirmation
        confirm = strcmp(path, other_path) == 0 ? 1:0;
        free(other_path);
        MINUS1(writen(fd_client, &confirm, sizeof(int)), return -ENOENT)
        if (!confirm) {
            close(fd_client);
            return -ENOENT;
        }

        fi->fh = fd_client;
        fi->direct_io = 1;  // avoid kernel caching
        fi->nonseekable = 1; // seeking, or calling pread(2) or pwrite(2) with a nonzero position is not supported on sockets. (from man 7 socket)
    } else if ((fi->flags & O_ACCMODE) == O_WRONLY) {  // open the file for write access
        MINUS1(*fd_skt = socket_connect(fspipe.timeout), perror("failed to connect"); return -ENOENT)

        // send the path requested by the client
        size = strlen(path);
        MINUS1(writen(*fd_skt, &size, sizeof(size_t)), return -ENOENT)
        MINUS1(writen(*fd_skt, (void*) path, sizeof(char)*size), return -ENOENT)

        // read the confirmation
        MINUS1(readn(*fd_skt, &confirm, sizeof(int)), return -ENOENT)
        if (!confirm) {
            close(*fd_skt);
            return -ENOENT;
        }
    } else if ((fi->flags & O_ACCMODE) == O_RDWR) {     // open the file for both reading and writing
        DEBUG("read and write access\n");
        return -EINVAL;
    } else {
        return -EINVAL;
    }

    DEBUG("established connection for %s\n", path);
    return 0;
}

/** Read data from an open file
 *
 * Read should return exactly the number of bytes requested except
 * on EOF or error, otherwise the rest of the data will be
 * substituted with zeroes.	 An exception to this is when the
 * 'direct_io' mount option is specified, in which case the return
 * value of the read system call will reflect the return value of
 * this operation.
 */
static int read_callback(const char *path, char *buf, size_t size, off_t offset, struct fuse_file_info *fi) {
    int fd_client = fi->fh; //file descriptor for client communication
    ssize_t read;
    MINUS1(read = readn(fd_client, buf, size), return -errno)
    return read;
}

/** Write data to an open file
 *
 * Write should return exactly the number of bytes requested
 * except on error.	 An exception to this is when the 'direct_io'
 * mount option is specified (see read operation).
 *
 * Unless FUSE_CAP_HANDLE_KILLPRIV is disabled, this method is
 * expected to reset the setuid and setgid bits.
 */
static int write_callback(const char *path, const char *buf, size_t size, off_t offset, struct fuse_file_info *fi) {
    int *fd_skt = (int*)(fuse_get_context()->private_data);
    ssize_t wrote;
    MINUS1(wrote = writen(*fd_skt, (void*) buf, size), return -errno)
    return wrote;
}

/**
 * Create and open a file
 *
 * If the file does not exist, first create it with the specified
 * mode, and then open it.
 *
 * If this method is not implemented or under Linux kernel
 * versions earlier than 2.6.15, the mknod() and open() methods
 * will be called instead.
 */
static int create_callback(const char *path, mode_t mode, struct fuse_file_info *fi) {
    return open_callback(path, fi);
}

/** Release an open file
 *
 * Release is called when there are no more references to an open
 * file: all file descriptors are closed and all memory mappings
 * are unmapped.
 *
 * For every open() call there will be exactly one release() call
 * with the same flags and file handle.  It is possible to
 * have a file opened more than once, in which case only the last
 * release will mean, that no more reads/writes will happen on the
 * file.  The return value of release is ignored.
 */
static int release_callback(const char *path, struct fuse_file_info *fi) {
    if ((fi->flags & O_ACCMODE) == O_RDONLY) {
        MINUS1(close(fi->fh), return -errno)
    } else {
        int *fd_skt = (int*)(fuse_get_context()->private_data);
        MINUS1(close(*fd_skt), return -errno)
        *fd_skt = -1;
    }

    DEBUG("closed connection for %s\n", path);
    return 0;
}

/** Change the size of a file
 *
 * `fi` will always be NULL if the file is not currently open, but
 * may also be NULL if the file is open.
 *
 * Unless FUSE_CAP_HANDLE_KILLPRIV is disabled, this method is
 * expected to reset the setuid and setgid bits.
 *
 * (It is ignored but it seems to be required..)
 */
static int truncate_callback(const char *path, off_t newsize) {
    return 0;
}

static const struct fuse_operations fspipe_oper = {
    .destroy = destroy_callback,
    .getattr = getattr_callback,
    .open = open_callback,
    .create = create_callback,
    .read = read_callback,
    .write = write_callback,
    .release = release_callback,
    .truncate = truncate_callback
};

static void show_help(const char *progname)
{
    printf("usage: %s [options] <mountpoint>\n\n", progname);
    printf("File-system specific options:\n"
           "    --host=<s>              When given it will act as a client, otherwise as a server\n"
           "    --port=<d>              The port used for the socket connection\n"
           "                            (default: %d)\n"
           "\n", DEFAULT_PORT);
}

int main(int argc, char** argv) {
    struct fuse_args args = FUSE_ARGS_INIT(argc, argv);

    /* Set defaults */
    fspipe.timeout = DEFAULT_TIMEOUT;
    fspipe.host = NULL;
    fspipe.port = DEFAULT_PORT;
    fspipe.remote_port = -1;

    /* Parse options */
    MINUS1ERR(fuse_opt_parse(&args, &fspipe, fspipe_opts, NULL), return 1)

    if (fspipe.debug) {
        MINUS1ERR(fuse_opt_add_arg(&args, "-d"), return 1)
    }
    /* When --help is specified, first print file-system specific help text,
       then signal fuse_main to show additional help (by adding `--help` to the options again)
       without usage: line (by setting argv[0] to the empty string) */
    if (fspipe.show_help) {
        show_help(argv[0]);
        MINUS1ERR(fuse_opt_add_arg(&args, "--help"), return 1)
        args.argv[0][0] = '\0'; //setting argv[0] to the empty string   //TODO this is not working. The usage line is still printed by fuse, why?
    } else if (fspipe.host == NULL) {
        fprintf(stderr, "missing host\nsee '%s -h' for usage\n", argv[0]);
        return 1;
    } else if (fspipe.remote_port == -1) {
        fprintf(stderr, "missing remote port\nsee '%s -h' for usage\n", argv[0]);
        return 1;
    }

    int *fd_skt;
    EQNULLERR(fd_skt = (int*) malloc(sizeof(int)), return 1)
    *fd_skt = -1;
    DEBUG("fspipe running on local port %d and host %s:%d\n", fspipe.port, fspipe.host, fspipe.remote_port);

    int ret = fuse_main(args.argc, args.argv, &fspipe_oper, fd_skt);

    DEBUG("%s\n", "cleanup");
    fuse_opt_free_args(&args);
    free(fd_skt);
    return ret;
}