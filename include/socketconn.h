#ifndef SOCKETCONN_H
#define SOCKETCONN_H

#include <sys/un.h>

#define DEFAULT_PORT 7000
#define DEFAULT_TIMEOUT 8000    // Massimo tempo, espresso in millisecondi, per avviare una connessione socket
#define CONNECT_INTERVAL 1000   // Ogni quanti millisecondi riprovare la connect se fallisce
#define UNIX_PATH_MAX 108
#define BASESOCKNAME "/tmp/sockfile"

/**
 * Crea un socket AF_UNIX ed esegue il binding del socket e la chiamata di sistema listen() sul socket.
 *
 * @return file descriptor del socket creato, -1 in caso di errore e imposta errno
 */
int socket_listen(int port);

/**
 * Accetta una connessione sul socket passato per argomento. Ritorna il file descriptor del client che ha accettato
 * la connessione. Se scade il timeout allora la funzione ritorna -1 ed errno viene impostato a ETIMEDOUT.
 *
 * @param fd_skt file descriptor sul quale accettare la connessione
 * @param timeout tempo massimo, espresso in millisecondi, per instaurare una connessione. Se negativo viene utilizzato
 * tempo di default DEFAULT_TIMEOUT
 * @return file descriptor del client con il quale è iniziata la connessione, -1 in caso di errore e imposta errno
 */
int socket_accept(int fd_skt, long timeout);

/** TODO this doc
 * Cerca di connettersi via socket AF_UNIX. Il tentativo di connessione viene svolto ad intervalli di CONNECT_INTERVAL
 * millisecondi. Ritorna il file descriptor da utilizzare per la comunicazione con il server oppure -1 in caso di errore
 * ed imposta errno. Se scade il timeout allora la funzione ritorna -1 ed errno viene impostato a ETIMEDOUT.
 *
 * @param timeout tempo massimo, espresso in millisecondi, per instaurare una connessione. Se negativo viene utilizzato
 * tempo di default DEFAULT_TIMEOUT
 *
 * @return il file descriptor per comunicare con il server oppure -1 in caso di errore ed imposta errno
 */
int socket_connect_interval(int fd_skt, struct sockaddr_un sa, long *timeout);

/** TODO this doc
 * Cerca di connettersi via socket AF_UNIX. Il tentativo di connessione viene svolto ad intervalli di CONNECT_INTERVAL
 * millisecondi. Ritorna il file descriptor da utilizzare per la comunicazione con il server oppure -1 in caso di errore
 * ed imposta errno. Se scade il timeout allora la funzione ritorna -1 ed errno viene impostato a ETIMEDOUT.
 *
 * @param timeout tempo massimo, espresso in millisecondi, per instaurare una connessione. Se negativo viene utilizzato
 * tempo di default DEFAULT_TIMEOUT
 *
 * @return il file descriptor per comunicare con il server oppure -1 in caso di errore ed imposta errno
 */
int socket_double_connect(int fdconnect, int fdaccept, struct sockaddr_un conn_sa, struct sockaddr_un acc_sa, long timeout);

/**
 * Invia i dati passati per argomento attraverso il file descriptor fornito. I dati vengono preceduti da un unsigned
 * integer che rappresenta la dimensione in bytes dei dati.
 *
 * @param fd_skt file descriptor sul quale scrivere i dati
 * @param data dati da inviare
 * @param size quanti bytes inviare
 *
 * @return numero di bytes scritti in caso di successo, -1 altrimenti ed imposta errno, 0 se il socket è stato chiudo
 */
int socket_write_h(int fd_skt, void *data, size_t size);

/**
 * Legge dal socket i dati attraverso il file descriptor fornito, allocando abbastanza memoria. ptr punterà all'aria di
 * memoria allocata ed il chiamante deve occuparsi di liberarla quando non gli serve più per evitare memory leaks.
 *
 * @param fd_skt file descriptor sul quale scrivere i dati
 * @param ptr puntatore all'area di memoria allocata che contiene i dati letti
 * @return numero di bytes letti in caso di successo, -1 altrimenti ed imposta errno oppure 0 se il socket è chiuso
 */
int socket_read_h(int fd_skt, void **ptr);

/**
 * Unlink the file used for the socket communication
 *
 * @return 0 on success, -1 otherwise and errno is set
 */
int socket_destroy(int fd, int port);

#endif //SOCKETCONN_H
