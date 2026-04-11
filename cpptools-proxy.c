#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <process.h>
#include <io.h>
#include <fcntl.h>
#else
#define _POSIX_C_SOURCE 200809L
#include <unistd.h>
#include <pthread.h>
#include <sys/types.h>
#include <sys/wait.h>
#endif

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_CONTENT_LENGTH 1000000

#ifdef _WIN32
#define PATH_SEP '\\'
#define DEFAULT_CPPTOOLS_ORIG "cpptools-orig.exe"
#ifndef STDIN_FILENO
#define STDIN_FILENO 0
#endif
#ifndef STDOUT_FILENO
#define STDOUT_FILENO 1
#endif
static char *xstrdup(const char *s) {
    return _strdup(s);
}
#else
#define PATH_SEP '/'
#define DEFAULT_CPPTOOLS_ORIG "cpptools-orig"
static char *xstrdup(const char *s) {
    return strdup(s);
}
#endif

/**
 * LSP Proxy: Launches an executable and proxies LSP messages bidirectionally.
 * Modifies JSON-RPC initialize messages to replace clientInfo.name with "Visual Studio Code".
 */

typedef struct {
    FILE *client_in;
    FILE *server_in;
} ClientToServerArgs;

typedef struct {
    FILE *server_out;
    FILE *client_out;
} ServerToClientArgs;

/**
 * Modify clientInfo.name in JSON if different from "Visual Studio Code"
 */
char* modify_client_info(const char *json, size_t *out_len) {
    // Check if this is an initialize message
    if (strstr(json, "\"method\":\"initialize\"") == NULL) {
        // Not an initialize message, return copy as-is
        char *result = malloc(strlen(json) + 1);
        strcpy(result, json);
        *out_len = strlen(json);
        return result;
    }

    const char *client_info_search = "\"clientInfo\":{";
    const char *new_name = "Visual Studio Code";
    
    char *result = malloc(MAX_CONTENT_LENGTH);
    if (!result) return NULL;
    
    size_t result_pos = 0;
    const char *current = json;
    
    const char *client_info = strstr(current, client_info_search);
    if (client_info == NULL) {
        strcpy(result, json);
        *out_len = strlen(json);
        return result;
    }
    
    size_t pre_len = client_info - json + strlen(client_info_search);
    memcpy(result + result_pos, json, pre_len);
    result_pos += pre_len;
    
    current = client_info + strlen(client_info_search);
    const char *name_search = "\"name\":\"";
    const char *name_field = strstr(current, name_search);
    
    if (name_field != NULL) {
        size_t pre_name = name_field - current + strlen(name_search);
        memcpy(result + result_pos, current, pre_name);
        result_pos += pre_name;
        
        const char *end_quote = strchr(name_field + strlen(name_search), '"');
        
        if (end_quote != NULL) {
            int old_name_len = end_quote - (name_field + strlen(name_search));
            
            if (old_name_len != (int)strlen(new_name) || 
                strncmp(name_field + strlen(name_search), new_name, old_name_len) != 0) {
                strcpy(result + result_pos, new_name);
                result_pos += strlen(new_name);
            } else {
                memcpy(result + result_pos, name_field + strlen(name_search), old_name_len);
                result_pos += old_name_len;
            }
            
            current = end_quote;
            strcpy(result + result_pos, current);
            result_pos += strlen(current);
        } else {
            strcpy(result + result_pos, current);
            result_pos += strlen(current);
        }
    } else {
        strcpy(result + result_pos, current);
        result_pos += strlen(current);
    }
    
    result[result_pos] = '\0';
    *out_len = result_pos;
    return result;
}

/**
 * Read a single LSP message from FILE stream
 */
