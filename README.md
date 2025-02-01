

# Version Control Project (VCP) - A Git Mimic

A simple version control system that mimics Git. This project allows users to manage and track changes in their files and projects with features such as repository initialization, adding files, and submitting them to a remote server.

## Features

- **Initialize Repository**: Set up a new repository to start tracking files and changes.
- **Add Files**: Add files to the repository to begin tracking their changes.
- **Submit to Server**: Upload committed files to a remote server for secure storage and backup.

## Prerequisites

Before using the system, ensure that you have the following installed:

- **C++ Compiler** (with support for C++17 or higher).
- **Storage Service** Server for file submission, or you can use local storage for testing (server side codes included).
  

## Installation

1. Clone this repository:
   ```bash
   git clone https://github.com/halfbldprinc/VCP.git
   cd VCP
   ```

2. Build the project (C++ example):
   ```bash
   g++ VCP.cpp FTP.cpp -o vcp -std=c++17 -I/opt/homebrew/opt/openssl@3/include -L/opt/homebrew/opt/openssl@3/lib -lssl -lcrypto
   g++ VCPserver.cpp -o server -std=c++17
   ```

## Usage

### 1. Initialize a New Repository
To start a new project and initialize a repository, run:
```bash
./vcp init
 <repository_name>
```
This will create a new folder with the name `<repository_name>` and set it up for version tracking.

### 2. Check Tracked Files and Folders
To check the current state of tracked files and folders, run:
```bash
./vcp state
```
This will print a list of new and modified files and folders.

### 3. Add a File to the Repository
To add a file to the repository and begin tracking its changes, run:
```bash
./vcp add <file_name>
```
This will add the specified file to the version control system.

### 4. Submit Changes to Remote Server
To upload your changes to a remote server (e.g., Firebase or Google Cloud), run:
```bash
./vcp submit
```
This will push the tracked files to your configured cloud storage or local server.

## Example Workflow

1. Initialize the repository:
   ```bash
   ./vcp init
   my_project
   ```

2. Check the state of the repository:
   ```bash
   ./vcp state
   ```

3. Add a file:
   ```bash
   ./vcp add main.cpp
   ```

4. Submit changes to the server:
   ```bash
   ./vcp submit
   ```

## Contributing

You are welcome to contribute! If you find a bug or have an idea for a feature, feel free to fork the repository and submit a pull request.

## License
This project is licensed under the BSD 3-Clause License.




