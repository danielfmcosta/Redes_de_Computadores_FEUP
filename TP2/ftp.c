/*
 * Simple FTP Client based on FEUP examples:
 *   - clientTCP.c (basic TCP connect/send)
 *   - getip.c (DNS resolution via gethostbyname)
 *
 * Usage:
 *   ./ftp_client_simple ftp://<host>/<path>
 *
 * Supports:
 *   - Anonymous login (USER/PASS)
 *   - Binary mode (TYPE I)
 *   - Passive mode data transfer (PASV + RETR)
 *   - Saves file to current directory
*/

/// OBjectivo , com o url do site ftp conectar ao servidor, e fazer download do ficheiro indicado na command line

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

// Read a line (ending in '\n') from sock into buf (null-terminated)
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

int main(int argc, char *argv[]) {
    if (argc != 2) {
        fprintf(stderr, "Usage: %s ftp://<host>/<path>\n", argv[0]);
        exit(-1);
    }
    
    // Parse URL: ftp://<host>/<path>
    char *url = argv[1];
    const char *prefix = "ftp://";
    if (strncmp(url, prefix, strlen(prefix)) != 0) {
        fprintf(stderr, "URL must start with ftp://\n");
        exit(-1);
    }
    char *host_path = url + strlen(prefix);
    char *slash = strchr(host_path, '/');
    if (!slash) {
        fprintf(stderr, "URL must contain a path after host\n");
        exit(-1);
    }
    *slash = '\0';
    char *host = host_path;
    char *path = slash + 1;

    // Resolve host
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
    // Read welcome
    read_line(ctrl, buf, sizeof(buf));
    printf("< %s", buf);

    // Send USER
    snprintf(buf, sizeof(buf), "USER anonymous\r\n");
    write(ctrl, buf, strlen(buf));
    read_line(ctrl, buf, sizeof(buf));
    printf("< %s", buf);

    // Send PASS
    snprintf(buf, sizeof(buf), "PASS anonymous@\r\n");
    write(ctrl, buf, strlen(buf));
    read_line(ctrl, buf, sizeof(buf));
    printf("< %s", buf);

    // Set binary mode
    snprintf(buf, sizeof(buf), "TYPE I\r\n");
    write(ctrl, buf, strlen(buf));
    read_line(ctrl, buf, sizeof(buf));
    printf("< %s", buf);

    // Enter passive mode
    snprintf(buf, sizeof(buf), "PASV\r\n");
    write(ctrl, buf, strlen(buf));
    read_line(ctrl, buf, sizeof(buf));
    printf("< %s", buf);
    int h1,h2,h3,h4,p1,p2;
    char *p = strchr(buf, '(');
    if (!p || sscanf(p+1, "%d,%d,%d,%d,%d,%d", &h1,&h2,&h3,&h4,&p1,&p2) != 6) {
        fprintf(stderr, "Failed to parse PASV response\n"); exit(-1);
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
    read_line(ctrl, buf, sizeof(buf));
    printf("< %s", buf);

    // Open local file
    char *fname = strrchr(path, '/');
    fname = fname ? fname+1 : path;
    FILE *f = fopen(fname, "wb");
    if (!f) { perror("fopen"); exit(-1); }

    // Read data
    ssize_t n;
    while ((n = read(data, buf, sizeof(buf))) > 0) {
        fwrite(buf, 1, n, f);
    }
    if (n < 0) perror("read(data)");
    fclose(f);
    close(data);

    // Read final reply
    read_line(ctrl, buf, sizeof(buf));
    printf("< %s", buf);

    // Quit
    snprintf(buf, sizeof(buf), "QUIT\r\n");
    write(ctrl, buf, strlen(buf));
    read_line(ctrl, buf, sizeof(buf));
    printf("< %s", buf);
    close(ctrl);

    printf("Downloaded '%s' successfully.\n", fname);
    return 0;
}
 