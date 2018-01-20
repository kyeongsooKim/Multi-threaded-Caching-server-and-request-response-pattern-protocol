#include "cream.h"
#include "utils.h"
#include "queue.h"
#include <ctype.h> //isdigit
#include <string.h>
#include <stdio.h>
#include <sys/socket.h> //connect
#include <unistd.h> //read, write
#include <errno.h> //errno
#include <netdb.h> //struct addrinfo .. etc
#include <signal.h>

#define LISTENQ 1024 /* Second argument to listen() */

#define USAGE(prog_name)                                                       \
  do {                                                                         \
    fprintf(stderr,                                                            \
            "%s [-h] NUM_WORKERS PORT_NUMBERS MAX_ENTRIES \n"                  \
            "-h\t\t\tDisplay help menu\n" \
            "NUM_WORKERS\t\tThe number of worker threads used to service requests.\n"\
            "PORT_NUMBERS\t\tPort number to listen on for incoming connections.\n"\
            "MAX_ENTRIES\t\tThe maximum number of entries that can be stored in 'cream''s underlying data store.\n", \
            (prog_name));                                                      \
  } while (0)



hashmap_t *global_map;
queue_t * global_queue;

typedef struct sockaddr SA;


//The process received a SIGPIPE. The default behaviour for this signal is to end the process.
//A SIGPIPE is sent to a process if it tried to write to a socket that had been shutdown
//for writing or isn't connected (anymore).
//To avoid that the program ends in this case, you could either
//make the process ignore SIGPIPE or install an explicit handler for SIGPIPE (typically doing nothing).
//In both cases send*()/write() would return -1 and set errno to EPIPE.
void sigpipe_handler(int signo) {
    //do nothing.
}


void unix_error(char *msg) /* Unix-style error */
{
    fprintf(stderr, "%s: %s\n", msg, strerror(errno));
    exit(EXIT_FAILURE);
}


//rio_readn - Robustly read n bytes (unbuffered)
ssize_t rio_readn(int fd, void *usrbuf, size_t n)
{
    size_t nleft = n;
    ssize_t nread;
    char *bufp = usrbuf;

    while (nleft > 0) {
        if ((nread = read(fd, bufp, nleft)) < 0) {
            if (errno == EINTR) /* Interrupted by sig handler return */
                nread = 0;      /* and call read() again */
            else
                return -1; /* errno set by read() */

            if (errno == EPIPE)
            {
                close(fd);
                return -1;
            }

        } else if (nread == 0)
            break; /* EOF */
        nleft -= nread;
        bufp += nread;
    }
    return (n - nleft); /* return >= 0 */
}


//rio_writen - Robustly write n bytes (unbuffered)
ssize_t rio_writen(int fd, void *usrbuf, size_t n) {
    size_t nleft = n;
    ssize_t nwritten;
    char *bufp = usrbuf;

    while (nleft > 0) {
        if ((nwritten = write(fd, bufp, nleft)) <= 0) {
            if (errno == EINTR) /* Interrupted by sig handler return */
                nwritten = 0;   /* and call write() again */
            else
                return -1; /* errno set by write() */
            if (errno == EPIPE)
            {
                close(fd);
                return -1;
            }
        }
        nleft -= nwritten;
        bufp += nwritten;
    }
    return n;
}


/* Used in item destruction */
void sample_destructor(map_key_t key, map_val_t val) {
    #ifdef DEBUG
        printf("destroy (%s,%s)\n", (char*)key.key_base, (char*)val.val_base);
    #endif

    free(key.key_base);
    free(val.val_base);
}

bool isNumber(char number[])
{
    int i = 0;

    //checking for negative numbers
    if (number[0] == '-')
        i = 1;
    for (; number[i] != 0; i++)
    {
        //if (number[i] > '9' || number[i] < '0')
        if (!isdigit(number[i]))
            return false;
    }
    return true;
}



