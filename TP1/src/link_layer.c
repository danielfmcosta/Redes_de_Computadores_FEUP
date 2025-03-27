// Link layer protocol implementation

#include "link_layer.h"

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <termios.h>
#include <unistd.h>

#include <signal.h>

LinkLayer global_connectionParameters;
int global_fd;

// MISC
#define _POSIX_SOURCE 1 // POSIX compliant source

#define FALSE 0
#define TRUE 1

#define sleep_time 1

// Buffer sizes
#define BUF_SIZE 512
#define MAX_BUF_SIZE BUF_SIZE*2

// Frame constants
#define FLAG 0x7E
#define ESCAPE 0x7D
#define A 0x03
#define C_0 0x00
#define C_1 0x40

//SET constants
#define BUF_SIZE_SET 5
#define A_SET 0x03
#define C_SET 0x03
#define BCC1_SET A_SET^C_SET

//UA constants
#define BUF_SIZE_UA 5
#define A_UA 0x03
#define C_UA 0x07
#define BCC1_UA A_UA^C_UA

//DISC constants
#define BUF_SIZE_DISC 5
#define A_DISC 0x03
#define C_DISC 0x0B
#define BCC1_DISC A_DISC^C_DISC 

//Reply Size
#define BUF_SIZE_REPLY 5
#define A_REPLY 0x03

//RR0 constants
#define C_RR_0 0x05
#define BCC1_RR_0 A^C_RR_0

//RR1 constants
#define C_RR_1 0x85
#define BCC1_RR_1 A^C_RR_1

//REJ0 constants
#define C_REJ_0 0x01
#define BCC1_REJ_0 A^C_REJ_0

//REJ1 constants
#define C_REJ_1 0x81
#define BCC1_REJ_1 A^C_REJ_1

volatile int ESTABLISHMENT = FALSE;
volatile int WRITE = FALSE;

int alarmEnabled = FALSE;
int alarmCount = 0;

int Ns = 0;

// Calcula o tamanho do frame, se e só se a frame acaber com uma FLAG
int get_frame_length(unsigned char *frame) {
    int first_FLAG= 0;
    int last_FLAG = 0;
    
    // Encontra a primeira FLAG
    for (int i = 0; i < MAX_BUF_SIZE; i++) {
        if (frame[i] == FLAG) {
            first_FLAG = i;
            break;
        }
    }
    
    // Encontra a ultima FLAG
    for (int i = MAX_BUF_SIZE - 1; i > first_FLAG; i--) {
        if (frame[i] == FLAG) {
            last_FLAG = i;
            break;
        }
    }

    return last_FLAG - first_FLAG + 1;
}

// Escreve um frame
int write_frame(unsigned char *frame) {
    int frame_Size = get_frame_length(frame);
    int written = write(global_fd, frame, frame_Size);
    return written;
}

// Imprime um array
void print_array(unsigned char *argv, int size) 
{   
    for (int i = 0; i < size; i++) {
        printf("0x%02X ", argv[i]);
    }
    printf("\n");
}

// Função que trata do sinal de alarme
void alarmHandler(int signal)
{
    if (ESTABLISHMENT == FALSE) {
        alarmEnabled = FALSE;
        alarmCount++;
    }
}

// Função que calcula o BCC2
int get_BCC2(const unsigned char *argv, int size){
    int BCC2 = argv[0];
    for (int i = 1; i < size; i++){
        BCC2 ^= argv[i];
    }
    return BCC2;
}

// Função de byte stuffing
unsigned char* byte_stuffing(unsigned char *frame, int inputLength) {
    static unsigned char stuffed[MAX_BUF_SIZE];
    memset(stuffed, 0, MAX_BUF_SIZE); // Dá clear ao buffer (evitar "lixo")
    int i, j = 0;

    stuffed[j++] = frame[0];
    for (i = 1; i < inputLength - 1; i++) {
        if (frame[i] == FLAG || frame[i] == ESCAPE) {
            stuffed[j++] = ESCAPE;
            stuffed[j++] = frame[i] ^ 0x20;
        } else {
            stuffed[j++] = frame[i];
        }
    }

    stuffed[j] = frame[inputLength - 1];
    return stuffed;
}

