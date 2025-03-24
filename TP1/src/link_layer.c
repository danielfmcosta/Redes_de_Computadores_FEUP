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

#define FLAG 0x7E

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

volatile int ESTABLISHMENT = FALSE;

int alarmEnabled = FALSE;
int alarmCount = 0;

#define sleep_time 1

void alarmHandler(int signal)
{
    if(ESTABLISHMENT==FALSE) {
        alarmEnabled = FALSE;
        alarmCount++;
        
        printf("Alarm #%d\n", alarmCount);
    }
}

void send_SET(int fd){
    // Send SET
    unsigned char buf_SET[BUF_SIZE_SET] = {0};
    buf_SET[0] = FLAG;
    buf_SET[1] = A_SET;
    buf_SET[2] = C_SET;
    buf_SET[3] = BCC1_SET;
    buf_SET[4] = FLAG;
    
    write(fd, buf_SET, BUF_SIZE_SET);
    printf("SET send!\n");
    sleep(sleep_time);
}

int read_UA(int fd){
    // Read UA
    unsigned char buf_UA[1] = {0};
    int k_UA = 0;
    volatile int STOP_UA = FALSE;
    int check = 0;

    while (STOP_UA == FALSE)
    {
        check = read(fd, buf_UA, 1);
        if (check == 0) {
            return 0;
        }
        switch(buf_UA[0]){
            case FLAG:
                if(k_UA==0){
                    k_UA++;
                } else if(k_UA==4) {
                    STOP_UA=TRUE;
                } else {
                    k_UA=0;
                    return 0;
                }
                break;
            case A_UA:
                if(k_UA==1){
                    k_UA++;
                } else {                    
                    k_UA=0;
                    return 0;
                }
                break;
            case C_UA:
                if(k_UA==2){
                    k_UA++;
                } else {                    
                    k_UA=0;
                    return 0;
                }
                break;
            case BCC1_UA:
                if(k_UA==3){
                    k_UA++;
                } else {
                    k_UA=0;
                    return 0;
                }
                break; 
            default:
                return 0;
            break;
        }
    }
    printf("UA received!\n\n");
    return 1;
}

int read_SET(int fd){
    // Read SET
    unsigned char buf_SET[1] = {0};
    int k_SET = 0;
    volatile int STOP_SET = FALSE;
    
    while (STOP_SET == FALSE) {
        read(fd,buf_SET,1);
        switch(buf_SET[0]) {
            case FLAG:
                if(k_SET==0){
                    k_SET++;
                } else if(k_SET==4){
                    STOP_SET = TRUE;
                } else {
                    k_SET=0;
                    return 0;
                }          
                break;
            case A_SET:
                if(k_SET==1 || k_SET==2){
                    k_SET++;
                } else {                    
                    k_SET=0;
                    return 0;
                }
                break;      
            case BCC1_SET:
                if(k_SET==3){
                    k_SET++;
                } else {
                    k_SET=0;
                    return 0;
                }
                break;
            default:
                    return 0;
                break;
            }  
    }   
    printf("SET received!\n");
    return 1;
}

void send_UA(int fd){
    // Send UA
    unsigned char buf_UA[BUF_SIZE_UA] = {0};
    buf_UA[0] = FLAG;
    buf_UA[1] = A_UA;
    buf_UA[2] = C_UA;
    buf_UA[3] = BCC1_UA;
    buf_UA[4] = FLAG;
        
    write(fd, buf_UA, BUF_SIZE_UA);
    printf("UA send!\n\n");
    sleep(sleep_time);
}

void send_DISC(int fd){
    // Send DISC
    unsigned char buf_DISC_Write[BUF_SIZE_DISC] = {0};
    buf_DISC_Write[0] = FLAG;
    buf_DISC_Write[1] = A_DISC;
    buf_DISC_Write[2] = C_DISC;
    buf_DISC_Write[3] = BCC1_DISC;
    buf_DISC_Write[4] = FLAG;
        
    write(fd, buf_DISC_Write, BUF_SIZE_DISC);
    printf("DISC send!\n");
    sleep(sleep_time);
}

