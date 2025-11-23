# Version Control Project (VCP) - A Git Mimic

A simple version control system that mimics Git. This project allows users to manage and track changes in their files and projects with features such as repository initialization, adding files, and submitting them to a remote server.

## Features (Sample)

- Initialize a new repository
- Add files for tracking
- Submit changes to a remote server
- Clone projects from the server
- List available projects

## Installation

1. Clone this repository:
   ```bash
   git clone https://github.com/halfbldprinc/VCP.git
   cd VCP
   ```

2. Build the project (C++17 or higher required):
   ```bash
   g++ VCP.cpp FTP.cpp -o vcp -std=c++17 $(pkg-config --cflags --libs openssl)
   g++ Server/VCPserver.cpp -o vcpserver -std=c++17
   ```
   
   If `pkg-config` is not available, you may need to specify OpenSSL include and library paths manually. For example, on macOS with Homebrew:
   ```bash
   g++ VCP.cpp FTP.cpp -o vcp -std=c++17 -I/opt/homebrew/opt/openssl@3/include -L/opt/homebrew/opt/openssl@3/lib -lssl -lcrypto
   ```
   On Linux, you may use:
   ```bash
   g++ VCP.cpp FTP.cpp -o vcp -std=c++17 -lssl -lcrypto
    ```

### manual build

#### On macOS (with Homebrew):
1. Install OpenSSL:
   ```bash
   brew install openssl@3
   ```
2. Build the project:
   ```bash
   g++ VCP.cpp FTP.cpp -o vcp -std=c++17 -I/opt/homebrew/opt/openssl@3/include -L/opt/homebrew/opt/openssl@3/lib -lssl -lcrypto
   g++ Server/VCPserver.cpp -o vcpserver -std=c++17
   ```

#### On Linux (Debian/Ubuntu):
1. Install OpenSSL development libraries:
   ```bash
   sudo apt-get update
   sudo apt-get install libssl-dev
   ```
2. Build the project:
   ```bash
   g++ VCP.cpp FTP.cpp -o vcp -std=c++17 -lssl -lcrypto
   g++ Server/VCPserver.cpp -o vcpserver -std=c++17
   ```
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

### 5. List Available Projects on Server
To see all projects available on the server, run:
```bash
./vcp list
```
This will display a list of all projects stored on the remote server.

### 6. Clone a Project from Server
To download and recreate a project from the server, run:
```bash
./vcp clone <project_name>
```
This will download all files from the specified project and create a local copy.

## Example Workflow

1. Start the server (in a separate terminal):
   ```bash
   ./vcpserver
   ```

2. Initialize the repository:
   ```bash
   ./vcp init
   my_project
   ```

3. Check the state of the repository:
   ```bash
   ./vcp state
   ```

4. Add a file:
   ```bash
   ./vcp add main.cpp
   ```

5. Submit changes to the server:
   ```bash
   ./vcp submit
   ```

6. List projects on server:
   ```bash
   ./vcp list
   ```

7. Clone a project from server:
   ```bash
   ./vcp clone my_project_20250725_1234
   ```

## Contributing

You are welcome to contribute! If you find a bug or have an idea for a feature, feel free to fork the repository and submit a pull request.

## License
This project is licensed under the BSD 3-Clause License.