char* read_lsp_message(FILE *fp, size_t *msg_len) {
    char line[512];
    int content_length = 0;
    
    while (fgets(line, sizeof(line), fp) != NULL) {
        size_t len = strlen(line);
        if (len > 0 && line[len-1] == '\n') {
            line[len-1] = '\0';
            len--;
        }
        if (len > 0 && line[len-1] == '\r') {
            line[len-1] = '\0';
        }
        
        if (strlen(line) == 0) break;
        
        if (strncmp(line, "Content-Length:", 15) == 0) {
            sscanf(line + 15, "%d", &content_length);
        }
    }
    
    if (content_length <= 0 || content_length > MAX_CONTENT_LENGTH) {
        return NULL;
    }
    
    char *message = malloc(content_length + 1);
    if (!message) return NULL;
    
    size_t bytes_read = fread(message, 1, content_length, fp);
    if (bytes_read != (size_t)content_length) {
        free(message);
        return NULL;
    }
    
    message[content_length] = '\0';
    *msg_len = content_length;
    return message;
}

/**
 * Write LSP message to FILE stream
 */
void write_lsp_message(FILE *fp, const char *message, size_t msg_len) {
    fprintf(fp, "Content-Length: %zu\r\n\r\n", msg_len);
    fwrite(message, 1, msg_len, fp);
    fflush(fp);
}

/**
 * Thread: forward messages from client to server
 */
#ifdef _WIN32
static unsigned __stdcall client_to_server(void *arg)
#else
void* client_to_server(void *arg)
#endif
{
    ClientToServerArgs *args = (ClientToServerArgs *)arg;
    size_t msg_len;
    char *message;
    
    while ((message = read_lsp_message(args->client_in, &msg_len)) != NULL) {
        size_t modified_len;
        char *modified = modify_client_info(message, &modified_len);
        write_lsp_message(args->server_in, modified, modified_len);
        free(message);
        free(modified);
    }
    
    fclose(args->server_in);
    free(args);
#ifdef _WIN32
    return 0;
#else
    return NULL;
#endif
}

/**
 * Thread: forward messages from server to client
 */
#ifdef _WIN32
static unsigned __stdcall server_to_client(void *arg)
#else
void* server_to_client(void *arg)
#endif
{
    ServerToClientArgs *args = (ServerToClientArgs *)arg;
    size_t msg_len;
    char *message;
    
    while ((message = read_lsp_message(args->server_out, &msg_len)) != NULL) {
        write_lsp_message(args->client_out, message, msg_len);
        free(message);
    }
    
    fclose(args->client_out);
    free(args);
#ifdef _WIN32
    return 0;
#else
    return NULL;
#endif
}

/**
 * Get the directory component of a path
 * Returns a newly allocated string (must be freed)
 */
char* get_directory(const char *path) {
    const char *last_slash = strrchr(path, '/');
    const char *last_back = strrchr(path, '\\');
    const char *last = NULL;
    if (last_slash && last_back) {
        last = (last_slash > last_back) ? last_slash : last_back;
    } else if (last_slash) {
        last = last_slash;
    } else if (last_back) {
        last = last_back;
    }
    if (!last) {
        // No directory component, return current directory
        return xstrdup(".");
    }
    
    size_t dir_len = (size_t)(last - path);
    if (dir_len == 0) {
#ifdef _WIN32
        if (last == path && (*path == '/' || *path == '\\')) {
            return xstrdup("\\");
        }
#else
        // Root directory
        return xstrdup("/");
#endif
    }
    
    char *dir = malloc(dir_len + 1);
    strncpy(dir, path, dir_len);
    dir[dir_len] = '\0';
    return dir;
}

#ifdef _WIN32
static void stop_thread_if_running(HANDLE th) {
    DWORD code;
    if (th == NULL || th == INVALID_HANDLE_VALUE) return;
    if (GetExitCodeThread(th, &code) && code == STILL_ACTIVE) {
        TerminateThread(th, 0);
    }
    WaitForSingleObject(th, INFINITE);
    CloseHandle(th);
}
#endif

