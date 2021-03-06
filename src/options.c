#include "../include/options.h"
#include "../include/netpipefs_socket.h"
#include "../include/utils.h"
#include "../include/netpipe.h"
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>

struct netpipefs_options netpipefs_options;

#define NETPIPEFS_OPT(t, p, v) { t, offsetof(struct netpipefs_options, p), v }

/**
 * NetpipeFS's option descriptor array
 */
static const struct fuse_opt netpipefs_opts[] = {
        NETPIPEFS_OPT("-h",                 show_help, 1),
        NETPIPEFS_OPT("--help",             show_help, 1),
        NETPIPEFS_OPT("-d",                 debug, 1),
        NETPIPEFS_OPT("--debug",            debug, 1),
        NETPIPEFS_OPT("-p %i",              port, 0),
        NETPIPEFS_OPT("--port=%i",          port, 0),
        NETPIPEFS_OPT("--timeout=%i",       timeout, 0),
        NETPIPEFS_OPT("--hostip=%s",        hostip, 0),
        NETPIPEFS_OPT("--hostport=%i",      hostport, 0),
        NETPIPEFS_OPT("--writeahead=%i",    writeahead, 0),
        NETPIPEFS_OPT("--readahead=%i",     readahead, 0),
        NETPIPEFS_OPT("-delayconnect",      delayconnect, 1),

        FUSE_OPT_END
};

int netpipefs_opt_parse(const char *progname, struct fuse_args *args) {
    /* Set defaults */
    netpipefs_options.mountpoint = NULL;
    netpipefs_options.multithreaded = 1;
    netpipefs_options.foreground = 0;
    netpipefs_options.timeout = DEFAULT_TIMEOUT;
    netpipefs_options.port = DEFAULT_PORT;
    netpipefs_options.hostip = NULL;
    netpipefs_options.hostport = DEFAULT_PORT;
    netpipefs_options.delayconnect = 0;
    netpipefs_options.readahead = DEFAULT_READAHEAD;
    netpipefs_options.writeahead = DEFAULT_WRITEAHEAD;
    //netpipefs_options.intr = 1;

    /* Parse options */
    MINUS1(fuse_opt_parse(args, &netpipefs_options, netpipefs_opts, NULL), return -1)
    MINUS1(fuse_parse_cmdline(args, &netpipefs_options.mountpoint, &netpipefs_options.multithreaded, &netpipefs_options.foreground), return -1)

    /* When --help is specified, first print usage text, then exit with success */
    if (netpipefs_options.show_help) {
        netpipefs_usage(progname);
        return 1;
    }

    /* Validate host ip address */
    int array[4];
    if (netpipefs_options.hostip == NULL) { // host ip is missing
        fprintf(stderr, "missing host ip address\nsee '%s -h' for usage\n", progname);
        return 1;
    } else if (strcmp(netpipefs_options.hostip, "localhost") == 0) { // localhost is valid. Will use afunix sockets
        //netpipefs_options.delayconnect = 0; // if AF_UNIX then do not delay connect
    } else if (ipv4_address_to_array(netpipefs_options.hostip, array) == -1) { // validate host ip address
        fprintf(stderr, "invalid host ip address\nsee '%s -h' for usage\n", progname);
        return 1;
    }

    /* Check host port */
    if (netpipefs_options.hostport < 0) {
        fprintf(stderr, "invalid host port\nsee '%s -h' for usage\n", progname);
        return 1;
    }

    /* Check local port */
    if (netpipefs_options.port < 0) {
        fprintf(stderr, "invalid port\nsee '%s -h' for usage\n", progname);
        return 1;
    }

    /*if (netpipefs_options.pipecapacity < 0) {
        fprintf(stderr, "invalid pipe capacity\nsee '%s -h' for usage\n", progname);
        return 1;
    }*/

    /* Check for debug flag */
    if (netpipefs_options.debug) {
        MINUS1ERR(fuse_opt_add_arg(args, "-d"),  return -1)
        netpipefs_options.foreground = 1;
    }

    /*if (netpipefs_options.intr) {
        MINUS1ERR(fuse_opt_add_arg(args, "-ointr"), return -1)
        netpipefs_options.intr_signal = SIGUSR1;
        MINUS1ERR(fuse_opt_add_arg(args, "-ointr_signal=10"), return -1)
    }*/
    return 0;
}

void netpipefs_opt_free(struct fuse_args *args) {
    if (netpipefs_options.hostip) {
        free((void*) netpipefs_options.hostip);
        netpipefs_options.hostip = NULL;
    }
    if (netpipefs_options.mountpoint) {
        free((void*) netpipefs_options.mountpoint);
        netpipefs_options.mountpoint = NULL;
    }
    fuse_opt_free_args(args);
}

