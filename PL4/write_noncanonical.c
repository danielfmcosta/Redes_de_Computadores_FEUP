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

#define FLAG 0x7E

#define BUF_SIZE_SET 5
#define A_SET 0x03
#define C_SET 0x03
#define BCC1_SET A_SET^C_SET

#define BUF_SIZE_UA 5
#define A_UA 0x03
#define C_UA 0x07
#define BCC1_UA A_UA^C_UA

#define BUF_SIZE_DISC 5
#define A_DISC 0x03
#define C_DISC 0x0B
#define BCC1_DISC A_DISC^C_DISC 

#define sleep_time 1

volatile int ESTABLISHMENT = FALSE;
volatile int STOP = FALSE;
volatile int Termination = FALSE;

int alarmEnabled = FALSE;
int alarmCount = 0;

void alarmHandler(int signal)
{
    if(ESTABLISHMENT==FALSE) {
        alarmEnabled = FALSE;
        alarmCount++;
        
        printf("Alarm #%d\n", alarmCount);
    }
}

/*int get_BCC2(unsigned char *argv){
    int BCC2 = argv[4];
    for(int i = 5; i<BUF_SIZE-2;i++) {
        BCC2 ^=argv[i];
    }
    return BCC2;
}*/

void send_DISC(int fd){
    // Send DISC
    unsigned char buf_DISC_Write[BUF_SIZE_DISC] = {0};
    buf_DISC_Write[0] = FLAG;
    buf_DISC_Write[1] = A_DISC;
    buf_DISC_Write[2] = C_DISC;
    buf_DISC_Write[3] = BCC1_DISC;
    buf_DISC_Write[4] = FLAG;
        
    write(fd, buf_DISC_Write, BUF_SIZE_DISC);
    printf("DISC send\n");
    sleep(sleep_time);
}

void send_UA(int fd){
    unsigned char buf_UA[BUF_SIZE_UA] = {0};
    buf_UA[0] = FLAG;
    buf_UA[1] = A_UA;
    buf_UA[2] = C_UA;
    buf_UA[3] = BCC1_UA;
    buf_UA[4] = FLAG;
        
    write(fd, buf_UA, BUF_SIZE_UA);
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

int llopen(int fd){
    // Send SET
    unsigned char buf_SET[BUF_SIZE_SET] = {0};
    buf_SET[0] = FLAG;
    buf_SET[1] = A_SET;
    buf_SET[2] = C_SET;
    buf_SET[3] = BCC1_SET;
    buf_SET[4] = FLAG;

    write(fd, buf_SET, BUF_SIZE_SET);
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
            write(fd, buf_SET, BUF_SIZE_SET);
            alarm(3);
            alarmEnabled = TRUE;
            sleep(1);

            if (read_UA(fd) == 1) {
                ESTABLISHMENT = TRUE;
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

    Termination = TRUE;    
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

    if(llclose(fd) == 1){
        printf("Termination Ok!\n\n");
    } else {
        printf("Termination Not Ok!\n\n");
    }

    //Write
    /*unsigned char buf[BUF_SIZE] = {0};

    buf[0] = FLAG;
    buf[1] = A;
    buf[2] = C;
    buf[3] = BCC1;
    for(int i=4; i<BUF_SIZE-2; i++){
        buf[i] = D;        
    }
    int BCC2 = get_BCC2(buf);
    buf[BUF_SIZE-2] = BCC2;
    buf[BUF_SIZE-1] = FLAG;

    for(int i =0; i<BUF_SIZE; i++){
        printf("0x%02X\n", buf[i]);
    }

    int bytes = write(fd, buf, BUF_SIZE);
    printf("%d bytes written\n", bytes);
    
    // Read - State Machine
    /*unsigned char buf[BUF_SIZE + 1] = {0};    
    unsigned char res[BUF_SIZE + 1] = {0};
    int k = 0;

    while (STOP == FALSE)
    {
        // Returns after 5 chars have been input
        read(fd, buf, 1);
        switch(buf[0]){
            case 0x7E:
                if(k==0){
                    res[k]=buf[0];
                } else if(k==4){
                    res[k]=buf[0];
                    STOP = TRUE;
                } else {
                    k=0;
                    res[k]=buf[0];
                }
                break;
            case 0x03:
                if(k==1 || k==2){
                    res[k]=buf[0];
                } else {                    
                    k=0;
                }
                break;
            case 0x00:
                if(k==3){
                    res[k]=buf[0];
                } else {
                    k=0;
                }
                break;
                     
            default:
                break;
        }
        k++;
    }
    
    for (int i = 0; i < BUF_SIZE; i++){
            printf("0x%02X\n", res[i]);
        } 
    */

    //Read
    /*unsigned char buf[BUF_SIZE + 1] = {0}; 
    while (STOP == FALSE)
    {
        int bytes = read(fd, buf, BUF_SIZE);
        buf[bytes] = '\0';
        for(int i = 0; i < BUF_SIZE; i++) {
            printf("0x%02x\n", buf[i]);
        }
        STOP = TRUE;
    }
    */
    

    // In non-canonical mode, '\n' does not end the writing.
    // Test this condition by placing a '\n' in the middle of the buffer.
    // The whole buffer must be sent even with the '\n'.
    //buf[5] = '\n';

    

    // Set alarm function handler
    
     
    

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