int main(int argc, char *argv[]) {
    const char *server_path = NULL;
    
    // Parse arguments for --path option
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--path") == 0) {
            if (i + 1 < argc) {
                server_path = argv[i + 1];
                // Remove --path and the path from argv for later use
                // Shift remaining args to remove --path option
                for (int j = i; j < argc - 2; j++) {
                    argv[j] = argv[j + 2];
                }
                argc -= 2;
                break;
            } else {
                fprintf(stderr, "Error: --path requires an argument\n");
                exit(1);
            }
        }
    }
    
    // If no --path specified, default to cpptools-orig(.exe on Windows) in same directory as proxy
    if (!server_path) {
        char *proxy_dir = get_directory(argv[0]);
        static char default_path[1024];
        snprintf(default_path, sizeof(default_path), "%s%c%s", proxy_dir, PATH_SEP, DEFAULT_CPPTOOLS_ORIG);
        server_path = default_path;
        free(proxy_dir);
    }
    
    // Verify server exists
#ifdef _WIN32
    if (_access(server_path, 0) != 0) {
#else
    if (access(server_path, X_OK) != 0) {
#endif
        fprintf(stderr, "Error: Server not found or not executable: %s\n", server_path);
        fprintf(stderr, " ");
        fprintf(stderr, "Did you place the proxy binary in the same path where %s is?", DEFAULT_CPPTOOLS_ORIG);
        fprintf(stderr, "If not, rename cpptools to %s if original cpptools exists and this binary to cpptools then try again.", DEFAULT_CPPTOOLS_ORIG);
        fprintf(stderr, "Otherwaise you can specify a custom path below:");
        fprintf(stderr, "Usage: %s [--path /path/to/server]\n", argv[0]);
        fprintf(stderr, "\nDefault: looks for '%s' in same directory as proxy\n", DEFAULT_CPPTOOLS_ORIG);
        exit(1);
    }
    
#ifdef _WIN32
    /*
     * Win32 stdio defaults to text mode: CRLF is translated. LSP framing uses raw
     * Content-Length byte counts; translation breaks fread of the JSON body and the
     * server exits, which looks like a hang on the client side.
     */
    if (_setmode(_fileno(stdin), _O_BINARY) == -1 ||
        _setmode(_fileno(stdout), _O_BINARY) == -1) {
        perror("_setmode");
        exit(1);
    }

    SECURITY_ATTRIBUTES sa;
    sa.nLength = sizeof(sa);
    sa.lpSecurityDescriptor = NULL;
    sa.bInheritHandle = TRUE;

    HANDLE hChildStdInRd = INVALID_HANDLE_VALUE;
    HANDLE hChildStdInWr = INVALID_HANDLE_VALUE;
    HANDLE hChildStdOutRd = INVALID_HANDLE_VALUE;
    HANDLE hChildStdOutWr = INVALID_HANDLE_VALUE;

    if (!CreatePipe(&hChildStdInRd, &hChildStdInWr, &sa, 0) ||
        !CreatePipe(&hChildStdOutRd, &hChildStdOutWr, &sa, 0)) {
        fprintf(stderr, "CreatePipe failed\n");
        exit(1);
    }

    STARTUPINFOA si;
    ZeroMemory(&si, sizeof(si));
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdInput = hChildStdInRd;
    si.hStdOutput = hChildStdOutWr;
    si.hStdError = GetStdHandle(STD_ERROR_HANDLE);

    PROCESS_INFORMATION pi;
    ZeroMemory(&pi, sizeof(pi));

    if (!CreateProcessA(server_path, NULL, NULL, NULL, TRUE, 0, NULL, NULL, &si, &pi)) {
        fprintf(stderr, "CreateProcess failed for: %s\n", server_path);
        CloseHandle(hChildStdInRd);
        CloseHandle(hChildStdInWr);
        CloseHandle(hChildStdOutRd);
        CloseHandle(hChildStdOutWr);
        exit(1);
    }

    CloseHandle(hChildStdInRd);
    CloseHandle(hChildStdOutWr);
    CloseHandle(pi.hThread);

    int fd_in = _open_osfhandle((intptr_t)hChildStdInWr, _O_BINARY);
    int fd_out = _open_osfhandle((intptr_t)hChildStdOutRd, _O_BINARY);
    if (fd_in < 0 || fd_out < 0) {
        fprintf(stderr, "_open_osfhandle failed\n");
        TerminateProcess(pi.hProcess, 1);
        CloseHandle(pi.hProcess);
        exit(1);
    }

    FILE *client_in = stdin;
    FILE *server_in = _fdopen(fd_in, "wb");
    FILE *server_out = _fdopen(fd_out, "rb");
    FILE *client_out = stdout;

    if (!server_in || !server_out) {
        perror("_fdopen");
        TerminateProcess(pi.hProcess, 1);
        CloseHandle(pi.hProcess);
        exit(1);
    }

    setvbuf(stdin, NULL, _IONBF, 0);
    setvbuf(stdout, NULL, _IONBF, 0);
    setvbuf(server_in, NULL, _IONBF, 0);
    setvbuf(server_out, NULL, _IONBF, 0);

    ClientToServerArgs *c2s_args = malloc(sizeof(ClientToServerArgs));
    c2s_args->client_in = client_in;
    c2s_args->server_in = server_in;

    ServerToClientArgs *s2c_args = malloc(sizeof(ServerToClientArgs));
    s2c_args->server_out = server_out;
    s2c_args->client_out = client_out;

    HANDLE c2s_thread = (HANDLE)_beginthreadex(NULL, 0, client_to_server, c2s_args, 0, NULL);
    HANDLE s2c_thread = (HANDLE)_beginthreadex(NULL, 0, server_to_client, s2c_args, 0, NULL);
    if (!c2s_thread || !s2c_thread) {
        fprintf(stderr, "_beginthreadex failed\n");
        TerminateProcess(pi.hProcess, 1);
        CloseHandle(pi.hProcess);
        exit(1);
    }

    WaitForSingleObject(pi.hProcess, INFINITE);

    DWORD status = 1;
    GetExitCodeProcess(pi.hProcess, &status);
    CloseHandle(pi.hProcess);

    stop_thread_if_running(c2s_thread);
    stop_thread_if_running(s2c_thread);

    return (int)status;
#else
    int child_stdin[2], child_stdout[2];
    if (pipe(child_stdin) < 0 || pipe(child_stdout) < 0) {
        perror("pipe");
        exit(1);
    }
    
    pid_t child_pid = fork();
    if (child_pid < 0) {
        perror("fork");
        exit(1);
    }
    
    if (child_pid == 0) {
        dup2(child_stdin[0], STDIN_FILENO);
        dup2(child_stdout[1], STDOUT_FILENO);
        close(child_stdin[0]);
        close(child_stdin[1]);
        close(child_stdout[0]);
        close(child_stdout[1]);
        
        // Execute the server directly (not via PATH)
        execv(server_path, (char *const[]){(char *)server_path, NULL});
        perror("execv");
        exit(1);
    }
    
    close(child_stdin[0]);
    close(child_stdout[1]);
    
    FILE *client_in = stdin;
    FILE *server_in = fdopen(child_stdin[1], "w");
    FILE *server_out = fdopen(child_stdout[0], "r");
    FILE *client_out = stdout;
    
    if (!server_in || !server_out) {
        perror("fdopen");
        exit(1);
    }
    
    pthread_t c2s_thread, s2c_thread;
    
    ClientToServerArgs *c2s_args = malloc(sizeof(ClientToServerArgs));
    c2s_args->client_in = client_in;
    c2s_args->server_in = server_in;
    
    if (pthread_create(&c2s_thread, NULL, client_to_server, c2s_args) != 0) {
        perror("pthread_create");
        exit(1);
    }
    
    ServerToClientArgs *s2c_args = malloc(sizeof(ServerToClientArgs));
    s2c_args->server_out = server_out;
    s2c_args->client_out = client_out;
    
    if (pthread_create(&s2c_thread, NULL, server_to_client, s2c_args) != 0) {
        perror("pthread_create");
        exit(1);
    }
    
    int st;
    waitpid(child_pid, &st, 0);
    
    pthread_cancel(c2s_thread);
    pthread_cancel(s2c_thread);
    pthread_join(c2s_thread, NULL);
    pthread_join(s2c_thread, NULL);
    
    return WIFEXITED(st) ? WEXITSTATUS(st) : 1;
#endif
}
