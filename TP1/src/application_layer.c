// Application layer protocol implementation

#include "application_layer.h"
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

void applicationLayer(const char *serialPort, const char *role, int baudRate,
    int nTries, int timeout, const char *filename)
{
    //   serialPort: Serial port name (e.g., /dev/ttyS0).
    //   role: Application role {"tx", "rx"}.
    //   baudrate: Baudrate of the serial port.
    //   nTries: Maximum number of frame retries.
    //   timeout: Frame timeout.
    //   filename: Name of the file to send / receive.

    LinkLayer connectionParameters;
    strcpy(connectionParameters.serialPort, serialPort);
    connectionParameters.baudRate = baudRate;
    connectionParameters.nRetransmissions = nTries;
    connectionParameters.timeout = timeout;

    if (role == "tx") {
        connectionParameters.role = LlTx;
    } else if (role == "rx") {
        connectionParameters.role = LlRx;
    } else {
        printf("Error!\n");
        return;
    }

    if (llopen(connectionParameters) < 0) {
        printf("llopen() Error!\n");
        return;
    }   

    if (llclose(FALSE) < 0) {
        printf("llclose() Error!\n");
        return;
    }

    //...
}