// Função de byte destuffing
unsigned char* byte_destuffing(unsigned char *argv, int inputLength) {
    static unsigned char destuffed[BUF_SIZE] = {0};
    memset(destuffed, 0, BUF_SIZE); // Dá clear ao buffer (evitar "lixo")
    int i, j = 0;

    destuffed[j++] = argv[0];
    for (i = 1; i < inputLength - 1; i++) {
        if (argv[i] == ESCAPE) {
            i++;
            destuffed[j++] = argv[i] ^ 0x20;
        } else {
            destuffed[j++] = argv[i];
        }
    }

    destuffed[j] = argv[inputLength - 1];
    return destuffed;
}

// Função que envia SET
void send_SET(int fd){
    const unsigned char SET_FRAME[BUF_SIZE_SET] = {FLAG, A_SET, C_SET, BCC1_SET, FLAG};
    write(fd, SET_FRAME, BUF_SIZE_SET);
    sleep(sleep_time);
}

// Função que envia UA
void send_UA(int fd){
    const unsigned char UA_FRAME[BUF_SIZE_UA] = {FLAG, A_UA, C_UA, BCC1_UA, FLAG};
    write(fd, UA_FRAME, BUF_SIZE_UA);
    sleep(sleep_time);
}

// Função que envia DISC
void send_DISC(int fd){
    const unsigned char DISC_FRAME[BUF_SIZE_DISC] = {FLAG, A_DISC, C_DISC, BCC1_DISC, FLAG};
    write(fd, DISC_FRAME, BUF_SIZE_DISC);
    sleep(sleep_time);
}

// Função que envia Reply (RR0, RR1, REJ0, REJ1)
void send_reply(int fd, int reply){
    if(reply == C_RR_0){
        const unsigned char RR0_FRAME[BUF_SIZE_REPLY] = { FLAG, A, C_RR_0, BCC1_RR_0, FLAG };
        write(fd, RR0_FRAME, BUF_SIZE_REPLY);
        sleep(sleep_time);
    }else if(reply == C_RR_1){
        const unsigned char RR1_FRAME[BUF_SIZE_REPLY] = { FLAG, A, C_RR_1, BCC1_RR_1, FLAG };
        write(fd, RR1_FRAME, BUF_SIZE_REPLY);
        sleep(sleep_time);
    }else if(reply == C_REJ_0){
        const unsigned char REJ0_FRAME[BUF_SIZE_REPLY] = { FLAG, A, C_REJ_0, BCC1_REJ_0, FLAG };
        write(fd, REJ0_FRAME, BUF_SIZE_REPLY);
        sleep(sleep_time);
    }else if(reply == C_REJ_1){
        const unsigned char REJ1_FRAME[BUF_SIZE_REPLY] = { FLAG, A, C_REJ_1, BCC1_REJ_1, FLAG };
        write(fd, REJ1_FRAME, BUF_SIZE_REPLY);
        sleep(sleep_time);
    }
}  

// Função que lê SET. Retorna 0 em caso de erro e 1 em caso de sucesso
int read_SET(int fd) {
    unsigned char buf;
    int state = 0;

    while (state < BUF_SIZE_SET) {
        int bytesRead = read(fd, &buf, 1);
        if (bytesRead <= 0) return 0;

        switch (state) {
            case 0: 
                if (buf == FLAG){
                    state = 1; 
                } else {
                    state = 0;
                    return 0;
                }
                break;
            case 1: 
                if (buf == A_SET){ 
                    state = 2; 
                } else{
                    state = 0;
                    return 0;
                }
                break;
            case 2: 
                if (buf == C_SET){
                    state = 3;
                } else{
                    state = 0;
                    return 0;
                }
                break;
            case 3: 
                if (buf == (BCC1_SET)){
                    state = 4; 
                } else{
                    state = 0;
                    return 0;
                }
                break;
            case 4: 
                if (buf == FLAG){
                    return 1; 
                } else{
                    state = 0;
                    return 0;
                }
                break;
        }
    }
    return 0;
}

