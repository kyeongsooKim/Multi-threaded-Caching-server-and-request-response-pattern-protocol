# Multi-threaded LRU Caching server and request-response pattern protocol development
### Kyeongsoo Kim, October 2017 ~ January 2018

## Introduction
This project is academic project for becoming familiar with low-level POSIX threads, multi-threading safety, concurrency guarantees, and networking. The project is originally designed by Prof. Jennifer in Stony Brook University, but all the actual code except base code is made by me.

In this project, First of all, I implemented a concurrent queue using the producers/consumers locking pattern and a concurrent hash map using the readers/writers locking pattern. Then , built an in-memory, multi-threaded, caching server similar to [__Memcached__](https://memcached.org/), through those data structures I made. 
Lastly I built simple protocol using this server. The entire program is multi-threading safe and can handle all the external errors such as connections getting closed, client programs getting killed, and a blocking syscall being interrupted.

,which means 
the program handles:
* EPIPE
* SIGPIPE
* EINTR


## USAGE

```
./cream [-h] NUM_WORKERS PORT_NUMBER MAX_ENTRIES
-h                 Displays this help menu and returns EXIT_SUCCESS.
NUM_WORKERS        The number of worker threads used to service requests.
PORT_NUMBER        Port number to listen on for incoming connections.
MAX_ENTRIES        The maximum number of entries that can be stored in `cream`'s underlying data store.
```

## File Structure


```
repo
├── Makefile
├── Makefile.config
├── include
│   ├── const.h
│   ├── cream.h
│   ├── debug.h
│   ├── extracredit.h
│   ├── hashmap.h
│   ├── queue.h
│   └── utils.h
├── src
│   ├── cream.c
│   ├── extracredit.c
│   ├── hashmap.c
│   ├── queue.c
│   └── utils.c
└── tests
    ├── extracredit_tests.c
    ├── hashmap_tests.c
    └── queue_tests.c
```




## Part I: Concurrent Queue

A queue is a first-in first-out (FIFO) linear data structure where elements are inserted at one end (the rear), and are removed from the other end (the front). I made a concurrent, blocking queue that follows the **producers/consumers** pattern. The queue is implemented using singly linked list.
The `queue_t` struct provided in `queue.h` includes a mutex and a semaphore for the producers/consumers pattern.

> My queue support two basic operations, `enqueue` and `dequeue`, which insert elements at the rear of the queue and remove elements from the front of the queue, respectively.

> All of the functions is multi-threading safe. Any number of threads is able to call these functions without any data corruption.


### Operations
- `queue_t *create_queue(void);`
    - This function `calloc(3)`s a new instance of `queue_t` and initializes all locks and semaphores in the `queue_t` struct.
    - *Returns:* A valid pointer to an initialized `queue_t` instance or `NULL`.

- `bool invalidate_queue(queue_t *self, item_destructor_f destroy_function);`
    - This function will invalidate the `queue_t` instance pointed to by `self`.
    - It will call `destroy_function` on all remaining items in the queue and `free(3)` the `queue_node_t` instances.
    - It will set the `invalid` flag in `self` to true to indicate that the queue is not usable.
    - *Returns:* `true` if the invalidation was successful, `false` otherwise.

- `bool enqueue(queue_t *self, void *item);`
    - This function `calloc(3)`s a new `queue_node_t` instance to add to the queue.
    - *Returns:* `true` if the operation was successful, `false` otherwise.

- `void *dequeue(queue_t *self);`
    - Removes the item at the front of the queue pointed to by `self`.
    - This function blocks until an item is available to dequeue.
    - *Returns:* A pointer to the item stored at the front of the queue.


## Part II: Concurrent Hash map

A hash map is a generic data structure that allows for insertion, searching, and deletion in expected constant time.
Hash maps store key-value pairs. Each key is unique, but values can be repeated.
I implement an [__open-addressed hash map__](http://www.algolist.net/Data_structures/Hash_table/Open_addressing) backed by an array that uses linear probing to deal with collisions.
> My hashmap supports the `put`, `get`, and `remove` operations.

> It computes the index for a given key using the formula `index = hash(key) % table_capacity`, where `hash()` is a hashing function that returns an unsigned integer. To insert, the map tries to put the key/value pair at the computed index.

> Linear probing is used to deal with hash collisions (i.e. when two or more keys hash to the same index).
In this case, the map will search larger indexes sequentially, wrapping around the array if necessary, until an empty slot is found. The new entry will be inserted at this empty slot.

> Searching for a value given a key is similar. First, the starting index is computed using the previous formula.
The map starts looking for the key at the computed index and continues searching sequentially through the map until the key is found, it gets back to the original index, or an empty slot is found. In the latter two situations, the map will conclude that the key is not present.

> It is incorrect to just remove the key/value pair from the array, leaving behind an empty slot. So my map sets a special tombstone flag at every deleted index. When searching, the map can skip over a tombstone and continue searching at the next index. When inserting, the map can treat the tombstone as an empty slot and insert a new key-value pair.


> It follows Least Recently Used (LRU) replacement policy. When the map is full and a `put` call is made with the `force` parameter set to `true`, so that the least recently used node is overwritten. If the map is full and `force` is set to `false`, set `errno` to `ENOMEM` and return `false` as usual. Both `put` and `get` operations count towards a node being recently used.


> All operations on the hash map is multi-threading safe. This will allow multiple threads to access the map concurrently without data corruption.

> Using the locks and `num_readers` variable in the `hashmap_t` struct, it follows the readers/writers pattern.


Here is a diagram to visualize the `hashmap_t` struct:

![hashmap_diagram](diagrams/hashmap_diagram.png)


### Operations
- `hashmap_t *create_map(uint32_t capacity, hash_func_f hash_function, destructor_f destroy_function)`
    - This will `calloc(3)` a new instance of `hashmap_t` that manages an array of `capacity` `map_node_t` instances.
    - *Returns:*  A valid pointer to a `hashmap_t` instance, or `NULL`.

- `bool put(hashmap_t *self, map_key_t key, map_val_t val, bool force)`
    - This will insert a key/value pair into the hash map pointed to by `self`.
    - If the key already exists in the map, update the value associated with it and return `true`.
    - If the map is full and `force` is `true`, overwrite the entry at the index given by `get_index` and return `true`.
    - *Returns:* `true` if the operation was successful, `false` otherwise.

    

- `map_val_t get(hashmap_t *self, map_key_t key)`
    - Retrieves the `map_val_t` corresponding to `key`.
    - *Returns:* The corresponding value.
      If `key` is not found in the map, the `map_val_t` instance will contain a `NULL` pointer and a `val_len` of 0.

- `map_node_t delete(hashmap_t *self, map_key_t key)`
    - Removes the entry with key `key`.
    - *Returns:* The removed `map_node_t` instance

- `bool clear_map(hashmap_t *self)`
    - Clears all remaining entries in the map.
    - It will call the `destroy_function` in `self` on every remaining item.
    - It doesn't free any pointers or destroy any locks in `self`.
    - *Returns:* `true` if the operation was successful, `false` otherwise.


- `bool invalidate_map(hashmap_t *self)`
    - This will invalidate the `hashmap_t` instance pointed to by `self`.
    - It will call the `destroy_function` in `self` on every remaining item.
    - It doesn't free `self` or destroy any locks in `self`.
    - *Returns:* `true` if the invalidation was successful, `false` otherwise.



## Part III : Multi-threaded LRU Caching server

The server that I implemented in this project (named "CREAM") is for the general purpose in-memory key-value caching service through concurrent data structures implemented in the previous partI and II.


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

![Server Diagram](./diagrams/server.png)

The diagram above shows how `cream` handles client requests.
On startup `cream` will spawn `NUM_WORKERS` worker threads for the lifetime of the program, bind a socket to the port specified by `PORT_NUMBER`, and infinitely listen on the bound socket for incoming connections.
Clients will attempt to establish a connection with `cream` which will be accepted in `cream`'s main thread.
After accepting the client's connection `cream`'s main thread adds the accepted socket to a **request queue** so that a blocked worker thread is unblocked to service the client's request.
Once the worker thread has serviced the request it will send a response to the client, close the connection, and block until it has to service another request.


### Personal Protocol
For better usuage, I implemented simple protocol over a streaming socket using the request-response pattern. It is a very basic protocol as the client will send only one message (a request) to the server, and the server will send only one message (a response) back to the client. Both messages (request and response) have a unique header which is prepended to the message's body. The fields in the request and response headers are used to denote the type and content of the message being sent.
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