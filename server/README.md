# Multi-threaded LRU Caching server and request-response pattern protocol development
### Kyeongsoo Kim, October 2017 ~ January 2018

## Introduction
This project is academic project for becoming familiar with low-level POSIX threads, multi-threading safety, concurrency guarantees, and networking. The project is originally designed by Prof. Jennifer in Stony Brook University, but all the actual code except base code is made by me.

In this project, First of all, I implemented a concurrent queue using the producers/consumers locking pattern and a concurrent hash map using the readers/writers locking pattern. Then , built an in-memory, multi-threaded, caching server similar to [__Memcached__](https://memcached.org/), through those data structures I made. 
Lastly I built request-response pattern protocol using this server. The entire program is multi-threading safe and can handle all the external errors such as connections getting closed, client programs getting killed, and a blocking syscall being interrupted.

,which means 
the program handles:
* EPIPE
* SIGPIPE
* EINTR


## USAGE

First compile the server with `make clean all`.

```
./cream [-h] NUM_WORKERS PORT_NUMBER MAX_ENTRIES
-h                 Displays this help menu and returns EXIT_SUCCESS.
NUM_WORKERS        The number of worker threads used to service requests.
PORT_NUMBER        Port number to listen on for incoming connections.
MAX_ENTRIES        The maximum number of entries that can be stored in `cream`'s underlying data store.
```
> To test client side, check README.md in "client" directory.



## Part I: Concurrent Queue

A queue is a first-in first-out (FIFO) linear data structure where elements are inserted at one end (the rear), and are removed from the other end (the front). I made a concurrent, blocking queue that follows the **producers/consumers** pattern. The queue is implemented using singly linked list.
The `queue_t` struct provided in `queue.h` includes a mutex and a semaphore for the producers/consumers pattern.

> My queue support two basic operations, `enqueue` and `dequeue`, which insert elements at the rear of the queue and remove elements from the front of the queue, respectively.

> All of the functions is multi-threading safe. Any number of threads is able to call these functions without any data corruption.



## Part II: Concurrent Hash map


I implement an [__open-addressed hash map__](http://www.algolist.net/Data_structures/Hash_table/Open_addressing) backed by an array that uses linear probing to deal with collisions. It supports the `put`, `get`, and `remove` operations and follows the readers/writers pattern by using the locks and `num_readers` variable in the `hashmap_t` struct.

> My hashmap sets a special tombstone flag at every deleted index. When searching, the map can skip over a tombstone and continue searching at the next index. When inserting, the map can treat the tombstone as an empty slot and insert a new key-value pair.


> It follows Least Recently Used (LRU) replacement policy. 

> All operations on the hash map is multi-threading safe. This will allow multiple threads to access the map concurrently without data corruption.




## Part III : Multi-threaded LRU Caching server

The server that I implemented in this project (named "CREAM") is for the general purpose in-memory key-value caching service through concurrent data structures implemented in the previous partI and II.

![](server_diagram.png)


### USAGE

```
./cream [-h] NUM_WORKERS PORT_NUMBER MAX_ENTRIES
-h                 Displays this help menu and returns EXIT_SUCCESS.
NUM_WORKERS        The number of worker threads used to service requests.
PORT_NUMBER        Port number to listen on for incoming connections.
MAX_ENTRIES        The maximum number of entries that can be stored in `cream`'s underlying data store.
```

`cream` will service a request for any client that connects to it.

`cream` will service **ONE** request per connection, and will terminate any connection after it has fulfilled and replied to its request.

On startup `cream` will spawn `NUM_WORKERS` worker threads for the lifetime of the program, bind a socket to the port specified by `PORT_NUMBER`, and infinitely listen on the bound socket for incoming connections.
Clients will attempt to establish a connection with `cream` which will be accepted in `cream`'s main thread.
After accepting the client's connection `cream`'s main thread adds the accepted socket to a **request queue** so that a blocked worker thread is unblocked to service the client's request.
Once the worker thread has serviced the request it will send a response to the client, close the connection, and block until it has to service another request.


## Part IV: Personal Protocol
For better usage, I implemented simple protocol over a streaming socket using the request-response pattern. It is a very basic protocol as the client will send only one message (a request) to the server, and the server will send only one message (a response) back to the client. Both messages (request and response) have a unique header which is prepended to the message's body. The fields in the request and response headers are used to denote the type and content of the message being sent.
Without the use of a message header, the receiver of the message has no idea what it is receiving.

> Request–response is a message exchange pattern in which a requester sends a  message to a responder system which receives, processes, and responds to the request. This is a simple, but powerful messaging pattern which allows two applications to have a conversation with one another over a connection.

#### Request Header
`request_header_t` struct located in `cream.h`.

```C
typedef struct request_header_t
{
    uint8_t request_code;
    uint32_t key_size;
    uint32_t value_size;
} __attribute__((packed)) request_header_t;
```
`request_codes` enum in `cream.h` consisting of all requests and their corresponding `request_code`.

```C
typedef enum request_codes {
    PUT = 0x01,
    GET = 0x02,
    EVICT = 0x04,
    CLEAR = 0x08
} request_codes;
```

When reading from the client's connected socket the server first reads and examines the `request_header`.
The server will be able to determine what the client's request is based on the `request_code`, and the structure of the payload based on the `key_size` and `value_size`.
The server checks if the client's request is valid by examining the `key_size` and `value_size` fields, and checking if they fall within the appropriate ranges.


#### Response Header
`response_header_t` struct located in `cream.h`.

```C
typedef struct response_header_t {
    uint32_t response_code;
    uint32_t value_size;
} __attribute__((packed)) response_header_t;
```
`response_codes` enum in `cream.h` consisting of all responses and their corresponding `response_code`.

```C
typedef enum response_codes {
    OK = 200,
    UNSUPPORTED = 220,
    BAD_REQUEST = 400,
    NOT_FOUND = 404
} response_codes;
```

When sending a response to the client, the server makes sure that the `response_header` is at the beginning of the message.
The client will be able to determine what the server's response is based on the `response_code`, and the structure of the payload based on the `value_size` field.


#### Hash Function

the hash function will be used by the server to hash keys when inserting, retrieving, and deleting values from the underlying data store.

#### Put Request

When a client wants to insert a value into the cache it will connect to the server and send a request message with a `request_code` of `PUT`.
The server checks if the `key_size` and `value_size` fields in the `request_header` are valid, and then attempts to read the key and value from the connected socket.

For a `PUT` request the first `key_size` bytes after the `request_header` correspond to the key, and the the next `value_size` bytes after the key correspond to the value.

If the cache is full, the `PUT` operation will evict a value from the cache by overwriting the entry in the hash map at the index that the key hashes to.

After the `PUT` operation has completed the server will send a response message back to the client informing them of the status of their request.
The `response_code` in the header of the response message will be set to `OK` if the operation was completed successfully, or `BAD_REQUEST` if an error occurred, and `value_size` will be set to 0.


#### Get Request

When a client wants to retrieve a value from the cache it will connect to the server and send a request message with a `request_code` of `GET`.
The server checks if the `key_size` field in the `request_header` is valid, and then attempts to read the key from the connected socket.

For a `GET` request the first `key_size` bytes after the `request_header` correspond to the key.

If the value that the client requested exists, the server will send a response message back to the client containing the corresponding value.
In the message, the `response_code` will be set to `OK`, the `value_size` will be set to the size of the value in bytes, and the first `value_size` bytes after the header will be the corresponding value.

If the value that the client requested does not exist, the server will still send a response message back to client.
In this scenario the `response_code` will be set to `NOT_FOUND`, and `value_size` will be set to 0.

#### Evict Request

When a client wants to delete a value from the cache it will connect to the server and send a request message with a `request_code` of `EVICT`.
The server checks if the `key_size` field in the `request_header` is valid, and then attempts to read the key from the connected socket.

Once the `EVICT` operation has completed, regardless of the outcome of this operation, the server will send a response message back to the client with a `response_code` of `OK` and `value_size` of 0.


#### Clear Request

When a client wants to clear all values from the cache it will connect to the server and send a request message with a request code of `CLEAR`.

Once the `CLEAR` operation has completed the server will send a response message back to the client with a `response_code` of `OK` and `value_size` of 0.

#### Invalid Request

If a client sends a message to the server, and the `request_code` is not set to any of the values in the `request_codes` enum, the server will send a response message back to the client with a `response_code` of `UNSUPPORTED` and `value_size` of 0.