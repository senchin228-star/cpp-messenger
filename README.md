# Wiretalk

**A C++ client-server messaging system built with TCP sockets and a custom JSON-based protocol.**

Wiretalk is an individual school project focused on low-level network programming, client-server architecture, and the practical implementation of a messaging system in C++.

The project is being developed as part of a school individual project.

## Project Goals

* Understand TCP communication and socket programming.
* Design and implement a custom application-layer protocol.
* Build a client-server architecture in C++.
* Exchange structured messages using JSON.
* Explore the transmission of images and other binary data.
* Learn about connection handling, message framing, and reliable data exchange.

## Planned Features

* [ ] Basic TCP client-server communication
* [ ] JSON-based text message serialization
* [ ] Basic message type field
* [ ] Multiple simultaneous clients
* [ ] User authentication
* [ ] Message routing between users
* [ ] Image transmission
* [ ] Message history
* [ ] Secure communication

## Technology Stack

* **C++** — core implementation
* **TCP/IP sockets** — network communication
* **JSON** — message serialization
* **nlohmann/json** — JSON for Modern C++

## Protocol

The initial protocol uses JSON objects to represent application messages.

Example:

```json
{
  "type": "text",
  "message": "Hello, world!"
}
```

The `type` field identifies the kind of message, allowing the protocol to be extended with additional message types in the future.

The current prototype uses newline-delimited JSON messages over a TCP connection.

## Project Structure

The project is currently in its early development stage. Its structure and protocol may change as new features are implemented.

## Building and Running

Build instructions will be added as the project develops and its dependencies and build system are finalized.

## Academic Context

Wiretalk is being developed as an individual school project focused on the design, development, and practical implementation of a multi-user text messaging system using client-server architecture, low-level network programming in C++, and TCP/IP.

## Status

**Early development — protocol and networking prototype.**

This project is primarily intended for educational purposes. Its architecture and features are subject to change.