int read_DISC(int fd){
    // Read DISC
    unsigned char buf_DISC_Read[1] = {0};
    int k_DISC = 0;
    volatile int STOP_DISC = FALSE;
    
    while (STOP_DISC == FALSE) {
        read(fd,buf_DISC_Read,1);
        switch(buf_DISC_Read[0]) {
            case FLAG:
                if(k_DISC==0){
                    k_DISC++;
                } else if(k_DISC==4){
                    STOP_DISC = TRUE;
                } else {
                    k_DISC=0;
                    return 0;
                }          
                break;
            case A_DISC:
                if(k_DISC==1){
                    k_DISC++;
                } else {                    
                    k_DISC=0;
                    return 0;
                }
                break;
            case C_DISC:
                if(k_DISC==2){
                    k_DISC++;
                } else {                    
                    k_DISC=0;
                    return 0;
                }
                break;   
            case BCC1_DISC:
                if(k_DISC==3){
                    k_DISC++;
                } else {                    
                    k_DISC=0;
                    return 0;
                }
                break; 
            default:
                    return 0;
                break;
            }  
    }   
    printf("DISC received!\n");
    return 1;
}

////////////////////////////////////////////////
// LLOPEN
////////////////////////////////////////////////
int llopen(LinkLayer connectionParameters)
{
    // Open a connection using the "port" parameters defined in struct linkLayer.
    // Return "1" on success or "-1" on error.
    global_connectionParameters = connectionParameters;
    global_fd = open(connectionParameters.serialPort, O_RDWR | O_NOCTTY);

    if (global_fd < 0)
    {
        perror(connectionParameters.serialPort);
        exit(-1);
    }

    struct termios oldtio;
    struct termios newtio;

    // Save current port settings
    if (tcgetattr(global_fd, &oldtio) == -1)
    {
        perror("tcgetattr");
        exit(-1);
    }

    // Clear struct for new port settings
    memset(&newtio, 0, sizeof(newtio));

    newtio.c_cflag = connectionParameters.baudRate | CS8 | CLOCAL | CREAD;
    newtio.c_iflag = IGNPAR;
    newtio.c_oflag = 0;

    // Set input mode (non-canonical, no echo,...)
    newtio.c_lflag = 0;
    newtio.c_cc[VTIME] = 0; // Inter-character timer unused
    newtio.c_cc[VMIN] = 5;  // Blocking read until 5 chars received

    // VTIME e VMIN should be changed in order to protect with a
    // timeout the reception of the following character(s)

    // Now clean the line and activate the settings for the port
    // tcflush() discards data written to the object referred to
    // by fd but not transmitted, or data received but not read,
    // depending on the value of queue_selector:
    //   TCIFLUSH - flushes data received but not read.
    tcflush(global_fd, TCIOFLUSH);

    // Set new port settings
    if (tcsetattr(global_fd, TCSANOW, &newtio) == -1)
    {
        perror("tcsetattr");
        exit(-1);
    }

    if(connectionParameters.role == LlTx){
        // Send SET
        send_SET(global_fd);

        // Read UA
        int check = read_UA(global_fd);
        if (check == 1) {
            ESTABLISHMENT = TRUE;
            return 1;
        }

        (void)signal(SIGALRM, alarmHandler);

        while (alarmCount < 4 && ESTABLISHMENT == FALSE) {
            if (alarmEnabled == FALSE) {       
                send_SET(global_fd); //problema aqui !!!!
                alarm(3);
                alarmEnabled = TRUE;
                sleep(1);

                if (read_UA(global_fd) == 1) {
                    ESTABLISHMENT = TRUE;
                    alarm(0);
                    return 1;
                }
            }
        }
        return -1;
    }

    if(connectionParameters.role == LlRx){
        // Read Set
        if(read_SET(global_fd)==0){
            printf("Error SET\n");
            return -1;
        }
    
        // Send UA
        send_UA(global_fd);
           
        return 1;  
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
    return 0;
}

////////////////////////////////////////////////
// LLREAD
////////////////////////////////////////////////
// Receive data in packet.
// Return number of chars read, or "-1" on error.
int llread(unsigned char *packet)
{
    return 0;
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
    }

    if(global_connectionParameters.role == LlRx){
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

    struct termios oldtio;

    sleep(1);

    // Restore the old port settings
    if (tcsetattr(global_fd, TCSANOW, &oldtio) == -1)
    {
        perror("tcsetattr");
        exit(-1);
    }

    close(global_fd);
    return 1;
}