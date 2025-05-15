/*
 * ftp_client_simple.c
 * Simple FTP Client based on FEUP examples with basic credential support
 *   - clientTCP.c (basic TCP connect/send)
 *   - getip.c (DNS resolution via gethostbyname)
 *
 * Objetivo:
 *   Com o URL do site FTP ([user:pass@]host/path), conectar ao servidor,
 *   fazer login (anónimo ou credenciais) e descarregar o ficheiro indicado.
 *
 * Usage:
 *   ./ftp_client_simple ftp://[user:pass@]host/path
 *
 * Supports:
 *   - Anonymous or user/password login (USER/PASS)
 *   - Binary mode (TYPE I)
 *   - Passive mode data transfer (PASV + RETR)
 *   - Saves file to current directory
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>

#define CONTROL_PORT 21
#define BUF_SIZE 4096

// Read a line ending in '\n'
ssize_t read_line(int sock, char *buf, size_t maxlen) {
    ssize_t n, total = 0;
    char c;
    while (total < maxlen - 1) {
        n = read(sock, &c, 1);
        if (n <= 0) break;
        buf[total++] = c;
        if (c == '\n') break;
    }
    buf[total] = '\0';
    return total;
}

// Read full FTP server response (handles multiline per RFC 959)
int read_response(int sock, char *buf, size_t buflen) {
    char code[4];
    ssize_t n = read_line(sock, buf, buflen);
    if (n <= 0) return -1;
    printf("< %s", buf);

    // Copy response code
    memcpy(code, buf, 3);
    code[3] = '\0';
    int multiline = (buf[3] == '-');

    // Read until code + space if multiline
    while (multiline) {
        n = read_line(sock, buf, buflen);
        if (n <= 0) return -1;
        printf("< %s", buf);
        if (strncmp(buf, code, 3) == 0 && buf[3] == ' ') {
            multiline = 0;
        }
    }
    return atoi(code);
}

int main(int argc, char *argv[]) {
    if (argc != 2) {
        fprintf(stderr, "Usage: %s ftp://[user:pass@]host/path\n", argv[0]);
        exit(-1);
    }

    // Parse URL: ftp://[user:pass@]host/path
    char *url = argv[1];
    const char *prefix = "ftp://";
    if (strncmp(url, prefix, strlen(prefix)) != 0) {
        fprintf(stderr, "URL must start with ftp://\n");
        exit(-1);
    }
    char *p = url + strlen(prefix);
    // split credentials@host and path
    char *slash = strchr(p, '/');
    if (!slash) {
        fprintf(stderr, "URL must contain a path after host\n");
        exit(-1);
    }
    *slash = '\0';
    char *cred_host = p;
    char *path = slash + 1;

    // default to anonymous
    char user[64] = "anonymous";
    char pass[64] = "anonymous@";

    // check if credentials provided
    char *at = strchr(cred_host, '@');
    char *host = cred_host;
    if (at) {
        *at = '\0';
        host = at + 1;
        // split user:pass
        char *colon = strchr(cred_host, ':');
        if (colon) {
            *colon = '\0';
            strncpy(user, cred_host, sizeof(user)-1);
            strncpy(pass, colon+1, sizeof(pass)-1);
        } else {
            strncpy(user, cred_host, sizeof(user)-1);
        }
    }

    // Resolve hostname
    struct hostent *h;
    if ((h = gethostbyname(host)) == NULL) {
        herror("gethostbyname");
        exit(-1);
    }
    char server_ip[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, h->h_addr_list[0], server_ip, sizeof(server_ip));

    // Connect to control port
    int ctrl;
    struct sockaddr_in ctrl_addr;
    bzero(&ctrl_addr, sizeof(ctrl_addr));
    ctrl_addr.sin_family = AF_INET;
    ctrl_addr.sin_port = htons(CONTROL_PORT);
    inet_aton(server_ip, &ctrl_addr.sin_addr);

    if ((ctrl = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        perror("socket"); exit(-1);
    }
    if (connect(ctrl, (struct sockaddr *)&ctrl_addr, sizeof(ctrl_addr)) < 0) {
        perror("connect"); exit(-1);
    }

    char buf[BUF_SIZE];
    // Read initial server greeting
    if (read_response(ctrl, buf, sizeof(buf)) < 0) {
        fprintf(stderr, "Failed to read server greeting\n");
        exit(-1);
    }

    // Send USER
    snprintf(buf, sizeof(buf), "USER %s\r\n", user);
    write(ctrl, buf, strlen(buf));
    if (read_response(ctrl, buf, sizeof(buf)) != 331) {
        fprintf(stderr, "USER command failed\n");
        exit(-1);
    }

    // Send PASS
    snprintf(buf, sizeof(buf), "PASS %s\r\n", pass);
    write(ctrl, buf, strlen(buf));
    if (read_response(ctrl, buf, sizeof(buf)) != 230) {
        fprintf(stderr, "PASS command failed\n");
        exit(-1);
    }

    // Set binary mode
    snprintf(buf, sizeof(buf), "TYPE I\r\n");
    write(ctrl, buf, strlen(buf));
    if (read_response(ctrl, buf, sizeof(buf)) != 200) {
        fprintf(stderr, "Failed to set binary mode\n");
        exit(-1);
    }

    // Enter passive mode
    snprintf(buf, sizeof(buf), "PASV\r\n");
    write(ctrl, buf, strlen(buf));
    if (read_response(ctrl, buf, sizeof(buf)) != 227) {
        fprintf(stderr, "PASV command failed\n");
        exit(-1);
    }
    // Parse PASV reply
    char *start = strchr(buf, '(');
    char *end   = strchr(buf, ')');
    if (!start || !end || end < start) {
        fprintf(stderr, "Invalid PASV response\n");
        exit(-1);
    }
    int h1,h2,h3,h4,p1,p2;
    if (sscanf(start+1, "%d,%d,%d,%d,%d,%d", &h1,&h2,&h3,&h4,&p1,&p2) != 6) {
        fprintf(stderr, "Failed to parse PASV address\n");
        exit(-1);
    }
    char data_ip[64];
    snprintf(data_ip, sizeof(data_ip), "%d.%d.%d.%d", h1,h2,h3,h4);
    int data_port = p1*256 + p2;

    // Connect data socket
    int data;
    struct sockaddr_in data_addr;
    bzero(&data_addr, sizeof(data_addr));
    data_addr.sin_family = AF_INET;
    data_addr.sin_port = htons(data_port);
    inet_aton(data_ip, &data_addr.sin_addr);
    if ((data = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        perror("socket(data)"); exit(-1);
    }
    if (connect(data, (struct sockaddr *)&data_addr, sizeof(data_addr)) < 0) {
        perror("connect(data)"); exit(-1);
    }

    // Request file
    snprintf(buf, sizeof(buf), "RETR %s\r\n", path);
    write(ctrl, buf, strlen(buf));
    int retr_code = read_response(ctrl, buf, sizeof(buf));
    if (retr_code != 150 && retr_code != 125) {
        fprintf(stderr, "RETR command failed (code %d)\n", retr_code);
        exit(-1);
    }

    // Open local file for writing
    char *fname = strrchr(path, '/');
    fname = fname ? fname+1 : path;
    FILE *f = fopen(fname, "wb");
    if (!f) { perror("fopen"); exit(-1); }

    // Transfer data
    ssize_t n;
    while ((n = read(data, buf, sizeof(buf))) > 0) {
        fwrite(buf, 1, n, f);
    }
    if (n < 0) perror("read(data)");
    fclose(f);
    close(data);

    // Transfer complete reply
    if (read_response(ctrl, buf, sizeof(buf)) != 226) {
        fprintf(stderr, "Transfer not properly completed\n");
        exit(-1);
    }

    // QUIT
    snprintf(buf, sizeof(buf), "QUIT\r\n");
    write(ctrl, buf, strlen(buf));
    read_response(ctrl, buf, sizeof(buf));
    close(ctrl);

    printf("Downloaded '%s' successfully.\n", fname);
    return 0;
}
