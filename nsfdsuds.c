/*
 * -----
 * Author: Shao Miller <code@sha0.net>
 * Date: 2021-02-23
 * Copyright 2021 Shao Miller - All Rights Reserved
 *
 * Permission is hereby granted, free of charge, to any person
 * obtaining a copy of this software and associated documentation
 * files (the "Software"), to deal in the Software without
 * restriction, including without limitation the rights to use,
 * copy, modify, merge, publish, distribute, sublicense, and/or
 * sell copies of the Software, and to permit persons to whom
 * the Software is furnished to do so, subject to the following
 * conditions:
 *
 * The above copyright notice and this permission notice shall
 * be included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES
 * OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT
 * HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY,
 * WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 * -----
 *
 * Try building with: gcc -ansi -pedantic -Wall -Wextra -Werror -o nsfdsuds nsfdsuds.c
 */
#include <errno.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <fcntl.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/un.h>
#include <unistd.h>

#define CountOf(arr) (sizeof (arr) / sizeof *(arr))

enum
  {
    cmd_start = 42,
    cmd_end,
    cmd_fds,
    cmd_error
  };

struct program;
typedef int mode_func(struct program *);

static int check_args(struct program * prog);
static mode_func client_mode;
static int client_mode(struct program * prog);
static int client_of_server(struct program * prog);
static int close_nsfds(struct program * prog);
static int recv_fds(struct program * prog);
static int send_fds(struct program * prog);
static int serve_client(struct program * prog);
static mode_func server_mode;
static int server_mode(struct program * prog);
static int unlink_socket(struct program * prog);
static void usage(struct program * prog);

static struct
  {
    char * arg;
    mode_func * mode_func;
  }
  modes[] =
  {
    { "--client", &client_mode },
    { "--server", &server_mode }
  };
static char ns_path[] = "/proc/self/ns/";
static char ns_types[][sizeof "user" /* longest entry */] =
  {
    "ipc",
    "mnt",
    "net",
    "pid",
    "user",
    "uts"
  };

struct program
  {
    int argc;
    char ** argv;
    char * name;
    char * socket_path;
    mode_func * mode_func;
    int client;
    int nsfds[CountOf(ns_types)];
  };

int main(int argc, char ** argv)
  {
    struct program prog;
    unsigned int nsi;
    int rv;

    prog.argc = argc;
    prog.argv = argv;

    rv = check_args(&prog);
    if (rv != EXIT_SUCCESS)
      goto err_check_args;

    prog.client = -1;

    for (nsi = 0; nsi < CountOf(ns_types); ++nsi)
      prog.nsfds[nsi] = -1;

    rv = prog.mode_func(&prog);
    if (rv != EXIT_SUCCESS)
      goto err_mode_func;

    err_mode_func:

    err_check_args:

    return rv;
  }

static int check_args(struct program * prog)
  {
    unsigned int mode;

    if (prog->argc < 1 || !prog->argv[0])
      {
        fprintf(stderr, "The C implementation did not produce a valid program-name\n");
        return EXIT_FAILURE;
      }
    prog->name = prog->argv[0];

    if (prog->argc < 3)
      {
        fprintf(stderr, "Invalid argument count\n\n");
        usage(prog);
        return EXIT_FAILURE;
      }

    for (mode = 0; mode < CountOf(modes); ++mode)
      {
        if (!strcmp(prog->argv[1], modes[mode].arg))
          break;
      }
    if (mode == CountOf(modes))
      {
        fprintf(stderr, "Invalid mode\n\n");
        usage(prog);
        return EXIT_FAILURE;
      }
    prog->mode_func = modes[mode].mode_func;

    if (strlen(prog->argv[2]) >= sizeof ((struct sockaddr_un *) NULL)->sun_path)
      {
        fprintf(stderr, "Socket path too long\n");
        return EXIT_FAILURE;
      }
    prog->socket_path = prog->argv[2];

    return EXIT_SUCCESS;
  }

