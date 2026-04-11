# LSP proxy for Microsoft C/C++ language server

A minimal C implementation of an LSP (Language Server Protocol) proxy that intercepts JSON-RPC `initialize` messages from the Microsoft C/C++ language server and replaces the `clientInfo.name` field with "Visual Studio Code" to allow VSCode forks (like VSCodium) to use the language server without crashing the server.

## Purpose

Ever since Microsoft tightened the official C/C++ language server [at the LSP level to not allow VSCode forks](https://github.com/VSCodium/vscodium/issues/2300) people could no longer use the official C/C++ extension on VSCode forks. This [obhiously got people angry](https://github.com/VSCodium/vscodium/issues/2300#issuecomment-3315030963) and had to use alternatives such as [Clangd](https://clangd.llvm.org), luckily you don't need to find alternatives anymore (assuming your architecture is supported by extension) thanks to this proxy.

## Building

```bash
make
```

This compiles the C program with warnings enabled and standard optimizations.

## Usage

The proxy is designed to be inserted in the communication pipeline between an LSP client (VSCodium) and server (`cpptools`). It reads LSP protocol messages from stdin and writes modified messages to stdout.

### Install

First install the Microsoft C/C++ language server in your VSCodium install, then to install the binary, find where is your `vscode-cpptools` package:
```bash
# On Windows, the extensions path is in C:\Users\<user>\.vscode\extensions
# On Mac OS, the extensions path is in /Users/<user>/.vscode/extensions
# On Linux, the extensions path is in /home/<user>/.vscode/extensions
$ cd ~/.vscode/extensions
$ ls | grep cpptools
ms-vscode.cpptools-1.31.4-linux-x64
ms-vscode.cpptools-extension-pack-1.5.1
ms-vscode.cpptools-themes-2.0.0
# the folder with your OS name is the one that the binary will be installed
$ cd ms-vscode.cpptools-1.31.4-linux-x64/bin
$ mv cpptools cpptools-orig
# download the cpptools-proxy[.exe] binary from the Releases tab or compile it yourself, see instructions above
$ cp ~/Downloads/cpptools-proxy cpptools # replace ~/Downloads/cpptools-proxy with your real path to the compiled build and add .exe if on Windows like here
#$ cp ~/cpptools-proxy.exe cpptools.exe

```

### Install with custom binary path to cpptools/cpptools-proxy binary

The steps are similar as above, except the last step where instead of copying the cpptools proxy binaries as usual, to this:
```bash
# set custom binary path to cpptools/cpptools-proxy
export cpptoolsproxypath=$(realpath /path/to/cpptools/proxy)
export cpptoolspath=$(realpath /path/to/cpptools)

cat <<EOF > cpptools
#!/usr/bin/env bash
$cpptoolsproxypath --path $cpptoolspath
EOF
chmod +x cpptools
```


### How does this work?

Until version v1.24.5, the language server didn't check if the `clientInfo` portion to see if the `name` related to Visual Studio products (such as Visual Studio Code), after v1.24.5 the language server started getting stricter by checking the `clientInfo` portion to see if the `name` contained the exact word "Visual Studio Code", and if it did not then the language server prints out
```
The C/C++ extension may be used only with Microsoft Visual Studio, Visual Studio for Mac, Visual Studio Code, Azure DevOps, Team Foundation Server, and successor Microsoft products and services to develop and test your applications.
```

and quits causing the extension to think the language server has crashed and restart with the affected name until it times out. 

On the proxy side, the proxy expects messages in the standard LSP format:

```
Content-Length: <number>\r\n
\r\n
<JSON payload>
```

Given an input message from VSCodium (which causes the language server to break):
```json
{"jsonrpc":"2.0","id":0,"method":"initialize","params":{"clientInfo":{"name":"VSCodium","version":"1.112.0"},...}}
```

The proxy outputs:
```json
{"jsonrpc":"2.0","id":0,"method":"initialize","params":{"clientInfo":{"name":"Visual Studio Code","version":"1.112.0"},...}}
```

in order to allow the language server to work.

 While doing this this breaks the [runtime licenses](https://github.com/microsoft/vscode-cpptools/blob/main/RuntimeLicenses/cpptools-LICENSE.txt) by running outside of Visual Studio Code, it's not like anyone cares if Microsoft [contributes to AI](https://www.cnbc.com/2025/04/29/satya-nadella-says-as-much-as-30percent-of-microsoft-code-is-written-by-ai.html) so much that they break [licenses on other contributors code by making AI models](https://nogithub.codeberg.page/) and [break their own OS](https://www.neowin.net/news/windows-11-25h2-24h2-allegedly-still-deleting-internet-and-with-only-one-way-to-fix-it/) one day after the other using their AI.

### But how does this proxy work in more detail?

1. **Reads Messages**: The proxy reads complete LSP messages from stdin, parsing the `Content-Length` header to know how many bytes to read.

2. **Detects Initialize**: For each message, it checks if the JSON contains `"method":"initialize"`.

3. **Modifies Client Name**: If it's an initialize message, it searches for `"name":"..."` patterns and replaces any client name that isn't "Visual Studio Code" with "Visual Studio Code". (This basically gets around the runtime restriction of the language server crashing upon detecting other product names)

4. **Passes Other Messages**: All non-initialize messages are passed through unchanged.

5. **Writes Messages**: Modified messages are written back to stdout in proper LSP format with updated `Content-Length` headers.


## Implementation Details

- **Single-pass string processing**: The implementation uses a single pass through the JSON message to find and replace the client name, making it efficient even for large messages.
- **Safe memory handling**: All allocations include bounds checking and proper cleanup.
- **LSP protocol aware**: Correctly handles the binary-safe Content-Length based framing of LSP.
- **Minimal dependencies**: Uses only standard C library functions; no external JSON parsing library required.

## Building for Different Environments

The Makefile uses standard gcc flags. To customize compilation:

```bash
CC=clang CFLAGS="-Wall -O3" make
```

## Adding application properties (Windows only)
If you want to embed application details, do these steps:
```bash
# run this step within the same path as the cpptools-proxy folder
# using MinGW's windres
windres -i cpptools-proxy.rc -o cpptools-proxy.res -O coff
# or using MSVC's rc compiler
#rc /fo cpptools-proxy.res cpptools-proxy.rc
# then you can compile usually
gcc -o cpptools-proxy.exe cpptools-proxy.c cpptools-proxy.res
# or within msvc
#cl /Fe:cpptools-proxy.exe cpptools-proxy.c cpptools-proxy.res
```

This step is not required when using Zig's C/C++ toolchain, you can pass the .rc file directly.
```bash
zig cc -o cpptools-proxy.exe cpptools-proxy.c cpptools-proxy.rc
```

## Cleaning Up

```bash
make clean
```