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

#define BUF_SIZE 512
#define MAX_BUF_SIZE BUF_SIZE*2
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
int Nr = 1;

#define sleep_time 1

// calculate the size of the frame if it ends in a FLAG
int get_frame_length(unsigned char *frame) {
    // find the first and last occurrence of 0x7e
    int first_flag_index = 0;
    int last_flag_index = 0;
    
    // find first flag
    for (int i = 0; i < MAX_BUF_SIZE; i++) {
        if (frame[i] == FLAG) {
            first_flag_index = i;
            break;
        }
    }
    
    // find last flag
    for (int i = MAX_BUF_SIZE - 1; i > first_flag_index; i--) {
        if (frame[i] == FLAG) {
            last_flag_index = i;
            break;
        }
    }
    
    // calculate the size between the first and last flags
    return last_flag_index - first_flag_index + 1;
}

int write_frame(unsigned char *frame) {
    int frameSize = get_frame_length(frame);
    int written = write(global_fd, frame, frameSize);
    return written;
}

void print_array(unsigned char *argv, int size) 
{   
    for (int i = 0; i < size; i++) {
        printf("0x%02X ", argv[i]);
    }
    printf("\n");
}

void alarmHandler(int signal)
{
    if (ESTABLISHMENT == FALSE) {
        alarmEnabled = FALSE;
        alarmCount++;
    }
}

int get_BCC2(const unsigned char *argv, int size){
    int BCC2 = argv[0];
    for (int i = 1; i < size; i++){
        BCC2 ^= argv[i];
    }
    return BCC2;
}