static int client_mode(struct program * prog)
  {
    int close_rv;
    int connect_rv;
    int e;
    int pe;
    int rv;
    int sock;
    struct sockaddr_un sock_addr;

    rv = EXIT_FAILURE;

    pe = errno;
    errno = 0;
    sock = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
    e = errno;
    errno = pe;
    if (sock == -1)
      {
        fprintf(stderr, "Socket error %d: %s\n", e, strerror(e));
        goto err_sock;
      }

    sock_addr.sun_family = AF_UNIX;
    strcpy(sock_addr.sun_path, prog->socket_path);

    pe = errno;
    errno = 0;
    connect_rv = connect(sock, (struct sockaddr *) &sock_addr, sizeof sock_addr);
    e = errno;
    errno = pe;
    if (connect_rv)
      {
        fprintf(stderr, "Error %d while connecting to server: %s\n", e, strerror(e));
        goto err_connect;
      }
    prog->client = sock;

    rv = client_of_server(prog);

    err_connect:

    pe = errno;
    errno = 0;
    close_rv = close(sock);
    e = errno;
    errno = pe;
    if (close_rv)
      {
        fprintf(stderr, "Error %d while closing socket: %s\n", e, strerror(e));
        rv = EXIT_FAILURE;
      }
    err_sock:

    return rv;
  }

static int client_of_server(struct program * prog)
  {
    int e;
    int pe;
    ssize_t write_rv;
    unsigned char cmds[] = { cmd_start, cmd_fds, cmd_end };

    /* TODO: Actually progress through the states */

    pe = errno;
    errno = 0;
    write_rv = write(prog->client, cmds, sizeof cmds);
    e = errno;
    errno = pe;
    if (write_rv < (ssize_t) sizeof cmds)
      {
        fprintf(stderr, "Error %d while sending commands: %s\n", e, strerror(e));
        return EXIT_FAILURE;
      }

    return recv_fds(prog);
  }

static int close_nsfds(struct program * prog)
  {
    int close_rv;
    int e;
    unsigned int nsi;
    int pe;
    int rv;

    pe = errno;
    rv = EXIT_SUCCESS;
    for (nsi = 0; nsi < CountOf(ns_types); ++nsi)
      {
        if (prog->nsfds[nsi] == -1)
          continue;
        errno = 0;
        close_rv = close(prog->nsfds[nsi]);
        e = errno;
        if (close_rv)
          {
            fprintf(stderr, "Closing NS FD %u failed with error %d:\n%s\n", nsi, e, strerror(e));
            rv = EXIT_FAILURE;
          }
          else
          {
            prog->nsfds[nsi] = -1;
          }
      }
    errno = pe;
    return rv;
  }

static int recv_fds(struct program * prog)
  {
    unsigned char cmd;
    void * cmsg_data;
    struct cmsghdr * cmsg_hdr;
    int e;
    struct iovec iovec;
    struct msghdr msg_hdr;
    char msg_space[CMSG_SPACE(sizeof prog->nsfds)];
    int pe;
    ssize_t recvmsg_rv;

    iovec.iov_base = &cmd;
    iovec.iov_len = sizeof cmd;

    msg_hdr.msg_name = NULL;
    msg_hdr.msg_namelen = 0;
    msg_hdr.msg_iov = &iovec;
    msg_hdr.msg_iovlen = 1;
    msg_hdr.msg_flags = 0;
    msg_hdr.msg_control = msg_space;
    msg_hdr.msg_controllen = sizeof msg_space;

    pe = errno;
    errno = 0;
    recvmsg_rv = recvmsg(prog->client, &msg_hdr, 0);
    e = errno;
    errno = pe;
    if (recvmsg_rv < (ssize_t) sizeof cmd)
      {
        fprintf(stderr, "Error %d while receiving FDs: %s\n", e, strerror(e));
        return EXIT_FAILURE;
      }

    cmsg_hdr = CMSG_FIRSTHDR(&msg_hdr);
    if (cmsg_hdr->cmsg_level != SOL_SOCKET || cmsg_hdr->cmsg_type != SCM_RIGHTS)
      {
        fprintf(stderr, "Unexpected level or type for ancillary data\n");
        return EXIT_FAILURE;
      }
    if (cmsg_hdr->cmsg_len != CMSG_LEN(sizeof prog->nsfds))
      {
        fprintf(stderr, "Unexpected length for ancillary data\n");
        return EXIT_FAILURE;
      }

    cmsg_data = CMSG_DATA(cmsg_hdr);
    memcpy(prog->nsfds, cmsg_data, sizeof prog->nsfds);

    if (prog->argc < 4)
      {
        fprintf(stderr, "Received FDs and nothing more to do\n");
        return EXIT_SUCCESS;
      }        

    pe = errno;
    errno = 0;
    execv(prog->argv[3], prog->argv + 4);
    e = errno;
    errno = pe;
    fprintf(stderr, "Failed to execv to program '%s' with error %d: %s\n", prog->argv[3], e, strerror(e));
    return EXIT_FAILURE;
  }

