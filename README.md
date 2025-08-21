A high-performance key-value server written in C/C++, designed to handle a variety of data types and client requests efficiently. It uses a non-blocking, single-threaded event loop and leverages several advanced data structures and optimizations to achieve low latency and high concurrency.

-----

### Build Instructions

To build and install the project, you need a C++ compiler like g++.

1.  Clone the repository or download the source code.
2.  Navigate to the project's root directory in your terminal.
3.  Run `make` to compile the server binary, which produces the executable `server`.
4.  To clean up the build files (object files and executables), you can run the `make clean` command.


-----

### Usage

The server listens on **port 1234** by default.

To start the server, run the `server` executable from your terminal:

```
./server
```
-----

### Key Features and Implementations

The server is built on a foundation of robust data structures and architectural patterns, including:

  * **Pipelining:** The server can process multiple client requests sent in a single batch, allowing for efficient communication and reduced round-trip latency.
  * **Non-Blocking Event Loop:** The core of the server's architecture is a non-blocking event loop managed by `poll()`. This design allows the server to handle thousands of concurrent connections without creating a separate thread for each client, maximizing resource utilization.
  * **Thread Pool:** Time-consuming operations, such as the deletion of large data containers, are offloaded to a dedicated **thread pool**. This prevents long-running tasks from blocking the main event loop, ensuring the server remains responsive.
  * **Sorted Set with AVL Trees:** The `zset` data type is implemented using a combination of a hash map for fast key lookups and an **AVL tree** to maintain the sorted order of elements based on their score.
  * **TTL Cache and Heap:** The server includes a Time-To-Live (TTL) cache expiration mechanism. Expirations are managed efficiently using a **min-heap**, which allows the server to quickly identify and remove the next expiring entry with minimal overhead.
  * **Intrusive Nodes:** For managing active and idle connections, the project uses **intrusive nodes** (`dlist`), which are linked directly within the connection (`Conn`) object. This avoids separate memory allocations for the list nodes, reducing memory overhead and improving performance.

-----

### Supported Commands

The server handles a set of commands, which are processed in the `do_request` function:

  * `get <key>`: Retrieves the value of a string key.
  * `set <key> <value>`: Sets the string value of a key.
  * `del <key>`: Deletes a key and its associated value.
  * `pexpire <key> <ttl_ms>`: Sets the Time-To-Live for a key in milliseconds.
  * `pttl <key>`: Returns the remaining Time-To-Live for a key in milliseconds.
  * `keys`: Returns a list of all keys in the database.
  * `zadd <key> <score> <name>`: Adds a member with a given score to a sorted set.
  * `zrem <key> <name>`: Removes a member from a sorted set.
  * `zscore <key> <name>`: Gets the score of a member in a sorted set.
  * `zquery <key> <score> <name> <offset> <limit>`: Queries a sorted set for a range of members.