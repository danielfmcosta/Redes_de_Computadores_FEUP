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

// tipos de byte de controlo
#define START 0x02
#define END 0x03
#define DATA 0x01

// tipos de TLV
#define TLV_SIZE 0x00 
#define TLV_NAME 0x01 

// maximo payload size para os data packets 
#define MAX_PACKET_DATA_SIZE 502 // 512 - 6 (cabeçalho e rodapé) - 4 (controlo e TLV)

// defines a maximum file name length 
#define MAX_FILENAME 494 // MAX_CONTROL_PACKET_SIZE - 1 (control field) - 1 (TLV size) - 4 (TLV size length) -  1 (TLV name) - 1 (TLV name length) - 2 (just in case)

// maximum control packet size:
#define MAX_CONTROL_PACKET_SIZE 504 // 256 - 6 (header and footer) - 2 (just in case) 

void printArray(const unsigned char *array, int length) {
    printf("Array (length %d): ", length);
    for (int i = 0; i < length; i++) {
        printf("%02X ", array[i]); // Print each byte in hex format
    }
    printf("\n");
}

// builds a control packet START or END
// returns the packet size, or -1 if an error occurs.
int buildControlPacket(int controlType, long fileSize, const char *fileName, unsigned char *packet) {
    int fileNameLen = strlen(fileName);
    if (fileNameLen > MAX_FILENAME) {
        fprintf(stderr, "Error: file name too long.\n");
        return -1;
    }
   
    int index = 0;
    packet[index] = controlType; // START or END
    index++;
    
    // TLV for file size:
    packet[index] = TLV_SIZE; 
    index++;
    packet[index] = 4;             
    index++;
    packet[index] = (fileSize >> 24) & 0xFF;
    index++;
    packet[index] = (fileSize >> 16) & 0xFF;
    index++;
    packet[index] = (fileSize >> 8) & 0xFF;
    index++;
    packet[index] = fileSize & 0xFF;
    index++;
   
    // TLV for file name:
    packet[index] = TLV_NAME;  // Type
    index++;
    packet[index] = fileNameLen;      // Length: file name length
    index++;
    memcpy(&packet[index], fileName, fileNameLen);
    index += fileNameLen;

    return index;
}

// builds a data packet into the provided buffer.
// Returns the packet size.
int buildDataPacket(const unsigned char *data, int dataSize, unsigned char *packet) {
    int index = 0;
    packet[index] = DATA; 
    index++;

    // k = L2 * 64 + L1, since we are using smaller sizes of 512 total bytes
    packet[index] = (dataSize / 64);  
    index++;
    packet[index] = (dataSize % 64);        
    index++;

    memcpy(&packet[index], data, dataSize);

    index += dataSize;
    return index;
}

// Parse a control packet received by the receiver.
// Returns 0 on success, -1 on error.
int parseControlPacket(const unsigned char *packet, int packetSize, int *controlType, long *fileSize, char *fileNameOut) {
    // check minimal size
    if (packetSize < 9)
        return -1;
    
    int index = 0;
    *controlType = packet[index]; 
    
    index++; 

    if (packet[index] != TLV_SIZE)
        return -1; 
    
    index++;
    int len = packet[index]; 
    index++;
    if (len != 4 || index + 4 > packetSize)
        return -1;
    
    int temp1 = packet[index] << 24;
    index++;
    int temp2 = packet[index] << 16;
    index++;
    int temp3 = packet[index] << 8;
    index++;
    int temp4 = packet[index];
    index++;
    
    *fileSize = (temp1) | (temp2) | (temp3) | (temp4);

    if (index >= packetSize) 
        return -1;


    if (packet[index] != TLV_NAME)
        return -1;
    index++;
    int nameLen = packet[index]; 
    if (nameLen <= 0 || nameLen > MAX_FILENAME || index + nameLen > packetSize)
        return -1;

    index++;
    memcpy(fileNameOut, &packet[index], nameLen);
    fileNameOut[nameLen] = '\0';
    
    return 0;
}


