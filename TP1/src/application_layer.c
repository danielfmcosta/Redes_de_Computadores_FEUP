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


#define START_CONTROL 0x02
#define END_CONTROL   0x03
#define DATA_CONTROL  0x01

#define TLV_FILE_SIZE 0x00  // file size
#define TLV_FILE_NAME 0x01  // file name

// maximum payload size for data packets 
#define MAX_PAYLOAD_SIZE 246 // 256 - 6 (header and footer) - 4 (control field and length)

// Define a maximum file name length 
#define MAX_FILENAME 238 // MAX_CONTROL_PACKET_SIZE - 1 (control field) - 1 (TLV size) - 4 (TLV size length) -  1 (TLV name) - 4 (TLV name length)

// Maximum control packet size:
#define MAX_CONTROL_PACKET_SIZE 248 // 256 - 6 (header and footer) - 2 (just in case) 


// Build a control packet (START or END) into the provided buffer.
// Returns the packet size, or -1 if an error occurs.
int buildControlPacket(int controlType, long fileSize, const char *fileName, unsigned char *packet) {
    int fileNameLen = strlen(fileName);
    if (fileNameLen > MAX_FILENAME) {
        fprintf(stderr, "Error: file name too long.\n");
        return -1;
    }
    
    int index = 0;
    packet[index++] = controlType; // START or END
    
    // TLV for file size:
    packet[index++] = TLV_FILE_SIZE;  // Type
    packet[index++] = 4;              // Length: 4 bytes
    packet[index++] = (fileSize >> 24) & 0xFF;
    packet[index++] = (fileSize >> 16) & 0xFF;
    packet[index++] = (fileSize >> 8) & 0xFF;
    packet[index++] = fileSize & 0xFF;
    
    // TLV for file name:
    packet[index++] = TLV_FILE_NAME;  // Type
    packet[index++] = fileNameLen;      // Length: file name length
    memcpy(&packet[index], fileName, fileNameLen);
    index += fileNameLen;
    
    return index;
}

// Build a data packet into the provided buffer.
// Returns the packet size.
int buildDataPacket(const unsigned char *data, int dataSize, unsigned char *packet) {
    int index = 0;
    packet[index++] = DATA_CONTROL; // Data packet identifier
    // Store data length in two bytes (big-endian)
    packet[index++] = (dataSize >> 8) & 0xFF; // high byte
    packet[index++] = dataSize & 0xFF;        // low byte
    memcpy(&packet[index], data, dataSize);
    index += dataSize;
    return index;
}

// Parse a control packet received by the receiver.
// On success, sets *controlType, *fileSize, and fills fileNameOut (which should be MAX_FILENAME long).
// Returns 0 on success, -1 on error.
int parseControlPacket(const unsigned char *packet, int packetSize, int *controlType, long *fileSize, char *fileNameOut) {
    // Check minimal size: control field + file size TLV (1+1+4) + file name TLV (1+1)
    if (packetSize < 1 + (1 + 1 + 4) + (1 + 1))
        return -1;
    
    int index = 0;
    *controlType = packet[index++];
    
    // Validate file size TLV:
    index++; 
    if (packet[index] != TLV_FILE_SIZE)
        return -1;
    int len = packet[index++];
    if (len != 4 || index + 4 > packetSize)
        return -1;
    index++;
    *fileSize = (packet[index] << 24) |
                (packet[index] << 16) |
                (packet[index] << 8)  |
                 packet[index];
    
    // Validate file name TLV:
    if (index >= packetSize)
        return -1;
    index++;
    if (packet[index] != TLV_FILE_NAME)
        return -1;
    int nameLen = packet[index++];
    if (nameLen <= 0 || nameLen > MAX_FILENAME || index + nameLen > packetSize)
        return -1;
    memcpy(fileNameOut, &packet[index], nameLen);
    fileNameOut[nameLen] = '\0';
    
    return 0;
}

