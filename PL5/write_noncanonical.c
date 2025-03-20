// Write to serial port in non-canonical mode
//
// Modified by: Eduardo Nuno Almeida [enalmeida@fe.up.pt]

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <termios.h>
#include <unistd.h>

#include <signal.h>

// Baudrate settings are defined in <asm/termbits.h>, which is
// included by <termios.h>
#define BAUDRATE B38400
#define _POSIX_SOURCE 1 // POSIX compliant source

#define FALSE 0
#define TRUE 1

#define BUF_SIZE 8
#define MAX_BUF_SIZE BUF_SIZE*2
#define FLAG 0x7E
#define ESCAPE 0x7D

// I frame constants
#define A 0x03
#define C_0 0x00
#define C_1 0x40
#define D 0x01
#define BCC1_0 A^C_0
#define BCC1_1 A^C_1

#define BUF_SIZE_SET_UA_DISC 5
//SET constants
#define A_SET 0x03
#define C_SET 0x03
#define BCC1_SET A_SET^C_SET
//UA constants
#define A_UA 0x03
#define C_UA 0x07
#define BCC1_UA A_UA^C_UA
//RR constants
#define C_RR_0 0x05
#define C_RR_1 0x85
#define BCC1_RR_0 A^C_RR_0
#define BCC1_RR_1 A^C_RR_1
//REJ constants
#define C_REJ_0 0x01
#define C_REJ_1 0x81
#define BCC1_REJ_0 A^C_REJ_0
#define BCC1_REJ_1 A^C_REJ_1

#define A_DISC 0x03
#define C_DISC 0x0B
#define BCC1_DISC A_DISC^C_DISC 

#define sleep_time 1

volatile int ESTABLISHMENT = FALSE;
volatile int STOP = FALSE;
volatile int DATA_SENT = FALSE;
volatile int TERMINATION = FALSE;

int alarmEnabled = FALSE;
int alarmCount = 0;

int Ns = 0;


void print_array(unsigned char *argv) 
{   
    int size = sizeof(argv);
    printf("%d", size);
    for (int i = 0; i <= size; i++) {
        printf("0x%02X ", argv[i]);
    }
    printf("\n");
}

void alarmHandler(int signal)
{
    if(ESTABLISHMENT==FALSE) {
        alarmEnabled = FALSE;
        alarmCount++;
        
        printf("Alarm #%d\n", alarmCount);
    }
}

int get_BCC2(unsigned char *argv){
    int BCC2 = argv[4];
    for(int i = 5; i<BUF_SIZE-2;i++) {
        BCC2 ^=argv[i];
    }
    return BCC2;
}

void send_DISC(int fd){
    // Send DISC
    unsigned char buf_DISC_Write[BUF_SIZE_SET_UA_DISC] = {0};
    buf_DISC_Write[0] = FLAG;
    buf_DISC_Write[1] = A_DISC;
    buf_DISC_Write[2] = C_DISC;
    buf_DISC_Write[3] = BCC1_DISC;
    buf_DISC_Write[4] = FLAG;
        
    write(fd, buf_DISC_Write, BUF_SIZE_SET_UA_DISC);
    printf("DISC send\n");
    sleep(sleep_time);
}

void send_UA(int fd){
    unsigned char buf_UA[BUF_SIZE_SET_UA_DISC] = {0};
    buf_UA[0] = FLAG;
    buf_UA[1] = A_UA;
    buf_UA[2] = C_UA;
    buf_UA[3] = BCC1_UA;
    buf_UA[4] = FLAG;
        
    write(fd, buf_UA, BUF_SIZE_SET_UA_DISC);
    printf("UA send\n\n");
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
    printf("UA received\n\n");
    return 1;
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
    printf("DISC received\n");
    return 1;
}

