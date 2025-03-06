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

#define FLAG 0x7E
#define A 0x03
#define C 0x40
#define BCC1 A^C

#define BUF_SIZE 5

volatile int STOP = FALSE;

int alarmEnabled = FALSE;
int alarmCount = 0;

void alarmHandler(int signal)
{
    alarmEnabled = FALSE;
    alarmCount++;
    
    printf("Alarm #%d\n", alarmCount);
}

int get_BCC2(unsigned char *argv){
    int BCC2 = argv[4];
    for (int i = 5; i < (BUF_SIZE - 2); i++){
        BCC2 ^= argv[i];
    }
    return BCC2;
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

    printf("New termios structure set\n");

    // Read - State Machine
    
    /*
    unsigned char buf[BUF_SIZE + 1] = {0};    
    unsigned char res[BUF_SIZE + 1] = {0};
    int k = 0;
    int FlagBCC2 = 0;
    
    while (STOP == FALSE)
    {
        // Returns after 5 chars have been input
        read(fd, buf, 1);
        if(FlagBCC2 == 0) {
            switch(buf[0]){
                case FLAG:
                    if(k==0){
                        res[k]=buf[0];
                    } else {                    
                        k=0;
                    }
                    break;
                case A:
                    if(k==1){
                        res[k]=buf[0];
                    } else {                    
                        k=0;
                    }
                    break;
                case C:
                    if(k==2){
                        res[k]=buf[0];
                    } else {                    
                        k=0;
                    }
                    break;
                case BCC1:
                    if(k==3){
                        res[k]=buf[0];
                        FlagBCC2 = 1;
                    } else {
                        k=0;
                    }
                    break;
                default:
                    break;
            }   
        } else {
            if(buf[0]==FLAG && k==(BUF_SIZE-1)){
                res[k]=buf[0];
                STOP = TRUE;
            } else {
                res[k]=buf[0];
            }
        }   
        k++;
    } 
    int BCC2 = get_BCC2(res);
    if(res[BUF_SIZE-2]!=BCC2) printf("Deu barracu\n");
    
    for (int i = 0; i < BUF_SIZE; i++){
            printf("0x%02X\n", res[i]);
        }
    */

    // Loop for input - Read
    /*
    unsigned char buf[BUF_SIZE + 1] = {0}; // +1: Save space for the final '\0' char
    
    while (STOP == FALSE) {
        int bytes = read(fd, buf, BUF_SIZE);
        buf[bytes] = '\0'; // Set end of string to '\0', so we can printf

        for (int i = 0; i < BUF_SIZE; i++) {
            printf("0x%02X\n", buf[i]);
        } 

        STOP = TRUE;
    }
    */

    // Write
    
    unsigned char buf_w[BUF_SIZE] = {0};

    buf_w[0] = FLAG;
    buf_w[1] = A;
    buf_w[2] = C;
    buf_w[3] = BCC1;
    buf_w[4] = FLAG;

    //int bytes_w = write(fd, buf_w, BUF_SIZE);
    //printf("%d bytes written\n", bytes_w); 
    
    
    // Set alarm function handler
    
    (void)signal(SIGALRM, alarmHandler);

    while (alarmCount < 4)
    {
        if (alarmEnabled == FALSE)
        {       
            write(fd, buf_w, BUF_SIZE);
            alarm(3); // Set alarm to be triggered in 3s
            alarmEnabled = TRUE;
        }
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