// Função que lê UA. Retorna 0 em caso de erro e 1 em caso de sucesso
int read_UA(int fd){
    unsigned char buf;
    int state = 0;
    while (state < BUF_SIZE_UA) {

        int bytesRead = read(fd, &buf, 1);
        if (bytesRead <= 0) return 0;

        switch (state) {
            case 0: 
                if (buf == FLAG){
                    state = 1; 
                } else {
                    state = 0;
                    return 0;
                }
                break;
            case 1: 
                if (buf == A_UA){ 
                    state = 2; 
                } else {
                    state = 0;
                    return 0;
                }
                break;
            case 2: 
                if (buf == C_UA){
                    state = 3;
                } else {
                    state = 0;
                    return 0;
                }
                break;
            case 3: 
                if (buf == (BCC1_UA)){
                    state = 4; 
                } else {
                    state = 0;
                    return 0;
                }
                break;
            case 4: 
                if (buf == FLAG){
                    return 1; 
                } else {
                    state = 0;
                    return 0;
                }
                break;
        }
    }
    return 0;
}

// Função que lê DISC. Retorna 0 em caso de erro e 1 em caso de sucesso
int read_DISC(int fd){
    unsigned char buf;
    int state = 0;

    while (state < BUF_SIZE_DISC) {
        int bytesRead = read(fd, &buf, 1);
        if (bytesRead <= 0) return 0;

        switch (state) {
            case 0: 
                if (buf == FLAG){
                    state = 1; 
                } else {
                    state = 0;
                    return 0;
                }
                break;
            case 1: 
                if (buf == A_DISC){ 
                    state = 2; 
                } else {
                    state = 0;
                    return 0;
                }
                break;
            case 2: 
                if (buf == C_DISC){
                    state = 3;
                } else {
                    state = 0;
                    return 0;
                }
                break;
            case 3: 
                if (buf == (BCC1_DISC)){
                    state = 4; 
                } else {
                    state = 0;
                    return 0;
                }
                break;
            case 4: 
                if (buf == FLAG){
                    return 1; 
                } else {
                    state = 0;
                    return 0;
                }
                break;
        }
    }
    return 0;
}

// Função que lê Reply. Retorna 0 em caso de erro e (RR0, RR1, REJ0, REJ1) em caso de sucesso
int read_Reply(int fd) {
    unsigned char buf;
    int state = 0;
    unsigned char reply[BUF_SIZE_REPLY] = {0};
    int res =0;

    while (state < BUF_SIZE_REPLY) {
        int bytesRead = read(fd, &buf, 1);
        if (bytesRead <= 0) return 0;

        switch (state) {
            case 0:
                if (buf == FLAG) {
                    reply[state] = buf;
                    state = 1;
                } else {
                    state = 0;
                    return 0;
                }
                break;
            
            case 1:
                if (buf == A_REPLY) {
                    reply[state] = buf;
                    state = 2;
                } else {
                    state = 0;
                    return 0;
                }
                break;

            case 2:
                if (buf == C_RR_0) {
                    reply[state] = buf;
                    state = 3;
                    res=C_RR_0;
                } else if(buf == C_RR_1) {
                    reply[state] = buf;
                    state = 3;
                    res=C_RR_1;
                } else if(buf == C_REJ_0) {
                    reply[state] = buf;
                    state = 3;
                    res=C_REJ_0;
                }else if(buf == C_REJ_1) {
                    reply[state] = buf;
                    state = 3;
                    res=C_REJ_1;
                } else {
                    state = 0;
                    return 0;
                }
                break;

            case 3:
                if (buf == (reply[1] ^ reply[2])) {
                    reply[state] = buf;
                    state = 4;
                } else {
                    state = 0;
                    return 0;
                }
                break;

            case 4:
                if (buf == FLAG) {
                    reply[state] = buf;
                    state = 0;
                    return res;
                } else {
                    state = 0;
                    return 0;
                }
                break;
        }
    }
    return 0;
}

