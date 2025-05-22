/*
 * ftp_client_simple.c
 * -------------------
 * Simple FTP Client structured into clear pipeline functions.
 *
 * Objetivo:
 *   Receber URL FTP ([user:pass@]host/path), conectar, fazer login,
 *   passar para modo binário e passivo, transferir o ficheiro, e guardar localmente.
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

ssize_t read_line(int sock, char *buf, size_t maxlen);
int read_response(int sock, char *buf, size_t buflen);
void parse_url(const char *url, char *user, char *pass, char *host, char *path);
int ftp_connect(const char *server_ip, int port);
void ftp_login(int ctrl, const char *user, const char *pass, char *buf);
void ftp_set_binary(int ctrl, char *buf);
void ftp_enter_passive(int ctrl, char *buf, char *data_ip, int *data_port);
void ftp_retrieve(int ctrl, int data, const char *path, char *buf);

/** Read a line ending in '\n' */
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

/** Read full FTP response, handling multi-line banners */
int read_response(int sock, char *buf, size_t buflen) {
    char code[4];
    ssize_t n = read_line(sock, buf, buflen);
    if (n <= 0) return -1;
    printf("< %s", buf);

    memcpy(code, buf, 3);
    code[3] = '\0';
    int multiline = (buf[3] == '-');
    while (multiline) {
        n = read_line(sock, buf, buflen);
        if (n <= 0) return -1;
        printf("< %s", buf);
        if (strncmp(buf, code, 3) == 0 && buf[3] == ' ') multiline = 0;
    }
    return atoi(code);
}

/** Parse ftp://[user:pass@]host/path */
void parse_url(const char *url, char *user, char *pass, char *host, char *path) {
    if (strncmp(url, "ftp://", 6) != 0) {
        fprintf(stderr, "URL must start with ftp://\n"); exit(-1);
    }
    char *p = strdup(url + 6);
    char *slash = strchr(p, '/');
    if (!slash) { fprintf(stderr, "URL missing path\n"); exit(-1); }
    *slash = '\0'; strcpy(path, slash + 1);

    // Default anon
    strcpy(user, "anonymous"); strcpy(pass, "anonymous@");
    char *cred = p;
    char *at = strchr(cred, '@');
    if (at) {
        *at = '\0'; strcpy(host, at + 1);
        char *colon = strchr(cred, ':');
        if (colon) { *colon = '\0'; strcpy(user, cred); strcpy(pass, colon + 1); }
        else strcpy(user, cred);
    } else strcpy(host, cred);
    free(p);
}

/** Establish TCP connection to server_ip:port */
int ftp_connect(const char *server_ip, int port) {
    int sock;
    struct sockaddr_in addr;
    bzero(&addr, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    inet_aton(server_ip, &addr.sin_addr);
    if ((sock = socket(AF_INET, SOCK_STREAM, 0)) < 0) { perror("socket"); exit(-1); }
    if (connect(sock, (struct sockaddr*)&addr, sizeof(addr)) < 0) { perror("connect"); exit(-1); }
    return sock;
}

void ftp_login(int ctrl, const char *user, const char *pass, char *buf) {
    snprintf(buf, BUF_SIZE, "USER %s\r\n", user);
    write(ctrl, buf, strlen(buf));
    if (read_response(ctrl, buf, BUF_SIZE) != 331) { fprintf(stderr,"USER failed\n"); exit(-1); }
    snprintf(buf, BUF_SIZE, "PASS %s\r\n", pass);
    write(ctrl, buf, strlen(buf));
    if (read_response(ctrl, buf, BUF_SIZE) != 230) { fprintf(stderr,"PASS failed\n"); exit(-1); }
}

void ftp_set_binary(int ctrl, char *buf) {
    snprintf(buf, BUF_SIZE, "TYPE I\r\n"); write(ctrl, buf, strlen(buf));
    if (read_response(ctrl, buf, BUF_SIZE) != 200) { fprintf(stderr,"TYPE failed\n"); exit(-1); }
}

void ftp_enter_passive(int ctrl, char *buf, char *data_ip, int *data_port) {
    snprintf(buf, BUF_SIZE, "PASV\r\n"); write(ctrl, buf, strlen(buf));
    if (read_response(ctrl, buf, BUF_SIZE) != 227) { fprintf(stderr,"PASV failed\n"); exit(-1); }
    char *start = strchr(buf,'('), *end = strchr(buf,')');
    int h1,h2,h3,h4,p1,p2;
    sscanf(start+1, "%d,%d,%d,%d,%d,%d",&h1,&h2,&h3,&h4,&p1,&p2);
    snprintf(data_ip,64,"%d.%d.%d.%d",h1,h2,h3,h4);
    *data_port = p1*256 + p2;
}

void ftp_retrieve(int ctrl, int data, const char *path, char *buf) {
    snprintf(buf, BUF_SIZE, "RETR %s\r\n", path); write(ctrl, buf, strlen(buf));
    int code = read_response(ctrl, buf, BUF_SIZE);
    if (code!=150 && code!=125) { fprintf(stderr,"RETR failed %d\n",code); exit(-1); }
    const char *fname = strrchr(path,'/'); fname = fname?fname+1:path;
    FILE *f = fopen(fname,"wb"); if(!f){perror("fopen");exit(-1);}  
    ssize_t n; while((n=read(data,buf,BUF_SIZE))>0) fwrite(buf,1,n,f);
    fclose(f); close(data);
    if (read_response(ctrl, buf, BUF_SIZE)!=226) { fprintf(stderr,"Transfer incomplete\n"); exit(-1);}  
    printf("Downloaded '%s'.\n", fname);
}

int main(int argc, char *argv[]) {
    if (argc!=2) { fprintf(stderr,"Usage: %s ftp://[user:pass@]host/path\n",argv[0]); return -1; }
    char user[64], pass[64], host[128], path[256], buf[BUF_SIZE];
    parse_url(argv[1], user, pass, host, path);

    // DNS lookup
    struct hostent *h = gethostbyname(host);
    char server_ip[INET_ADDRSTRLEN]; inet_ntop(AF_INET,h->h_addr_list[0],server_ip,sizeof(server_ip));

    // Control connection + greeting
    int ctrl = ftp_connect(server_ip, CONTROL_PORT);
    if (read_response(ctrl, buf, BUF_SIZE)<0) exit(-1);

    ftp_login(ctrl, user, pass, buf);
    ftp_set_binary(ctrl, buf);

    // Passive + data connect
    char data_ip[64]; int data_port;
    ftp_enter_passive(ctrl, buf, data_ip, &data_port);
    int data = ftp_connect(data_ip, data_port);

    // Retrieve file
    ftp_retrieve(ctrl, data, path, buf);

    // Quit
    write(ctrl,"QUIT\r\n",6);
    read_response(ctrl, buf, BUF_SIZE);
    close(ctrl);
    return 0;
}
