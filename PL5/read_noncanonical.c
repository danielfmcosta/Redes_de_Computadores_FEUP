// Read from serial port in non-canonical mode
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

#define A 0x03

#define C_0 0x00
#define C_1 0x40
#define BCC1_0 A^C_0
#define BCC1_1 A^C_1

#define A_RR_0 0x03
#define C_RR_0 0x05
#define BCC1_RR_0 A_RR_0^C_RR_0

#define A_RR_1 0x03
#define C_RR_1 0x85
#define BCC1_RR_1 A_RR_1^C_RR_1

#define A_REJ_0 0x03
#define C_REJ_0 0x01
#define BCC1_REJ_0 A_REJ_0^C_REJ_0

#define A_REJ_1 0x03
#define C_REJ_1 0x81
#define BCC1_REJ_1 A_REJ_1^C_REJ_1

#define BUF_SIZE_SET_UA_RR_DISC 5
#define A_SET 0x03
#define C_SET 0x03
#define BCC1_SET A_SET^C_SET

#define A_UA 0x03
#define C_UA 0x07
#define BCC1_UA A_UA^C_UA

#define A_DISC 0x03
#define C_DISC 0x0B
#define BCC1_DISC A_DISC^C_DISC 

#define sleep_time 1

volatile int STOP = FALSE;
volatile int ESTABLISHMENT = FALSE;
volatile int READ = FALSE;
volatile int TERMINATION = FALSE;

int C_RR = 0x85;

void print_array(unsigned char *argv) 
{   
    int size = sizeof(argv);
    for (int i = 0; i <= size; i++) {
        printf("0x%02X ", argv[i]);
    }
    printf("\n");
}

int get_BCC2(unsigned char *argv){
    int BCC2 = argv[4];
    for (int i = 5; i < (BUF_SIZE - 2); i++){
        BCC2 ^= argv[i];
    }
    return BCC2;
}

void send_UA(int fd){
    // Send UA
    unsigned char buf_UA[BUF_SIZE_SET_UA_RR_DISC] = {0};
    buf_UA[0] = FLAG;
    buf_UA[1] = A_UA;
    buf_UA[2] = C_UA;
    buf_UA[3] = BCC1_UA;
    buf_UA[4] = FLAG;
        
    write(fd, buf_UA, BUF_SIZE_SET_UA_RR_DISC);
    printf("UA send\n\n");
    sleep(sleep_time);
}

void send_DISC(int fd){
    // Write DISC
    unsigned char buf_DISC_Write[BUF_SIZE_SET_UA_RR_DISC] = {0};
    buf_DISC_Write[0] = FLAG;
    buf_DISC_Write[1] = A_DISC;
    buf_DISC_Write[2] = C_DISC;
    buf_DISC_Write[3] = BCC1_DISC;
    buf_DISC_Write[4] = FLAG;
        
    write(fd, buf_DISC_Write, BUF_SIZE_SET_UA_RR_DISC);
    printf("DISC send\n");
    sleep(sleep_time);
}

void send_RR_1(int fd) {
    // Write RR1
    unsigned char buf_RR_1_Write[BUF_SIZE_SET_UA_RR_DISC] = {0};
    buf_RR_1_Write[0] = FLAG;
    buf_RR_1_Write[1] = A_RR_1;
    buf_RR_1_Write[2] = C_RR_1;
    buf_RR_1_Write[3] = BCC1_RR_1;
    buf_RR_1_Write[4] = FLAG;
        
    write(fd, buf_RR_1_Write, BUF_SIZE_SET_UA_RR_DISC);
    printf("RR1 send!\n");
    sleep(sleep_time);
}

void send_RR_0(int fd) {
    // Write RR0
    unsigned char buf_RR_0_Write[BUF_SIZE_SET_UA_RR_DISC] = {0};
    buf_RR_0_Write[0] = FLAG;
    buf_RR_0_Write[1] = A_RR_0;
    buf_RR_0_Write[2] = C_RR_0;
    buf_RR_0_Write[3] = BCC1_RR_0;
    buf_RR_0_Write[4] = FLAG;
        
    write(fd, buf_RR_0_Write, BUF_SIZE_SET_UA_RR_DISC);
    printf("RR0 send!\n");
    sleep(sleep_time);
}

void send_REJ_0(int fd) {
    // Write REJ0
    unsigned char buf_REJ_0_Write[BUF_SIZE_SET_UA_RR_DISC] = {0};
    buf_REJ_0_Write[0] = FLAG;
    buf_REJ_0_Write[1] = A_REJ_0;
    buf_REJ_0_Write[2] = C_REJ_0;
    buf_REJ_0_Write[3] = BCC1_REJ_0;
    buf_REJ_0_Write[4] = FLAG;
        
    write(fd, buf_REJ_0_Write, BUF_SIZE_SET_UA_RR_DISC);
    printf("REJ0 send!\n");
    sleep(sleep_time);
}