// Função que lê I frame. Retorna 0 em caso de erro e o tamanho do frame em caso de sucesso
int read_I(int fd, unsigned char *frame) {
    unsigned char buf;
    int state = 0;
    int res;
    int dataIndex = 0;

    while (state+dataIndex < MAX_BUF_SIZE) {
        res = read(fd, &buf, 1);
        if (res <= 0) return 0;

        switch (state) {
            case 0: 
                if (buf == FLAG) {
                    frame[state] = buf;
                    state = 1;
                } else {
                    state = 0;
                    return 0;
                }
                break;
            
            case 1: 
                if (buf == A) {
                    frame[state] = buf;
                    state = 2;
                } else {
                    state = 0;
                    return 0;
                }
                break;

            case 2: 
                if (buf == C_0) {
                    frame[state] = buf;
                    state = 3;

                } else if (buf == C_1) {
                    frame[state] = buf;
                    state = 3;
                } else {
                    state = 0;
                    return 0;
                }
                break;

            case 3: 
                if (buf == (frame[1] ^ frame[2])) {  
                    frame[state] = buf;
                    state = 4;
                }  else {
                    state = 0;
                    return 0;
                }
                break;

            case 4: 
                if (buf == FLAG) {
                    frame[state + dataIndex] = buf;
                    res = state + dataIndex + 1;
                    state = 0;
                    dataIndex = 0;
                    return res;
                } else {
                    frame[state + dataIndex] = buf;
                    dataIndex++;
                }
                break;
        
        }
    }
    return 0;
}

////////////////////////////////////////////////
// LLOPEN
////////////////////////////////////////////////
// Open a connection using the "port" parameters defined in struct linkLayer.
// Return "1" on success or "-1" on error.
int llopen(LinkLayer connectionParameters) {
    // Interpretar os parametros de ligação (role, baudrate, etc)
    global_fd = open(connectionParameters.serialPort, O_RDWR | O_NOCTTY);
    if (global_fd < 0)
    {
        perror(connectionParameters.serialPort);
        return -1;
    }

    struct termios oldtio, newtio;
    if (tcgetattr(global_fd, &oldtio) == -1)
    {
        perror("tcgetattr");
        return -1;
    }
    memset(&newtio, 0, sizeof(newtio));

    speed_t baud;
    switch (connectionParameters.baudRate) {
        case 9600: baud = B9600; break;
        case 19200: baud = B19200; break;
        case 38400: baud = B38400; break;
        case 57600: baud = B57600; break;
        case 115200: baud = B115200; break;
        default:
            printf("Invalid baud rate: %d\n", connectionParameters.baudRate);
            return -1;
    }
    newtio.c_cflag = baud | CS8 | CLOCAL | CREAD;
    newtio.c_iflag = IGNPAR;
    newtio.c_oflag = 0;
    newtio.c_lflag = 0;
    
    newtio.c_cc[VTIME] = connectionParameters.timeout * 10;
    newtio.c_cc[VMIN] = 0;

    tcflush(global_fd, TCIOFLUSH);
    if (tcsetattr(global_fd, TCSANOW, &newtio) == -1)
    {
        perror("tcsetattr");
        return -1;
    }

    printf("\n 🐧 \n");

    if (connectionParameters.role == LlTx) {
        // Transmissor:
        (void)signal(SIGALRM, alarmHandler);

        // Envia SET
        send_SET(global_fd);

        // Lê UA
        if (read_UA(global_fd) == 1) {
            ESTABLISHMENT = TRUE;
            return 1;
        }

        // Se não recebe UA, reenvia SET, 3 vezes (N_TRIES)
        alarmCount = 0;
        while (alarmCount < connectionParameters.nRetransmissions && ESTABLISHMENT == FALSE) {
            if (alarmEnabled == FALSE) {       
                send_SET(global_fd);
                alarm(connectionParameters.timeout);  
                alarmEnabled = TRUE;

                if (read_UA(global_fd) == 1) {
                    ESTABLISHMENT = TRUE;
                    alarm(0); 
                    return 1;
                }
            }
        }

        printf("ERROR Establishment!\n");
        return -1;
    }

    if (connectionParameters.role == LlRx) {
        // Receiver:
        while (1) {
            // Lê SET, e envia UA
            if (read_SET(global_fd)) {
                send_UA(global_fd);
                ESTABLISHMENT = TRUE;
                return 1;
            }
        }
    }

    return -1;
}