int parseDataPacket(const unsigned char *packet, int packetSize, int *dataSizeOut, unsigned char *dataOut) {
    if (packetSize < 1 + 2) // at least control field and 2 bytes for length
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
    LinkLayer connectionParameters;
    memset(&connectionParameters, 0, sizeof(LinkLayer));
    
    // Set connection parameters
    strncpy(connectionParameters.serialPort, serialPort, sizeof(connectionParameters.serialPort) - 1);
    connectionParameters.baudRate = baudRate;
    connectionParameters.nRetransmissions = nTries;
    connectionParameters.timeout = timeout;
    
    if (strcmp(role, "tx") == 0) {
        connectionParameters.role = LlTx;
    } else if (strcmp(role, "rx") == 0) {
        connectionParameters.role = LlRx;
    } else {
        printf("Invalid role: %s\n", role);
        return;
    }
    
    // Open connection using the link layer API
    int openResult = llopen(connectionParameters);
    if (openResult == -1) {
        fprintf(stderr, "Connection failed\n");
        return;
    }
    printf("Connection established\n");


    
    if (connectionParameters.role == LlTx) {
        // TRANSMITTER 
        FILE *fp = fopen(filename, "rb");
        if (!fp) {
            fprintf(stderr, "Error opening file %s\n", filename);
            llclose(0);
            return;
        }
        
        // file size
        fseek(fp, 0, SEEK_END);
        long fileSize = ftell(fp);

        fseek(fp, 0, SEEK_SET);
        
        printf("Sending file %s, size = %ld bytes\n", filename, fileSize);

        // Build and send START packet
        unsigned char controlPacket[MAX_CONTROL_PACKET_SIZE];
        int controlPacketSize = buildControlPacket(START_CONTROL, fileSize, filename, controlPacket);
        print_array(controlPacket);
        if (controlPacketSize < 0 || llwrite(controlPacket, controlPacketSize) < 0) {
            fprintf(stderr, "Error sending START packet\n");
            fclose(fp);
            llclose(0);
            return;
        }
        
        // Read file in chunks and send as data packets
        unsigned char fileBuffer[MAX_PAYLOAD_SIZE];
        unsigned char dataPacket[1 + 2 + MAX_PAYLOAD_SIZE];
        int bytesRead;
        while ((bytesRead = fread(fileBuffer, 1, MAX_PAYLOAD_SIZE, fp)) > 0) {
            int dataPacketSize = buildDataPacket(fileBuffer, bytesRead, dataPacket);
            if (llwrite(dataPacket, dataPacketSize) < 0) {
                fprintf(stderr, "Error sending data packet\n");
                fclose(fp);
                llclose(0);
                return;
            }
        }
        fclose(fp);
        
        // Build and send END packet
        controlPacketSize = buildControlPacket(END_CONTROL, fileSize, filename, controlPacket);
        if (controlPacketSize < 0 || llwrite(controlPacket, controlPacketSize) < 0) {
            fprintf(stderr, "Error sending END packet\n");
            llclose(0);
            return;
        }
        printf("File sent successfully.\n");
    }
    else if (connectionParameters.role == LlRx) {
        // ---------- RECEIVER SIDE ----------
        unsigned char packetBuffer[1 + 2 + MAX_PAYLOAD_SIZE];
        int packetSize;
        
        // Wait for the START packet
        packetSize = llread(packetBuffer);
        if (packetSize < 0) {
            fprintf(stderr, "Error reading START packet\n");
            llclose(0);
            return;
        }
        
        int ctrlType;
        long fileSize;
        char receivedFileName[MAX_FILENAME];
        if (parseControlPacket(packetBuffer, packetSize, &ctrlType, &fileSize, receivedFileName) < 0 || ctrlType != START_CONTROL) {
            fprintf(stderr, "Error: Expected START packet\n");
            llclose(0);
            return;
        }
        printf("START packet received: file size = %ld, file name = %s\n", fileSize, receivedFileName);
        
        // Prepare output file (prefixing with "received_")
        char outputFileName[MAX_FILENAME + 10];
        snprintf(outputFileName, sizeof(outputFileName), "received_%s", receivedFileName);
        FILE *fp = fopen(outputFileName, "wb");
        if (!fp) {
            fprintf(stderr, "Error creating file %s\n", outputFileName);
            llclose(0);
            return;
        }
        
        long totalBytesReceived = 0;
        int finished = 0;
        while (!finished) {
            packetSize = llread(packetBuffer);
            if (packetSize < 0) {
                fprintf(stderr, "Error reading packet\n");
                fclose(fp);
                llclose(0);
                return;
            }
            
            int packetType = packetBuffer[0];
            if (packetType == DATA_CONTROL) {
                int payloadSize;
                unsigned char fileBuffer[MAX_PAYLOAD_SIZE];
                if (parseDataPacket(packetBuffer, packetSize, &payloadSize, fileBuffer) < 0) {
                    fprintf(stderr, "Error parsing data packet\n");
                    fclose(fp);
                    llclose(0);
                    return;
                }
                fwrite(fileBuffer, 1, payloadSize, fp);
                totalBytesReceived += payloadSize;
            }
            else if (packetType == END_CONTROL) {
                // Validate the END packet if desired
                if (parseControlPacket(packetBuffer, packetSize, &ctrlType, &fileSize, receivedFileName) < 0 || ctrlType != END_CONTROL) {
                    fprintf(stderr, "Error parsing END packet\n");
                    fclose(fp);
                    llclose(0);
                    return;
                }
                finished = 1;
                printf("END packet received\n");
            }
            else {
                fprintf(stderr, "Unknown packet type: %d\n", packetType);
            }
        }
        fclose(fp);
        printf("File received successfully, total bytes = %ld\n", totalBytesReceived);
    }
    
    // Close the connection (pass FALSE if you don't want statistics printed)
    llclose(FALSE);
}