int read_R(int fd){
    // Read possible replies
    unsigned char reply[BUF_SIZE_SET_UA_DISC] = {0};
    unsigned char buf_Reply_Read[1] = {0};
    int k_RR = 0;
    volatile int STOP_RR = FALSE;
    
    while (STOP_RR == FALSE) {
        read(fd,buf_Reply_Read,1);
        switch(buf_Reply_Read[0]) {
            case FLAG:
                if(k_RR==0){
                    reply[k_RR] = buf_Reply_Read[0];
                    k_RR++;
                } else if(k_RR==4){
                    reply[k_RR] = buf_Reply_Read[0];
                    STOP_RR = TRUE;
                } else {
                    k_RR=0;
                    return 0;
                }          
                break;
            case A_DISC:
                if(k_RR==1){
                    reply[k_RR] = buf_Reply_Read[0];
                    k_RR++;
                } else {                    
                    k_RR=0;
                    return 0;
                }
                break;
            case C_RR_0:
                if(k_RR==2){
                    reply[k_RR] = buf_Reply_Read[0];
                    k_RR++;
                } else {                    
                    k_RR=0;
                    return 0;
                }
                break;
            case C_RR_1:
                if(k_RR==2){
                    reply[k_RR] = buf_Reply_Read[0];
                    k_RR++;
                } else {                    
                    k_RR=0;
                    return 0;
                }
                break; 
            case C_REJ_0:
                if(k_RR==2){
                    reply[k_RR] = buf_Reply_Read[0];
                    k_RR++;
                } else {                    
                    k_RR=0;
                    return 0;
                }
                break; 
            case C_REJ_1:
                if(k_RR==2){
                    reply[k_RR] = buf_Reply_Read[0];
                    k_RR++;
                } else {                    
                    k_RR=0;
                    return 0;
                }
                break;    
            case BCC1_RR_0:
                if(k_RR==3){
                    reply[k_RR] = buf_Reply_Read[0];
                    k_RR++;
                } else {                    
                    k_RR=0;
                    return 0;
                }
                break; 
            case BCC1_RR_1:
                if(k_RR==3){
                    reply[k_RR] = buf_Reply_Read[0];
                    k_RR++;
                } else {                    
                    k_RR=0;
                    return 0;
                }
                break; 
            case BCC1_REJ_0:
                if(k_RR==3){
                    reply[k_RR] = buf_Reply_Read[0];
                    k_RR++;
                } else {                    
                    k_RR=0;
                    return 0;
                }
                break; 
            case BCC1_REJ_1:
                if(k_RR==3){
                    reply[k_RR] = buf_Reply_Read[0];
                    k_RR++;
                } else {                    
                    k_RR=0;
                    return 0;
                }
                break; 
            default:
                    return 0;
                break;
            }  
    }   

    printf("Reply received correctly\n");

    print_array(reply);

    return 1;
}



unsigned char* byte_stuffing(unsigned char *argv, int *length)
{
    static unsigned char stuffed[BUF_SIZE * 2];
    
    int i, j = 0;

    stuffed[j++] = argv[0];  

    for (i = 1; i < BUF_SIZE - 1; i++) {
        if (argv[i] == FLAG || argv[i] == ESCAPE) {
            stuffed[j++] = ESCAPE;
            stuffed[j++] = argv[i] ^ 0x20;
        } else {
            stuffed[j++] = argv[i];
        }
    }

    // copiar a ultima FLAG (0x7E)
    stuffed[j++] = argv[BUF_SIZE - 1];

    // returnar o comprimento do e stuffed frame
    *length = j;

    print_array(stuffed);

    return stuffed;
}

int llopen(int fd){
    // Send SET
    unsigned char buf_SET[BUF_SIZE_SET_UA_DISC] = {0};
    buf_SET[0] = FLAG;
    buf_SET[1] = A_SET;
    buf_SET[2] = C_SET;
    buf_SET[3] = BCC1_SET;
    buf_SET[4] = FLAG;
    
    write(fd, buf_SET, BUF_SIZE_SET_UA_DISC);
    printf("SET send\n");
    sleep(sleep_time);

    // Read UA
    int check = read_UA(fd);
    if (check == 1) {
        ESTABLISHMENT = TRUE;
        return 1;
    }

    (void)signal(SIGALRM, alarmHandler);

    while (alarmCount < 4 && ESTABLISHMENT == FALSE) {
        if (alarmEnabled == FALSE) {       
            write(fd, buf_SET, BUF_SIZE_SET_UA_DISC);
            alarm(3);
            alarmEnabled = TRUE;
            sleep(1);

            if (read_UA(fd) == 1) {
                DATA_SENT = TRUE;
                alarm(0);
                return 1;
            }
        }
    }
    return 0;
}


