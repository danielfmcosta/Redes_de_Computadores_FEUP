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

// Define control field values for packet types
#define START_CONTROL 2
#define END_CONTROL   3
#define DATA_CONTROL  1

// Define TLV types
#define TLV_FILE_SIZE 0  // For file size
#define TLV_FILE_NAME 1  // For file name

// Maximum payload size for data packets (as defined in link_layer.h)
#ifndef MAX_PAYLOAD_SIZE
#define MAX_PAYLOAD_SIZE 1000
#endif

// Define a maximum file name length (adjust if needed)
#define MAX_FILENAME 256

// Maximum control packet size:
// 1 byte control + (1+1+4) for file size + (1+1+MAX_FILENAME) for file name.
#define MAX_CONTROL_PACKET_SIZE (1 + (1 + 1 + 4) + (1 + 1 + MAX_FILENAME))

// Build a control packet (START or END) into the provided buffer.
// Returns the packet size, or -1 if an error occurs.
int buildControlPacket(int controlType, long fileSize, const char *fileName, unsigned char *packet)
{
    int fileNameLen = strlen(fileName);
    if (fileNameLen > MAX_FILENAME)
        return -1;
    int index = 0;
    packet[index++] = controlType; // Control field (START or END)
    
    // TLV for file size:
    packet[index++] = TLV_FILE_SIZE;  // Type
    packet[index++] = 4;              // Length (4 bytes for file size)
    // File size in big-endian format (most significant byte first)
    packet[index++] = (fileSize >> 24) & 0xFF;
    packet[index++] = (fileSize >> 16) & 0xFF;
    packet[index++] = (fileSize >> 8) & 0xFF;
    packet[index++] = fileSize & 0xFF;
    
    // TLV for file name:
    packet[index++] = TLV_FILE_NAME;  // Type
    packet[index++] = fileNameLen;      // Length (file name length)
    memcpy(&packet[index], fileName, fileNameLen);
    index += fileNameLen;
    
    return index;
}

// Build a data packet into the provided buffer.
// Returns the packet size.
int buildDataPacket(const unsigned char *data, int dataSize, unsigned char *packet)
{
    int index = 0;
    packet[index++] = DATA_CONTROL;              // Data control field
    packet[index++] = (dataSize >> 8) & 0xFF;      // L2: high byte
    packet[index++] = dataSize & 0xFF;             // L1: low byte
    memcpy(&packet[index], data, dataSize);        // Payload data
    index += dataSize;
    return index;
}

// Parse a control packet received by the receiver.
// On success, sets *controlType, *fileSize, and fills fileNameOut (which should be MAX_FILENAME long).
// Returns 0 on success, -1 on error.
int parseControlPacket(const unsigned char *packet, int packetSize, int *controlType, long *fileSize, char *fileNameOut)
{
    if (packetSize < 1 + (1+1+4) + (1+1)) // minimum size check
        return -1;
    
    int index = 0;
    *controlType = packet[index++]; // Should be START_CONTROL or END_CONTROL

    // Parse TLV for file size:
    if (packet[index++] != TLV_FILE_SIZE)
        return -1;
    int len = packet[index++];
    if (len != 4 || index + 4 > packetSize)
        return -1;
    *fileSize = 0;
    *fileSize |= packet[index++] << 24;
    *fileSize |= packet[index++] << 16;
    *fileSize |= packet[index++] << 8;
    *fileSize |= packet[index++];
    
    // Parse TLV for file name:
    if (index >= packetSize)
        return -1;
    if (packet[index++] != TLV_FILE_NAME)
        return -1;
    int nameLen = packet[index++];
    if (nameLen <= 0 || nameLen > MAX_FILENAME || index + nameLen > packetSize)
        return -1;
    memcpy(fileNameOut, &packet[index], nameLen);
    fileNameOut[nameLen] = '\0';
    return 0;
}

// Parse a data packet.
// On success, sets *dataSizeOut and copies payload into dataOut (which should be MAX_PAYLOAD_SIZE long).
// Returns 0 on success, -1 on error.
int parseDataPacket(const unsigned char *packet, int packetSize, int *dataSizeOut, unsigned char *dataOut)
{
    if (packetSize < 1 + 2) // at least control + 2 bytes for length
        return -1;
    int index = 0;
    if (packet[index++] != DATA_CONTROL)
        return -1;
    int L2 = packet[index++];
    int L1 = packet[index++];
    int payloadSize = (L2 << 8) | L1;
    if (index + payloadSize > packetSize)
        return -1;
    memcpy(dataOut, &packet[index], payloadSize);
    *dataSizeOut = payloadSize;
    return 0;
}

