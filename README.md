# Version Control Project (VCP) - A Git Mimic

A simple version control system that mimics Git. This project allows users to manage and track changes in their files and projects with features such as repository initialization, adding files, and submitting them to a remote server.

## Features (Sample)

- Initialize a new repository
- Add files for tracking
- Submit changes to a remote server
- Clone projects from the server
- List available projects

## Installation

 (CMake):

```bash
mkdir -p build && cd build
cmake ..
cmake --build .
```

Single-file compile (pkg-config fallback):

```bash
g++ VCP.cpp FTP.cpp -o vcp -std=c++17 $(pkg-config --cflags --libs openssl)
g++ Server/server_main.cpp Server/protocol.cpp Server/storage.cpp Server/logging.cpp Server/auth.cpp -o vcpserver -std=c++17 $(pkg-config --cflags --libs sqlite3 openssl)
```

## Usage

- **Note:** The server (`vcpserver`) is intended to run on a separate machine or device. Build/compile `vcpserver` on the server device (or copy the binary there), start it, and ensure it is running before using client commands that communicate with the server (for example `submit`, `list`, or `clone`).

- Initialize a repository:

```bash
./vcp init <repo-name>
```

- Add a file:

```bash
./vcp add <file>
```

- Submit changes:

```bash
./vcp submit
```

- Run the server:

```bash
./vcpserver
```
Manual (Homebrew) macOS example:

```bash

g++ VCP.cpp FTP.cpp -o vcp -std=c++17 -I$(brew --prefix openssl@3)/include -L$(brew --prefix openssl@3)/lib -Wl,-rpath,$(brew --prefix openssl@3)/lib -lssl -lcrypto

g++ Server/server_main.cpp Server/protocol.cpp Server/storage.cpp Server/logging.cpp Server/auth.cpp -o vcpserver -std=c++17 -I$(brew --prefix openssl@3)/include -L$(brew --prefix openssl@3)/lib -Wl,-rpath,$(brew --prefix openssl@3)/lib -lssl -lcrypto -lsqlite3
```

## Contributing

You are welcome to contribute! If you find a bug or have an idea for a feature, feel free to fork the repository and submit a pull request.

## License
This project is licensed under the BSD 3-Clause License.