unsigned char* byte_stuffing(unsigned char *frame, int inputLength) {
    static unsigned char stuffed[MAX_BUF_SIZE];
    memset(stuffed, 0, MAX_BUF_SIZE); // Clear the buffer
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

unsigned char* byte_destuffing(unsigned char *argv, int inputLength) {
    static unsigned char destuffed[BUF_SIZE] = {0};
    memset(destuffed, 0, BUF_SIZE); // Clear the buffer
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

void send_SET(int fd){
    // Send SET
    const unsigned char SET_FRAME[BUF_SIZE_SET] = { FLAG, A_SET, C_SET, BCC1_SET, FLAG };
    write(fd, SET_FRAME, BUF_SIZE_SET);
    sleep(sleep_time);
}

void send_UA(int fd){
    // Send UA
    const unsigned char UA_FRAME[BUF_SIZE_UA] = { FLAG, A_UA, C_UA, BCC1_UA, FLAG };
    write(fd, UA_FRAME, BUF_SIZE_UA);
    sleep(sleep_time);
}

void send_DISC(int fd){
    // Send DISC
    const unsigned char DISC_FRAME[BUF_SIZE_DISC] = { FLAG, A_DISC, C_DISC, BCC1_DISC, FLAG };
    write(fd, DISC_FRAME, BUF_SIZE_DISC);
    sleep(sleep_time);
}

void send_reply(int fd, int reply){
    if(reply == C_RR_0){
        const unsigned char RR0_FRAME[BUF_SIZE_REPLY] = { FLAG, A, C_RR_0, BCC1_RR_0, FLAG };
        write(fd, RR0_FRAME, BUF_SIZE_REPLY);
        printf("RR0 send!\n\n");
        sleep(sleep_time);
    }else if(reply == C_RR_1){
        const unsigned char RR1_FRAME[BUF_SIZE_REPLY] = { FLAG, A, C_RR_1, BCC1_RR_1, FLAG };
        write(fd, RR1_FRAME, BUF_SIZE_REPLY);
        printf("RR1 send!\n\n");
        sleep(sleep_time);
    }else if(reply == C_REJ_0){
        const unsigned char REJ0_FRAME[BUF_SIZE_REPLY] = { FLAG, A, C_REJ_0, BCC1_REJ_0, FLAG };
        write(fd, REJ0_FRAME, BUF_SIZE_REPLY);
        printf("REJ0 send!\n\n");
        sleep(sleep_time);
    }else if(reply == C_REJ_1){
        const unsigned char REJ1_FRAME[BUF_SIZE_REPLY] = { FLAG, A, C_REJ_1, BCC1_REJ_1, FLAG };
        write(fd, REJ1_FRAME, BUF_SIZE_REPLY);
        printf("REJ1 send!\n\n");
        sleep(sleep_time);
    }
}  

// All read_ functions return 0 if it fails, 1 if it succeeds
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
                    //printf("SET received!\n"); 
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
                    //printf("UA received!\n"); 
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
                    //printf("DISC received!\n"); 
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

//Returns 0 if it fails and res if it succeeds  (C_RR0,  C_RR1, C_REJ0, C_REJ1)
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
                if (buf == (reply[1] ^ reply[2])) {  // BCC1 check
                    reply[state] = buf;
                    state = 4;
                } else { //BCC1 incorrect so I must ignore with out any action
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

// NEEDS UPDATE
//Returns 0 if it fails and the size of the frame read
int read_I(int fd, unsigned char *frame)  {
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
                    printf("FLAG? 0x%02X\n", buf);
                    state = 0;
                    return 0;
                }
                break;
            
            case 1: 
                if (buf == A) {
                    frame[state] = buf;
                    state = 2;
                } else {
                    printf("A? 0x%02X\n", buf);
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
                    printf("C? 0x%02X\n", buf);
                    state = 0;
                    return 0;
                }
                break;

            case 3: 
                if (buf == (frame[1] ^ frame[2])) {  
                    frame[state] = buf;
                    state = 4;
                }  else {
                    printf("BCC1? 0x%02X\n", buf);
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
    printf("Error: Frame too long\n");
    return 0;
}

////////////////////////////////////////////////
// LLOPEN
////////////////////////////////////////////////
int llopen(LinkLayer connectionParameters)
{
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

    // convert baud rate
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
    
    // adjust timeout settings
    newtio.c_cc[VTIME] = connectionParameters.timeout * 10;
    newtio.c_cc[VMIN] = 0;


    tcflush(global_fd, TCIOFLUSH);
    if (tcsetattr(global_fd, TCSANOW, &newtio) == -1)
    {
        perror("tcsetattr");
        return -1;
    }

       
    if (connectionParameters.role == LlTx) {
        // TRANSMITTER
        (void)signal(SIGALRM, alarmHandler);

        send_SET(global_fd);
        printf("SET sent!\n");

        if (read_UA(global_fd) == 1) {
            ESTABLISHMENT = TRUE;
            printf("UA received, connection established!\n");
            return 1;
        }

        alarmCount = 0;
        while (alarmCount < connectionParameters.nRetransmissions && ESTABLISHMENT == FALSE) {
            if (alarmEnabled == FALSE) {       
                send_SET(global_fd);
                printf("Timeout occured, will resend SET!\n");
                alarm(connectionParameters.timeout);  
                alarmEnabled = TRUE;
                //sleep(sleep_time); 

                if (read_UA(global_fd) == 1) {
                    ESTABLISHMENT = TRUE;
                    alarm(0); 
                    printf("UA received, connection established!\n");
                    return 1;
                }
            }
        }

        printf("ERROR: Connection failed after 3 retries.\n");
        return -1;
    }

    if (connectionParameters.role == LlRx) {
        // RECEIVER:
        while (1) {
            if (read_SET(global_fd)) {
                send_UA(global_fd);
                printf("SET received, UA sent. Connection established.\n");
                ESTABLISHMENT = TRUE;
                return 1;
            }
        }
    }

    return 1;
}

////////////////////////////////////////////////
// LLWRITE
////////////////////////////////////////////////
// Send data in buf with size bufSize.
// Return number of chars written, or "-1" on error.
int llwrite(const unsigned char *buf, int bufSize)
{
    unsigned char frame[bufSize + 6];
    frame[0] = FLAG;         
    frame[1] = A;           
    frame[2] = (Ns == 0) ? C_0 : C_1;    
    frame[3] = frame[1] ^ frame[2]; 
    memcpy(&frame[4], buf, bufSize);   
    frame[4 + bufSize] = get_BCC2(buf, bufSize);
    frame[5 + bufSize] = FLAG; 

    printf("\nFrame: ");
    for (int i = 0; i < 6; i++) {
        printf("0x%02X ", frame[i]);
    }
    for (int i = bufSize; i < bufSize + 6; i++) {
        printf("0x%02X ", frame[i]);
    }
    
    unsigned char *stuffed_buf = byte_stuffing(frame, bufSize + 6);
    int stuffed_size = get_frame_length(stuffed_buf);
    printf("Stuffed Frame length: %d\n", stuffed_size);
    int retries = 0;
    int written;
    (void)signal(SIGALRM, alarmHandler);

    while (retries < 3) {
        printf("[llwrite] Sending stuffed frame: ");
        //print_array(stuffed_buf);
        written = write_frame(stuffed_buf); 
        if (written == -1) {
            printf("Error: Failed to write frame\n");
            return -1;
        }
        
        alarm(3);
        alarmEnabled = 1;
        int response = read_Reply(global_fd);

        if (response == C_RR_0 || response == C_RR_1) {
            printf("RR%d received\n", (response == C_RR_0) ? 0 : 1);
            Ns = (response == C_RR_0) ? 0 : 1;
            alarm(0);
            return written;
        } else if (response == C_REJ_0 || response == C_REJ_1) {
            printf("REJ%d received\n", (response == C_REJ_0) ? 0 : 1);
            retries++;
        } else {
            printf("Error: Failed to read reply or unexpected response\n");
            retries++;
        }
    }

    printf("Error: Max retransmissions reached. Frame lost.\n");
    return -1;
}

////////////////////////////////////////////////
// LLREAD
////////////////////////////////////////////////
// Receive data in packet.
// Return number of chars read, or "-1" on error.
int llread(unsigned char *packet) {
    unsigned char stuffed_frame[BUF_SIZE] = {0};
    int frame_size = read_I(global_fd, stuffed_frame);
    printf("Frame from read_I size: %d\n", frame_size);
    if (frame_size == 0) {
        printf("Error: Failed to read I frame\n");
        return -1;
    }

    unsigned char *destuffed_frame = byte_destuffing(stuffed_frame, frame_size);

    printf("\nDestuffed frame: ");
    int frame_length = get_frame_length(destuffed_frame);
    
    for (int i = 0; i < 6; i++) {
        printf("0x%02X ", destuffed_frame[i]);
    }
    for (int i = frame_length; i < frame_length + 6; i++) {
        printf("0x%02X ", destuffed_frame[i]);
    }

    printf("Destuffed Frame length: %d\n", frame_length);


    if (frame_length <= 6) {
        // Must be at least 6 bytes: FLAG, A, C, BCC1, BCC2, FLAG.
        printf("Error: Frame too short (%d bytes)\n", frame_length);
        return -1;
    }

    // Verify BCC2:
    // Compute BCC2 over the payload bytes. According to our frame format, payload is from index 4 to (frame_length - 3).
    int computed_BCC2 = get_BCC2(&destuffed_frame[4], get_frame_length(destuffed_frame) - 6);
    
    // The received BCC2 is located at index frame_length - 2.
    unsigned char received_BCC2 = destuffed_frame[frame_length - 2];
    
    if (computed_BCC2 != received_BCC2) {
        printf("BCC2 error: computed 0x%X, received 0x%X\n", computed_BCC2, received_BCC2);
        // Send REJ depending on the control field.
        if (destuffed_frame[2] == C_0) {
            send_reply(global_fd, C_REJ_0); // Send REJ0
            llread(packet);
        } else if (destuffed_frame[2] == C_1) {
            send_reply(global_fd, C_REJ_1); // Send REJ1
            llread(packet);
        }
        return -1;
    }
    
    int payload_size = frame_length - 6;  
    for (int i = 0; i < payload_size; i++) {
        packet[i] = destuffed_frame[i + 4];
    }
    
    if (destuffed_frame[2] == C_0) {
        send_reply(global_fd, C_RR_1); // RR1
    } else if (destuffed_frame[2] == C_1) {
        send_reply(global_fd, C_RR_0); // RR0
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
        // Send DISC
        send_DISC(global_fd);

        // Read DISC
        if(read_DISC(global_fd)==0){
            printf("Error DISC!\n");
            return -1;
        }
        
        // Send UA
        send_UA(global_fd);   
    }else if(global_connectionParameters.role == LlRx){
        // Read DISC
        if(read_DISC(global_fd)==0){
            printf("Error DISC\n");
            return -1;
        }
        // Send DISC
        send_DISC(global_fd);
        // Read UA
        if(read_UA(global_fd)==0){
            printf("Error UA\n");
            return -1;
        }
    }

    printf("\nEnding link-layer protocol application\n");

    close(global_fd);
    return 1;
}