////////////////////////////////////////////////
// LLWRITE
////////////////////////////////////////////////
// Send data in buf with size bufSize.
// Return number of chars written, or "-1" on error.
int llwrite(const unsigned char *buf, int bufSize) {
    // Construir o frame com Dados e BCC2
    unsigned char frame[bufSize + 6];
    frame[0] = FLAG;         
    frame[1] = A;           
    frame[2] = (Ns == 0) ? C_0 : C_1;    
    frame[3] = frame[1] ^ frame[2]; 
    memcpy(&frame[4], buf, bufSize);   
    frame[4 + bufSize] = get_BCC2(buf, bufSize);
    frame[5 + bufSize] = FLAG; 
    
    // Fazer byte stuffing
    unsigned char *stuffed_buf = byte_stuffing(frame, bufSize + 6);
    
    // Enviar o frame stuffed e caso necessário reenviar
    int retries = 0;
    int written;
    (void)signal(SIGALRM, alarmHandler);

    while (retries < 3) {
        written = write_frame(stuffed_buf); 
        if (written == -1) {
            printf("Error! Write Frames!\n");
            return -1;
        }
        
        alarm(3);
        alarmEnabled = 1;
        int response = read_Reply(global_fd);

        // Verificação da resposta em casos como RR0, RR1, REJ0, REJ1
        if (response == C_RR_0 || response == C_RR_1) {
            Ns = (response == C_RR_0) ? 0 : 1;
            alarm(0);
            return written;
        } else if (response == C_REJ_0 || response == C_REJ_1) {
            retries++;
        } else {
            retries++;
        }
    }

    printf("Error: Max Retransmissions!\n");
    return -1;
}

////////////////////////////////////////////////
// LLREAD
////////////////////////////////////////////////
// Receive data in packet.
// Return number of chars read, or "-1" on error.
int llread(unsigned char *packet) {
    // Leitura do frame I
    unsigned char stuffed_frame[BUF_SIZE] = {0};
    int frame_size = read_I(global_fd, stuffed_frame);

    if (frame_size == 0) {
        printf("Error: Read I Frame!\n");
        return -1;
    }

    // Destuffing do frame
    unsigned char *destuffed_frame = byte_destuffing(stuffed_frame, frame_size);
    int frame_length = get_frame_length(destuffed_frame);

    // Caso o frame seja menor que 6 bytes, ou sejam: (FLAG, A, C, BCC1, BCC2, FLAG), é descartado imediatamente
    if (frame_length <= 6) {
        return -1;
    }

    // Verificação do BCC2
    int computed_BCC2 = get_BCC2(&destuffed_frame[4], get_frame_length(destuffed_frame) - 6);
    unsigned char received_BCC2 = destuffed_frame[frame_length - 2];
    
    if (computed_BCC2 != received_BCC2) {
        // Em caso de erro, ou seja, BCC2 não é o esperado, envia REJ0 ou REJ1
        if (destuffed_frame[2] == C_0) {
            send_reply(global_fd, C_REJ_0);
            llread(packet);
        } else if (destuffed_frame[2] == C_1) {
            send_reply(global_fd, C_REJ_1);
            llread(packet);
        }
        return -1;
    }
    
    // Calcula o número de caracteres lidos
    int payload_size = frame_length - 6;  
    for (int i = 0; i < payload_size; i++) {
        packet[i] = destuffed_frame[i + 4];
    }
    
    // Caso o frame seja válido, envia RR0 ou RR1
    if (destuffed_frame[2] == C_0) {
        send_reply(global_fd, C_RR_1);
    } else if (destuffed_frame[2] == C_1) {
        send_reply(global_fd, C_RR_0);
    }
    
    return payload_size;
}

////////////////////////////////////////////////
// LLCLOSE
////////////////////////////////////////////////
// Close previously opened connection.
// if showStatistics == TRUE, link layer should print statistics in the console on close.
// Return "1" on success or "-1" on error.
int llclose(int showStatistics)
{
    if(global_connectionParameters.role == LlTx){
        // Transmiter:

        // Envio DISC
        send_DISC(global_fd);

        // Lê DISC
        if(read_DISC(global_fd)==0){
            return -1;
        }
        
        // Envia UA
        send_UA(global_fd);   
    }
    if(global_connectionParameters.role == LlRx){
        // Receiver:

        // Lê DISC
        if(read_DISC(global_fd)==0){
            return -1;
        }

        // Envia DISC
        send_DISC(global_fd);
        
        // Lê UA
        if(read_UA(global_fd)==0){
            return -1;
        }
    }

    // Finaliza o processo e fecha a ligação
    printf("\n 🐧 \n");
    close(global_fd);
    return 1;
}