static int send_fds(struct program * prog)
  {
    unsigned char cmd;
    void * cmsg_data;
    struct cmsghdr * cmsg_hdr;
    int e;
    struct iovec iovec;
    struct msghdr msg_hdr;
    char msg_space[CMSG_SPACE(sizeof prog->nsfds)];
    int pe;
    ssize_t sendmsg_rv;

    cmd = cmd_fds;

    iovec.iov_base = &cmd;
    iovec.iov_len = sizeof cmd;

    msg_hdr.msg_name = NULL;
    msg_hdr.msg_namelen = 0;
    msg_hdr.msg_iov = &iovec;
    msg_hdr.msg_iovlen = 1;
    msg_hdr.msg_flags = 0;
    msg_hdr.msg_control = msg_space;
    msg_hdr.msg_controllen = sizeof msg_space;

    cmsg_hdr = CMSG_FIRSTHDR(&msg_hdr);
    cmsg_hdr->cmsg_level = SOL_SOCKET;
    cmsg_hdr->cmsg_type = SCM_RIGHTS;
    cmsg_hdr->cmsg_len = CMSG_LEN(sizeof prog->nsfds);
    cmsg_data = CMSG_DATA(cmsg_hdr);
    memcpy(cmsg_data, prog->nsfds, sizeof prog->nsfds);

    msg_hdr.msg_controllen = cmsg_hdr->cmsg_len;

    pe = errno;
    errno = 0;
    sendmsg_rv = sendmsg(prog->client, &msg_hdr, MSG_NOSIGNAL);
    e = errno;
    errno = pe;
    if (sendmsg_rv < (ssize_t) sizeof cmd)
      {
        fprintf(stderr, "Error %d while sending FDs: %s\n", e, strerror(e));
        return EXIT_FAILURE;
      }
    return EXIT_SUCCESS;
  }

static int serve_client(struct program * prog)
  {
    int client;
    unsigned char cmd;
    int e;
    int pe;
    ssize_t read_rv;
    int rv;
    int started;

    client = prog->client;
    started = 0;

    get_cmd:
    pe = errno;
    errno = 0;
    read_rv = read(client, &cmd, sizeof cmd);
    e = errno;
    errno = pe;
    if (read_rv == -1)
      {
        fprintf(stderr, "Error %d reading command from client: %s\n", e, strerror(e));
        return EXIT_FAILURE;
      }

    switch (cmd)
      {
        default:
        fprintf(stderr, "Invalid command %d from client\n", (int) cmd);
        return EXIT_FAILURE;

        case cmd_start:
        if (started)
          {
            fprintf(stderr, "Received duplicate start command from client\n");
            return EXIT_FAILURE;
          }
        started = 1;
        goto get_cmd;

        case cmd_end:
        if (!started)
          {
            fprintf(stderr, "Received end command before start command, from client\n");
            return EXIT_FAILURE;
          }
        return EXIT_SUCCESS;

        case cmd_fds:
        if (!started)
          {
            fprintf(stderr, "Received FDs command before start command, from client\n");
            return EXIT_FAILURE;
          }
        rv = send_fds(prog);
        if (rv != EXIT_SUCCESS)
          return rv;
          else
          goto get_cmd;

        case cmd_error:
        fprintf(stderr, "Client reported error\n");
        return EXIT_FAILURE;
      }
  }