/** Prints FUSE's usage without the "netpipefs_usage: ..." line */
static void fuse_usage(void);

void netpipefs_usage(const char *progname) {
    printf("usage: %s [options] <mountpoint>\n"
           "\n", progname);
    printf("netpipefs options:\n"
           "    -p <d>, --port=<d>      local port used for the socket connection (default: %d)\n"
           "    --hostip=<s>            remote host ipv4 address to which connect to. if localhost then AF_UNIX sockets are used\n"
           "    --hostport=<d>          remote port used for the socket connection (default: %d)\n"
           "    --timeout=<d>           connection timeout expressed in milliseconds (default: %d ms)\n"
           "    -delayconnect           connect to host after the filesystem is mounted\n"
           "    --readahead=<d>         how many bytes can be received and put into the buffer to anticipate read requests (default: %d)\n"
           "    --writeahead=<d>        how many bytes can be bufferized on write requests if the remote host can't receive data (default: %d)\n"
           "\n", DEFAULT_PORT, DEFAULT_PORT, DEFAULT_TIMEOUT, DEFAULT_READAHEAD, DEFAULT_WRITEAHEAD);
    fuse_usage();
}

static void fuse_usage(void) {
    printf("general options:\n"
           "    -o opt,[opt...]        mount options\n"
           "    -h   --help            print help\n"
           "    -V   --version         print version\n"
           "\n"
           "FUSE options:\n"
           "    -d   -o debug          enable debug output (implies -f)\n"
           "    -f                     foreground operation\n"
           "    -s                     disable multi-threaded operation\n"
           "\n"
           "    -o allow_other         allow access to other users\n"
           "    -o allow_root          allow access to root\n"
           "    -o auto_unmount        auto unmount on process termination\n"
           "    -o nonempty            allow mounts over non-empty file/dir\n"
           "    -o default_permissions enable permission checking by kernel\n"
           "    -o fsname=NAME         set filesystem name\n"
           "    -o subtype=NAME        set filesystem type\n"
           "    -o large_read          issue large read requests (2.4 only)\n"
           "    -o max_read=N          set maximum size of read requests\n"
           "\n"
           "    -o hard_remove         immediate removal (don't hide files)\n"
           "    -o use_ino             let filesystem set inode numbers\n"
           "    -o readdir_ino         try to fill in d_ino in readdir\n"
           "    -o direct_io           use direct I/O\n"
           "    -o kernel_cache        cache files in kernel\n"
           "    -o [no]auto_cache      enable caching based on modification times (off)\n"
           "    -o umask=M             set file permissions (octal)\n"
           "    -o uid=N               set file owner\n"
           "    -o gid=N               set file group\n"
           "    -o entry_timeout=T     cache timeout for names (1.0s)\n"
           "    -o negative_timeout=T  cache timeout for deleted names (0.0s)\n"
           "    -o attr_timeout=T      cache timeout for attributes (1.0s)\n"
           "    -o ac_attr_timeout=T   auto cache timeout for attributes (attr_timeout)\n"
           "    -o noforget            never forget cached inodes\n"
           "    -o remember=T          remember cached inodes for T seconds (0s)\n"
           "    -o nopath              don't supply path if not necessary\n"
           "    -o intr                allow requests to be interrupted\n"
           "    -o intr_signal=NUM     signal to send on interrupt (10)\n"
           "    -o modules=M1[:M2...]  names of modules to push onto filesystem stack\n"
           "\n"
           "    -o max_write=N         set maximum size of write requests\n"
           "    -o max_readahead=N     set maximum readahead\n"
           "    -o max_background=N    set number of maximum background requests\n"
           "    -o congestion_threshold=N  set kernel's congestion threshold\n"
           "    -o async_read          perform reads asynchronously (default)\n"
           "    -o sync_read           perform reads synchronously\n"
           "    -o atomic_o_trunc      enable atomic open+truncate support\n"
           "    -o big_writes          enable larger than 4kB writes\n"
           "    -o no_remote_lock      disable remote file locking\n"
           "    -o no_remote_flock     disable remote file locking (BSD)\n"
           "    -o no_remote_posix_lock disable remove file locking (POSIX)\n"
           "    -o [no_]splice_write   use splice to write to the fuse device\n"
           "    -o [no_]splice_move    move data while splicing to the fuse device\n"
           "    -o [no_]splice_read    use splice to read from the fuse device\n"
           "\n"
           "Module options:\n"
           "\n"
           "[iconv]\n"
           "    -o from_code=CHARSET   original encoding of file names (default: UTF-8)\n"
           "    -o to_code=CHARSET      new encoding of the file names (default: UTF-8)\n"
           "\n"
           "[subdir]\n"
           "    -o subdir=DIR           prepend this directory to all paths (mandatory)\n"
           "    -o [no]rellinks         transform absolute symlinks to relative\n");
}