void service_util(int connfd)
{
    request_header_t request_header;
    response_header_t response_header;
    void * key_base = calloc(1, sizeof(map_key_t));
    void * value_base = calloc(1, sizeof(map_val_t));


    rio_readn(connfd, &request_header, sizeof(request_header));
    rio_readn(connfd, key_base, request_header.key_size);
    rio_readn(connfd, value_base, request_header.value_size);

    map_key_t key = MAP_KEY(key_base, request_header.key_size);
    map_val_t value = MAP_VAL(value_base, request_header.value_size);

    //handle PUT
    if(request_header.request_code == PUT)
    {


        //check if the client's request is valid by examining the key_size and value_size.
        if (request_header.key_size < MIN_KEY_SIZE || request_header.key_size > MAX_KEY_SIZE
            ||request_header.value_size < MIN_VALUE_SIZE || request_header.value_size > MAX_VALUE_SIZE)
        {
            response_header.response_code = BAD_REQUEST;
            response_header.value_size = 0;
        }


        #ifdef DEBUG
            printf("receive PUT request with (key,value) : (%s,%s)\n", (char *)key.key_base,(char *)value.val_base);
        #endif

        if (put(global_map, key, value, true) == true)
        {
            response_header.response_code = OK;
        }
        else
        {
            response_header.response_code = BAD_REQUEST;
            response_header.value_size = 0;
        }


    }
    //handle GET
    else if(request_header.request_code == GET)
    {
        #ifdef DEBUG
            printf("receive GET request with key %s\n", (char *)key.key_base);
        #endif


        //check if the client's request is valid by examining the key_size and value_size.
        if (request_header.key_size < MIN_KEY_SIZE || request_header.key_size > MAX_KEY_SIZE)
        {
            response_header.response_code = BAD_REQUEST;
            response_header.value_size = 0;
        }

        value = get(global_map, key);
        #ifdef DEBUG
            printf("retrieved val from key %s is %s\n",(char*)key.key_base, (char*)value.val_base);
        #endif

        if (value.val_len == 0)
        {
            response_header.response_code = NOT_FOUND;
            response_header.value_size = 0;
        }
        else
        {
            response_header.response_code = OK;
            response_header.value_size = value.val_len;
        }


    }
    //handle EVICT
    else if(request_header.request_code == EVICT)
    {
        #ifdef DEBUG
            printf("receive EVICT request\n");
        #endif


        //check if the client's request is valid by examining the key_size and value_size.
        if (request_header.key_size < MIN_KEY_SIZE || request_header.key_size > MAX_KEY_SIZE)
        {
            response_header.response_code = BAD_REQUEST;
            response_header.value_size = 0;
        }


        map_node_t node = delete(global_map, key);

        if ( node.key.key_len == 0 && node.val.val_len == 0 )
        {
              //do nothing
        }
        else
        {
            for (int i = 0; i < global_map->capacity;i++)
            {
                if (global_map->nodes[i].key.key_base == node.key.key_base)
                {
                    global_map->destroy_function(global_map->nodes[i].key, global_map->nodes[i].val);
                    global_map->nodes[i].key.key_base = NULL;
                    global_map->nodes[i].val.val_base = NULL;
                    global_map->nodes[i].key.key_len = 0;
                    global_map->nodes[i].val.val_len = 0;
                }
            }

        }
        //once the EVICT operation has completed the server will send a response message back
        // to the client with a response_code of OK and value_size of 0
        response_header.response_code = OK;
        response_header.value_size = 0;
    }
    //handle CLEAR
    else if(request_header.request_code == CLEAR)
    {

        #ifdef DEBUG
            printf("receive CLEAR request\n");
        #endif

        if (clear_map(global_map))
        {
            response_header.response_code = OK;
            response_header.value_size = 0;
        }
        else
        {
            response_header.response_code = BAD_REQUEST;
            response_header.value_size = 0;
        }
    }
    else
    {
        response_header.response_code = UNSUPPORTED;
        response_header.value_size = 0;
    }

    // sending a response to the client
    rio_writen(connfd, &response_header, sizeof(response_header));
    rio_writen(connfd, value.val_base, response_header.value_size);
}


void * service()
{
    pthread_detach(pthread_self());

    while(1)
    {
        int connfd = *((int *)dequeue(global_queue)); //remove connfd from queue
        service_util(connfd); //service client
        close(connfd);
    }
}