int llwrite(int fd){

    unsigned char buf[BUF_SIZE] = {0};

    buf[0] = FLAG;
    buf[1] = A;
    buf[2] = C_0;
    buf[3] = BCC1_0;
    buf[4] = FLAG;
    for(int i=5; i<BUF_SIZE-2; i++){
        buf[i] = D;        
    }
    int BCC2 = get_BCC2(buf);
    buf[BUF_SIZE-2] = BCC2;
    buf[BUF_SIZE-1] = FLAG;

    for(int i =0; i<BUF_SIZE; i++){
        printf("0x%02X\n", buf[i]);
    }

    int stuffed_length = 0;
    unsigned char *stuffed_buf = byte_stuffing(buf, &stuffed_length);
    printf("Length: %d\n", stuffed_length);
    write(fd, stuffed_buf, stuffed_length);
    printf("I send\n");
    sleep(sleep_time);

 

    int check = read_R(fd);
    if (check == 1) {
        DATA_SENT = TRUE;
        Ns=1;
        return 1;
    }

    (void)signal(SIGALRM, alarmHandler);

    while (alarmCount < 4 && DATA_SENT == FALSE) {
        if (alarmEnabled == FALSE) {       
            write(fd, stuffed_buf, stuffed_length);
            alarm(3);
            alarmEnabled = TRUE;
            sleep(1);

            if (read_R(fd) == 1) {
                DATA_SENT = TRUE;
                Ns=1;
                alarm(0);
                return 1;
            }
        }
    }
    
    return 0;
}

int llclose(int fd){
    // Send DISC
    send_DISC(fd);

    // Read DISC
    if(read_DISC(fd)==0){
        printf("Error DISC\n");
        return 0;
    }
    
    // Send UA
    send_UA(fd);

    TERMINATION = TRUE;    
    return 1;            
}

int main(int argc, char *argv[])
{   

    // Program usage: Uses either COM1 or COM2
    const char *serialPortName = argv[1];

    if (argc < 2)
    {
        printf("Incorrect program usage\n"
               "Usage: %s <SerialPort>\n"
               "Example: %s /dev/ttyS1\n",
               argv[0],
               argv[0]);
        exit(1);
    }

    // Open serial port device for reading and writing, and not as controlling tty
    // because we don't want to get killed if linenoise sends CTRL-C.
    int fd = open(serialPortName, O_RDWR | O_NOCTTY);

    if (fd < 0)
    {
        perror(serialPortName);
        exit(-1);
    }

    struct termios oldtio;
    struct termios newtio;

    // Save current port settings
    if (tcgetattr(fd, &oldtio) == -1)
    {
        perror("tcgetattr");
        exit(-1);
    }

    // Clear struct for new port settings
    memset(&newtio, 0, sizeof(newtio));

    newtio.c_cflag = BAUDRATE | CS8 | CLOCAL | CREAD;
    newtio.c_iflag = IGNPAR;
    newtio.c_oflag = 0;

    // Set input mode (non-canonical, no echo,...)
    newtio.c_lflag = 0;
    newtio.c_cc[VTIME] = 0; // Inter-character timer unused
    newtio.c_cc[VMIN] = 0;  // Blocking read until 5 chars received

    // VTIME e VMIN should be changed in order to protect with a
    // timeout the reception of the following character(s)

    // Now clean the line and activate the settings for the port
    // tcflush() discards data written to the object referred to
    // by fd but not transmitted, or data received but not read,
    // depending on the value of queue_selector:
    //   TCIFLUSH - flushes data received but not read.
    tcflush(fd, TCIOFLUSH);

    // Set new port settings
    if (tcsetattr(fd, TCSANOW, &newtio) == -1)
    {
        perror("tcsetattr");
        exit(-1);
    }

    printf("\nNew termios structure set\n\n");

    if(llopen(fd) == 1){
        printf("Establishment Ok!\n\n");
    } else {
        printf("Establishment Not Ok!\n\n");
    }


    if(llwrite(fd) == 1 && ESTABLISHMENT == TRUE){
        printf("Write Ok!\n\n");
    } else {
        printf("Write Not Ok!\n\n");
    }

    if(llclose(fd) == 1 && DATA_SENT == TRUE){
        printf("Termination Ok!\n\n");
    } else {
        printf("Termination Not Ok!\n\n");
    }
    

    // Wait until all bytes have been written to the serial port
    sleep(1);

    // Restore the old port settings
    if (tcsetattr(fd, TCSANOW, &oldtio) == -1)
    {
        perror("tcsetattr");
        exit(-1);
    }

    close(fd);

    return 0;
}