void applicationLayer(const char *serialPort, const char *role, int baudRate,
                      int nTries, int timeout, const char *filename)
{
    //   serial port: /dev/ttyS10
    //   role "tx" or "rx"
    //   baudrate: baudrate of the serial port.
    //   nTries: maximum number of frame retries.
    //   timeout: frame timeout.
    //   filename: name of the file to send / receive.

    //   build the link layer connection parameters.
    LinkLayer connectionParameters;
    strcpy(connectionParameters.serialPort, serialPort); //copy string, serial port
    // set the baud rate, number of retries, and timeout.
    connectionParameters.baudRate = baudRate; 
    connectionParameters.nRetransmissions = nTries;
    connectionParameters.timeout = timeout;
    // set the role, transmitter or receiver.
    if (strcmp(role, "tx") == 0) connectionParameters.role = LlTx;
    if (strcmp(role, "rx") == 0) connectionParameters.role = LlRx;

    // establish connection with the link layer.
    int openResult = llopen(connectionParameters);
    if (openResult == -1)
    {
        printf("Connection failed\n");
        return;
    }
    printf("Connection established\n");
    
    if (connectionParameters.role == LlTx)
    {
        // TRANSMITTER SIDE
        FILE *fp = fopen(filename, "rb");
        if (!fp)
        {
            printf("Error opening file %s\n", filename);
            llclose(0);
            return;
        }
        
        // Get file size.
        fseek(fp, 0, SEEK_END);
        long fileSize = ftell(fp);
        fseek(fp, 0, SEEK_SET);
        
        // Build and send START control packet.
        unsigned char controlPacket[MAX_CONTROL_PACKET_SIZE];
        int controlPacketSize = buildControlPacket(START_CONTROL, fileSize, filename, controlPacket);
        if (controlPacketSize < 0 || llwrite(controlPacket, controlPacketSize) < 0)
        {
            printf("Error sending START packet\n");
            fclose(fp);
            llclose(0);
            return;
        }
        
        // Send data packets.
        unsigned char fileBuffer[MAX_PAYLOAD_SIZE];
        unsigned char dataPacket[1 + 2 + MAX_PAYLOAD_SIZE];
        int bytesRead;
        while ((bytesRead = fread(fileBuffer, 1, MAX_PAYLOAD_SIZE, fp)) > 0)
        {
            int dataPacketSize = buildDataPacket(fileBuffer, bytesRead, dataPacket);
            if (llwrite(dataPacket, dataPacketSize) < 0)
            {
                printf("Error sending data packet\n");
                fclose(fp);
                llclose(0);
                return;
            }
        }
        fclose(fp);
        
        // Build and send END control packet.
        controlPacketSize = buildControlPacket(END_CONTROL, fileSize, filename, controlPacket);
        if (controlPacketSize < 0 || llwrite(controlPacket, controlPacketSize) < 0)
        {
            printf("Error sending END packet\n");
            llclose(0);
            return;
        }
    }
    else if (connectionParameters.role == LlRx)
    {
        // RECEIVER SIDE
        // Buffer for incoming packets.
        // Assume maximum packet size is either a control packet (MAX_CONTROL_PACKET_SIZE)
        // or a data packet (1 + 2 + MAX_PAYLOAD_SIZE).
        unsigned char packetBuffer[1 + 2 + MAX_PAYLOAD_SIZE];
        int packetSize;
        
        // First, wait for the START control packet.
        packetSize = llread(packetBuffer);
        if (packetSize < 0)
        {
            printf("Error reading packet\n");
            llclose(0);
            return;
        }
        int ctrlType;
        long fileSize;
        char receivedFileName[MAX_FILENAME];
        if (parseControlPacket(packetBuffer, packetSize, &ctrlType, &fileSize, receivedFileName) < 0 || ctrlType != START_CONTROL)
        {
            printf("Error: Expected START packet\n");
            llclose(0);
            return;
        }
        printf("START packet received: file size = %ld, file name = %s\n", fileSize, receivedFileName);
        
        // Open a file for writing the received data.
        // For example, prepend "received_" to the file name.
        char outputFileName[MAX_FILENAME + 10];
        snprintf(outputFileName, sizeof(outputFileName), "received_%s", receivedFileName);
        FILE *fp = fopen(outputFileName, "wb");
        if (!fp)
        {
            printf("Error creating file %s\n", outputFileName);
            llclose(0);
            return;
        }
        
        long totalBytesReceived = 0;
        int finished = 0;
        while (!finished)
        {
            packetSize = llread(packetBuffer);
            if (packetSize < 0)
            {
                printf("Error reading packet\n");
                fclose(fp);
                llclose(0);
                return;
            }
            // Determine packet type by examining the first byte.
            int packetType = packetBuffer[0];
            if (packetType == DATA_CONTROL)
            {
                int payloadSize;
                unsigned char fileBuffer[MAX_PAYLOAD_SIZE];
                if (parseDataPacket(packetBuffer, packetSize, &payloadSize, fileBuffer) < 0)
                {
                    printf("Error parsing data packet\n");
                    fclose(fp);
                    llclose(0);
                    return;
                }
                // Write payload to file.
                fwrite(fileBuffer, 1, payloadSize, fp);
                totalBytesReceived += payloadSize;
            }
            else if (packetType == END_CONTROL)
            {
                // Parse END control packet (we could verify file size or file name if desired).
                if (parseControlPacket(packetBuffer, packetSize, &ctrlType, &fileSize, receivedFileName) < 0 || ctrlType != END_CONTROL)
                {
                    printf("Error parsing END packet\n");
                    fclose(fp);
                    llclose(0);
                    return;
                }
                finished = 1;
                printf("END packet received\n");
            }
            else
            {
                printf("Unknown packet type received: %d\n", packetType);
            }
        }
        fclose(fp);
        printf("File received successfully, total bytes = %ld\n", totalBytesReceived);
    }
    
    // Close connection (passing TRUE to print statistics if desired).
    llclose(FALSE);
}