//the server creates a listening descriptor that is ready to receive connection requests
//by calling the open_listenfd function.
//it returns listening descriptor that is ready to receive connection requests on port port.
int open_listenfd(char *port) {
    struct addrinfo hints, *listp, *p;
    int listenfd, optval = 1;

    /* Get a list of potential server addresses */
    memset(&hints, 0, sizeof(struct addrinfo));
    hints.ai_socktype = SOCK_STREAM;  /* Accept TCP connections */
    hints.ai_flags = AI_PASSIVE;      /* ... on any IP address */
    hints.ai_flags |= AI_NUMERICSERV; /* ... using a numeric port arg. */
    hints.ai_flags |= AI_ADDRCONFIG;  /* Recommended for connections */
    getaddrinfo(NULL, port, &hints, &listp);

    /* Walk the list for one that we can bind to */
    for (p = listp; p; p = p->ai_next) {
        /* Create a socket descriptor */
        if ((listenfd = socket(p->ai_family, p->ai_socktype, p->ai_protocol)) <
            0)
            continue; /* Socket failed, try the next */

        /* Eliminates "Address already in use" error from bind */
        setsockopt(listenfd, SOL_SOCKET, SO_REUSEADDR, (const void *)&optval,
                   sizeof(int));

        /* Bind the descriptor to the address */
        if (bind(listenfd, p->ai_addr, p->ai_addrlen) == 0)
            break;       /* Success */
        close(listenfd); /* Bind failed, try the next */
    }

    /* Clean up */
    freeaddrinfo(listp);
    if (!p) /* No address worked */
        return -1;

    /* Make it a listening socket ready to accept connection requests */
    if (listen(listenfd, LISTENQ) < 0){
        close(listenfd);
        return -1;
    }
    return listenfd;
}



int main(int argc, char *argv[]) {

    int NUM_WORKERS;
    char * PORT_NUMBERS;
    int MAX_ENTRIES;

    if (argc == 1)
    {
        USAGE(argv[0]);
        exit(EXIT_FAILURE);
    }
    else if(strcmp(argv[1],"-h") == 0)
    {
        USAGE(argv[0]);
        exit(EXIT_SUCCESS);
    }
    else if(argc == 4 && strcmp(argv[1],"-h") != 0)
    {
        for(int i = 1; i < argc;i++)
        {
            if(!isNumber(argv[i]))
            {
                USAGE(argv[0]);
                exit(EXIT_FAILURE);
            }
        }

        NUM_WORKERS = atoi(argv[1]);
        MAX_ENTRIES = atoi(argv[3]);
        PORT_NUMBERS = argv[2];

    }
    else
    {
        USAGE(argv[0]);
        exit(EXIT_FAILURE);
    }

    //handling external errors such as connections getting closed,
    //client programs getting killed, and blocking syscall beng interrupted.
    if (signal(SIGPIPE, sigpipe_handler) == SIG_ERR) {
        unix_error("An error occurred while setting a signal handler.\n");
        return EXIT_FAILURE;
    }

    int listenfd, connfd;
    socklen_t clientlen;
    struct sockaddr_storage clientaddr;

    //bind a socket to the port specified by PORT_NUMBER
    listenfd = open_listenfd(PORT_NUMBERS);


    //initialization. the request queue is an instance of queue_t.
    //underlying data store is an instance of hashmap_with capacity MAX_ENTRIES
    global_map = create_map(MAX_ENTRIES, jenkins_one_at_a_time_hash, sample_destructor);
    global_queue = create_queue();



    pthread_t tid;

    // On startup, cream spawn NUM_WORKERS worker threads for the lifetime of the program.
    // each thread for each new client. The server consists of a main thread and a set of
    // worker threads. the main thread repeatedly accepts connection requests from clients
    // and places the resulting connected descriptors in a bounded buffer.
    for(int index = 0; index < NUM_WORKERS; index++) {
        if(pthread_create(&tid, NULL, service, NULL) != 0)
        {
            exit(EXIT_FAILURE);
        }
    }

    // infinite server loop, accepting connection requests and inserting the resulting
    // connected descriptors in queue
    //infinitely listen on the bound socket for incoming connections.
    while (1)
    {
        clientlen = sizeof(struct sockaddr_storage);



        //clients' connection request will be accepted in cream's main thread.
        connfd = accept(listenfd, (SA *) &clientaddr, &clientlen);

        //after accepting the client's connection, main thread adds the accepted socket
        //to a request queue so taht a blocked worker thread is unblocked to service
        //the client's request.
        enqueue(global_queue, &connfd) ;//insert connfd in queue
    }

    exit(0);
}