static int server_mode(struct program * prog)
  {
    int bind_rv;
    int client;
    int close_rv;
    int e;
    int listen_rv;
    unsigned int nsi;
    char path[CountOf(ns_path) + CountOf(ns_types[0])];
    int pe;
    int rv;
    int sock;
    struct sockaddr_un sock_addr;
    int unlink_socket_rv;

    rv = EXIT_FAILURE;

    unlink_socket_rv = unlink_socket(prog);
    if (unlink_socket_rv)
      goto err_unlink;

    pe = errno;
    errno = 0;
    sock = socket(AF_UNIX, SOCK_STREAM, 0);
    e = errno;
    errno = pe;
    if (sock == -1)
      {
        fprintf(stderr, "Socket error %d: %s\n", e, strerror(e));
        goto err_sock;
      }

    sock_addr.sun_family = AF_UNIX;
    strcpy(sock_addr.sun_path, prog->socket_path);

    pe = errno;
    errno = 0;
    bind_rv = bind(sock, (struct sockaddr *) &sock_addr, sizeof sock_addr);
    e = errno;
    errno = pe;
    if (bind_rv)
      {
        fprintf(stderr, "Error %d while binding socket: %s\n", e, strerror(e));
        goto err_bind;
      }

    for (nsi = 0; nsi < CountOf(ns_types); ++nsi)
      {
        strcpy(path, ns_path);
        strcat(path, ns_types[nsi]);
        pe = errno;
        errno = 0;
        prog->nsfds[nsi] = open(path, O_RDONLY);
        e = errno;
        errno = pe;
        if (prog->nsfds[nsi] == -1)
          {
            fprintf(stderr, "Error %d while opening '%s': %s\n", e, path, strerror(e));
            goto err_fds;
          }
      }

    pe = errno;
    errno = 0;
    listen_rv = listen(sock, 1);
    e = errno;
    errno = pe;
    if (listen_rv)
      {
        fprintf(stderr, "Error %d while trying to listen on socket: %s\n", e, strerror(e));
        goto err_listen;
      }

    pe = errno;
    errno = 0;
    client = accept(sock, NULL, NULL);
    e = errno;
    errno = pe;
    if (client == -1)
      {
        fprintf(stderr, "Error %d while trying to accept connection on socket: %s\n", e, strerror(e));
        goto err_accept;
      }
    prog->client = client;

    rv = serve_client(prog);

    pe = errno;
    errno = 0;
    close_rv = close(client);
    e = errno;
    errno = pe;
    if (close_rv)
      fprintf(stderr, "Error %d while closing connection: %s\n", e, strerror(e));
    err_accept:

    err_listen:

    err_fds:
    close_nsfds(prog);

    /* See unlink_socket comment, below */
    err_bind:

    pe = errno;
    errno = 0;
    close_rv = close(sock);
    e = errno;
    errno = pe;
    if (close_rv)
      {
        fprintf(stderr, "Error %d while closing socket: %s\n", e, strerror(e));
        rv = EXIT_FAILURE;
      }
    err_sock:

    /* Although established via 'bind', clean up after closing the socket */
    unlink_socket(prog);

    err_unlink:

    return rv;
  }

static int unlink_socket(struct program * prog)
  {
    int e;
    int pe;
    int unlink_rv;

    pe = errno;
    errno = 0;
    unlink_rv = unlink(prog->socket_path);
    e = errno;
    errno = pe;
    if (unlink_rv == -1 && e != ENOENT)
      {
        fprintf(stderr, "Unlinking '%s' failed with error %d:\n%s\n", prog->socket_path, e, strerror(e));
        return e;
      }
    return 0;
  }

static void usage(struct program * prog)
  {
    fprintf(stderr, "Usage:\n\nClient mode: %s --client <socketpath> [program [args]]\nReceive namespace FDs and execv to [program] with [args]\n\nServer mode: %s --server <socketpath>\nWait for connection from client and then send namespace FDs\n", prog->name, prog->name);
  }