void send_REJ_1(int fd) {
    // Write REJ1
    unsigned char buf_REJ_1_Write[BUF_SIZE_SET_UA_RR_DISC] = {0};
    buf_REJ_1_Write[0] = FLAG;
    buf_REJ_1_Write[1] = A_REJ_1;
    buf_REJ_1_Write[2] = C_REJ_1;
    buf_REJ_1_Write[3] = BCC1_REJ_1;
    buf_REJ_1_Write[4] = FLAG;
        
    write(fd, buf_REJ_1_Write, BUF_SIZE_SET_UA_RR_DISC);
    printf("REJ1 send!\n");
    sleep(sleep_time);
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
    printf("SET received\n");
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

int read_UA(int fd){
    // Read UA
    unsigned char buf_UA[1] = {0};
    int k_UA = 0;
    volatile int STOP_UA = FALSE;
    
    while (STOP_UA == FALSE) {
        read(fd,buf_UA,1);
        switch(buf_UA[0]) {
            case FLAG:
                if(k_UA==0){
                    k_UA++;
                } else if(k_UA==4){
                    STOP_UA = TRUE;
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
    printf("UA read\n\n");
    return 1;
}

unsigned char* byte_destuffing(unsigned char *argv, int stuffed_length)
{
    static unsigned char destuffed[BUF_SIZE];
    int i, j = 0;

    destuffed[j++] = argv[0];

    for (i = 1; i < stuffed_length - 1; i++) {
        if (argv[i] == ESCAPE) {
            destuffed[j++] = argv[++i] ^ 0x20;
        } else {
            destuffed[j++] = argv[i];
        }
    }

    destuffed[j++] = argv[stuffed_length - 1];

    return destuffed;
}

int llopen(int fd){
    // Read SET
    if(read_SET(fd)==0){
        printf("Error SET\n");
        return 0;
    }

    // Send UA
    send_UA(fd);
    
    ESTABLISHMENT = TRUE;    
    return 1;            
}

int llread(int fd){
    unsigned char buf[1] = {0};    
    unsigned char res[MAX_BUF_SIZE] = {0};
    int k = 0;
    int FlagBCC2 = 0;
    
    while (STOP == FALSE)
    {
        read(fd, buf, 1);
        if(FlagBCC2 == 0) {
            switch(buf[0]){
                case FLAG:
                    if(k==0){
                        res[k]=buf[0];
                        k++;
                    } else {    
                        return 0;
                    }
                    break;
                case A:
                    if(k==1){
                        res[k]=buf[0];
                        k++;
                    } else if(k==3){
                        res[k]=buf[0];
                        k++;
                        FlagBCC2 = 1;
                    } else {                    
                        return 0;
                    }
                    break;
                case C_0:
                    if(k==2){
                        res[k]=buf[0];
                        k++;
                    } else {                    
                        return 0;
                    }
                    break;
                case C_1:
                    if(k==2){
                        res[k]=buf[0];
                        k++;
                    } else {                    
                        return 0;
                    }
                    break;
                /*case BCC1_0:
                    if(k==3){
                        res[k]=buf[0];
                        k++;
                        FlagBCC2 = 1;
                    } else {
                        return 0;
                    }
                    break;*/
                case BCC1_1:
                    if(k==3){
                        res[k]=buf[0];
                        k++;
                        FlagBCC2 = 1;
                    } else {
                        return 0;
                    }
                    break;
                default:
                    break;
            }   
        } else {
            if(buf[0]==FLAG){
                res[k]=buf[0];
                k++;
                STOP = TRUE;
            } else {
                res[k]=buf[0];
                k++;
            }
        }   
    } 

    unsigned char* original_buf = byte_destuffing(res, sizeof(res));
    

    if(original_buf[2] == C_0) {
        if(original_buf[sizeof(original_buf)-2]!=get_BCC2(original_buf)){ 
            printf("Error BCC2!\n");
            send_REJ_0(fd);
            return 0;
        } else {
            send_RR_1(fd);
        }
    } else if(original_buf[2] == C_1){
        if(original_buf[sizeof(original_buf)-2]!=get_BCC2(original_buf)){ 
            printf("Error BCC2!\n");
            send_REJ_1(fd);
            return 0;
        } else {
             send_RR_0(fd);
        }
    } else {
        printf("Error send RR!\n");
        return 0;
    }

    READ = TRUE;
    return 1;
}

int llclose(int fd){
    // Read DISC
    if(read_DISC(fd)==0){
        printf("Error DISC\n");
        return 0;
    }

    // Send DISC
    send_DISC(fd);
    
    // Read UA
    if(read_UA(fd)==0){
        printf("Error UA\n");
        return 0;
    }

    TERMINATION = TRUE;
    return 1;            
}

int main(int argc, char *argv[])
{   
    
    if (argc < 2)
    {
        printf("Incorrect program usage\n"
            "Usage: %s <SerialPort>\n"
            "Example: %s /dev/ttyS1\n",
            argv[0],
            argv[0]);
            exit(1);
    }
        
    // Program usage: Uses either COM1 or COM2
    const char *serialPortName = argv[1];
    
    // Open serial port device for reading and writing and not as controlling tty
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
    newtio.c_cc[VMIN] = 5;  // Blocking read until 5 chars received

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

    if(llread(fd) == 1 && ESTABLISHMENT == TRUE){
        printf("Read Ok!\n\n");
    } else {
        printf("Read Not Ok!\n\n");
    }

    if(llclose(fd) == 1 && READ == TRUE){
        printf("Termination Ok!\n\n");
    } else {
        printf("Termination Not Ok!\n\n");
    }

    // The while() cycle should be changed in order to respect the specifications
    // of the protocol indicated in the Lab guide

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