// Returns 0 on success, -1 on error.
int parseDataPacket(const unsigned char *packet, int packetSize, int *dataSizeOut, unsigned char *dataOut) {
    if (packetSize <= 3) // at least control field and 2 bytes for length
        return -1;
    
    int index = 0;
    if (packet[index++] != DATA)
        return -1;
    
    int L2 = packet[index++];
    int L1 = packet[index++];
    int payloadSize = L2 * 64 + L1;
    
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

        // build and send START packet
        unsigned char controlPacket[MAX_CONTROL_PACKET_SIZE];
        int controlPacketSize = buildControlPacket(START, fileSize, filename, controlPacket);
        int write_start = llwrite(controlPacket, controlPacketSize);
        if (controlPacketSize < 0 || write_start < 0) {

            fprintf(stderr, "Error sending START packet\n");
            fclose(fp);
            llclose(0);
            return;
        }
        
        // read file in chunks and send as data packets
        unsigned char fileBuffer[MAX_PACKET_DATA_SIZE];
        unsigned char dataPacket[1 + 2 + MAX_PACKET_DATA_SIZE];
        int bytesRead;
        int written = 0;
        long totalSent = 0; 

        // Set a bar width (e.g., 50 characters)
        const int barWidth = 50;

        while ((bytesRead = fread(fileBuffer, 1, MAX_PACKET_DATA_SIZE, fp)) > 0) {
            int dataPacketSize = buildDataPacket(fileBuffer, bytesRead, dataPacket);
            written = llwrite(dataPacket, dataPacketSize);
            if (written < 0) {
                fprintf(stderr, "Error sending data packet\n");
                fclose(fp);
                llclose(0);
                return;
            }
            totalSent += bytesRead;
            printf("\rSent %ld bytes\n", totalSent);
            // Calculate progress percentage
            int percent = (int)((totalSent * 100) / fileSize);
            
            // Print loading bar
            printf("\r["); // return carriage to start of line
            int pos = (percent * barWidth) / 100;
            for (int i = 0; i < barWidth; i++) {
                if (i < pos)
                    printf("#");
                else
                    printf(" ");
            }
            printf("] %d%%", percent);
            fflush(stdout);
        }
        fclose(fp);
        
        // build and send END packet
        controlPacketSize = buildControlPacket(END, fileSize, filename, controlPacket);
        int write_end = llwrite(controlPacket, controlPacketSize);
        if (controlPacketSize < 0 || write_end < 0) {
            fprintf(stderr, "Error sending END packet\n");
            llclose(0);
            return;
        }
        printf("File sent successfully.\n");
    
    }
    else if (connectionParameters.role == LlRx) {
        // RECEIVER
        unsigned char packetBuffer[1 + 2 + MAX_PACKET_DATA_SIZE] = {0};
        int packetSize;
        
        // wait for the START packet
        packetSize = llread(packetBuffer);

        if (packetSize < 0) {
            fprintf(stderr, "Error reading START packet\n");
            llclose(0);
            return;
        }
        
        int ctrlType = 0;
        long fileSize = 0;
        char receivedFileName[MAX_FILENAME] = {0};
        int parse_result = parseControlPacket(packetBuffer, packetSize, &ctrlType, &fileSize, receivedFileName);
        if (parse_result < 0 || ctrlType != START) {
            fprintf(stderr, "Error: Expected START packet\n");
            llclose(0);
            return;
        }
        printf("START packet received: file size = %ld, file name = %s\n", fileSize, receivedFileName);
        
        // prepare output file 
        FILE *fp = fopen(filename, "wb");
        if (!fp) {
            fprintf(stderr, "Error creating file %s\n", filename);
            llclose(0);
            return;
        }
        
        long totalBytesReceived = 0;

        const int barWidth = 50;

        int finish = 1;
        while (finish) {
            packetSize = llread(packetBuffer);
            if (packetSize < 0) {
                fprintf(stderr, "Error reading packet\n");
                fclose(fp);
                llclose(0);
                return;
            }
            
            int packetType = packetBuffer[0];
            if (packetType == DATA) {
                int payloadSize;
                unsigned char fileBuffer[MAX_PACKET_DATA_SIZE];
                if (parseDataPacket(packetBuffer, packetSize, &payloadSize, fileBuffer) < 0) {
                    fprintf(stderr, "Error parsing data packet\n");
                    fclose(fp);
                    llclose(0);
                    return;
                }
                fwrite(fileBuffer, 1, payloadSize, fp);
                totalBytesReceived += payloadSize;
                
                // Update loading bar
                int percent = (int)((totalBytesReceived * 100) / fileSize);
                printf("\r["); // Return carriage to start of line
                int pos = (percent * barWidth) / 100;
                for (int i = 0; i < barWidth; i++) {
                    if (i < pos)
                        printf("#");
                    else
                        printf(" ");
                }
                printf("] %d%%", percent);
                fflush(stdout);
            }
            else if (packetType == END) {
                // Validate the END packet if desired
                if (parseControlPacket(packetBuffer, packetSize, &ctrlType, &fileSize, receivedFileName) < 0 || ctrlType != END) {
                    fprintf(stderr, "Error parsing END packet\n");
                    fclose(fp);
                    llclose(0);
                    return;
                }
                finish = 0;
                printf("END packet received\n");
            }
            else {
                fprintf(stderr, "Unknown packet type: %d\n", packetType);
            }
        }
        fclose(fp);
        printf("File received successfully, total bytes = %ld\n", totalBytesReceived);
        
    }
    
    // close the connection pass FALSE we don't want statistics printed
    llclose(FALSE);
}


