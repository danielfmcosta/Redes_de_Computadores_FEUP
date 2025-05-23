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
#define MAX_PACKET_DATA_SIZE 502 // 512 - 6 (cabeçalho e rodapé) - 3 (controlo e L2 e L1) - 1 (só por garantia)

// maximo tamanho de um nome de ficheiro
#define MAX_FILENAME 494 // MAX_CONTROL_PACKET_SIZE - 1 (controlo) - 1 (TLV_SIZE) - 4 (TLV size length) -  1 (TLV_NAME) - 1 (TLV name length) - 2 (só por garantia)

// maximum control packet size:
#define MAX_CONTROL_PACKET_SIZE 504 // 512 - 6 (header and footer) - 2 (só por garantia) 

// função para imprimir um array de bytes
void printArray(const unsigned char *array, int length) {
    printf("Array (length %d): ", length);
    for (int i = 0; i < length; i++) {
        printf("%02X ", array[i]);
    }
    printf("\n");
}

// constroi o pacote de controlo START ou END
// retorna o tamanho do pacote, ou -1 se ocorrer algum erro
int buildControlPacket(int controlType, long fileSize, const char *fileName, unsigned char *packet) {
    int fileNameLen = strlen(fileName);
    if (fileNameLen > MAX_FILENAME) {
        printf("Error: file name too long.\n");
        return -1;
    }
   
    int index = 0;
    packet[index] = controlType;  // START ou END
    index++;
    
    packet[index] = TLV_SIZE; // TLV_SIZE
    index++;
    packet[index] = 4;  // tamanho do tamanho do ficheiro         
    index++;
    // tamanho do ficheiro em 4 bytes em big-endian
    packet[index] = (fileSize >> 24) & 0xFF; 
    index++;
    packet[index] = (fileSize >> 16) & 0xFF; 
    index++;
    packet[index] = (fileSize >> 8) & 0xFF; 
    index++;
    packet[index] = fileSize & 0xFF; 
    index++;
   
    packet[index] = TLV_NAME;  // TLV_NAME
    index++;
    packet[index] = fileNameLen; // tamanho do nome do ficheiro    
    index++;
    memcpy(&packet[index], fileName, fileNameLen); // nome do ficheiro
    index += fileNameLen;

    return index;
}

// constroi o pacote de dados
// retorna o tamanho do pacote
int buildDataPacket(const unsigned char *data, int dataSize, unsigned char *packet) {
    int index = 0;
    if(dataSize > MAX_PACKET_DATA_SIZE) {
        printf("Error: data packet size too large.\n");
        return -1;
    }
    packet[index] = DATA; 
    index++;

    // k = L2 * 64 + L1, como temos pacotes de tamanho máximo 502, não faz sentido utilizar 256 como diz nos slides
    packet[index] = (dataSize / 64);  
    index++;
    packet[index] = (dataSize % 64);        
    index++;

    memcpy(&packet[index], data, dataSize);

    index += dataSize;
    return index;
}

// interpreta o pacote de controlo
// retorna 0 em caso de sucesso, -1 se ocorrer algum erro
int parseControlPacket(const unsigned char *packet, int packetSize, int *controlType, long *fileSize, char *fileNameOut) {
    // tamanho mínimo de um pacote de controlo, que são os C (1), T1(1), L1(1), V1(4), T2(1), L2(1) 
    if (packetSize < 9)
        return -1;
    
    int index = 0;
    *controlType = packet[index];   //verifica se é START ou END
    
    index++; 

    if (packet[index] != TLV_SIZE) //verifica se é TLV_SIZE
        return -1; 
    
    index++;
    int len = packet[index]; 
    index++;
    if (len != 4 || index + 4 > packetSize) //verifica se o tamanho é 4 e se não ultrapassa o tamanho do pacote
        return -1;
    
    int temp1 = packet[index] << 24; 
    index++;
    int temp2 = packet[index] << 16;
    index++;
    int temp3 = packet[index] << 8;
    index++;
    int temp4 = packet[index];
    index++;
    
    *fileSize = (temp1) | (temp2) | (temp3) | (temp4); // junta os 4 bytes para obter o tamanho do ficheiro

    if (packet[index] != TLV_NAME) //verifica se é TLV_NAME
        return -1;
    index++;
    int nameLen = packet[index]; 
    if (nameLen <= 0 || nameLen > MAX_FILENAME || index + nameLen > packetSize) //verifica se o tamanho do nome é válido
        return -1;

    index++;
    memcpy(fileNameOut, &packet[index], nameLen); //copia o nome do ficheiro
    fileNameOut[nameLen] = '\0'; //adiciona o terminador de string
    return 0;
}


// Returns 0 on success, -1 on error.
int parseDataPacket(const unsigned char *packet, int packetSize, int *dataSizeOut, unsigned char *dataOut) {
    if (packetSize <= 3) // at least control field and 2 bytes for length
        return -1;
    
    int index = 0;
    if (packet[index] != DATA)
        return -1;
    index++;
    
    int L2 = packet[index]; // L2
    index++;
    int L1 = packet[index]; // L1
    index++;
    int payloadSize = L2 * 64 + L1;
    
    if (index + payloadSize > packetSize)
        return -1;
    
    memcpy(dataOut, &packet[index], payloadSize); // copiar payload
    *dataSizeOut = payloadSize;
    return 0;
}


void applicationLayer(const char *serialPort, const char *role, int baudRate,
                      int nTries, int timeout, const char *filename)
{
    // Criar uma estrutura LinkLayer com os dados da conexão
    LinkLayer connectionParameters;
    memset(&connectionParameters, 0, sizeof(LinkLayer)); // Inicializar a estrutura com zeros
    
    // Copiar os parâmetros da conexão
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
    
    // Estabelecer a conexão
    int openResult = llopen(connectionParameters);
    if (openResult == -1) {
        printf("Connection failed\n");
        return;
    }
    printf("Connection established\n");
    

    // Transmitir ou receber o ficheiro
    if (connectionParameters.role == LlTx) {
        // Transmiter

        // abrir o ficheiro
        FILE *fp = fopen(filename, "rb");
        if (!fp) {
            printf("Error opening file %s\n", filename);
            llclose(0);
            return;
        }
        
        // tamanho do ficheiro
        fseek(fp, 0, SEEK_END);
        long fileSize = ftell(fp);

        fseek(fp, 0, SEEK_SET);
        
        printf("Sending file %s, size = %ld bytes\n", filename, fileSize);

        // constroi e manda o control packet
        unsigned char controlPacket[MAX_CONTROL_PACKET_SIZE];
        int controlPacketSize = buildControlPacket(START, fileSize, filename, controlPacket);
        int write_start = llwrite(controlPacket, controlPacketSize);
        if (controlPacketSize < 0 || write_start < 0) {
            printf("Error sending START packet\n");
            fclose(fp);
            llclose(0);
            return;
        }
        
        // lê o ficheiro e envia os dados
        unsigned char fileBuffer[MAX_PACKET_DATA_SIZE];
        unsigned char dataPacket[1 + 2 + MAX_PACKET_DATA_SIZE]; 
        int bytesRead;
        int written = 0;
        long totalSent = 0; 

        // tamanho da barra de progresso
        const int barWidth = 50;
        // lê o ficheiro e envia os dados
        while ((bytesRead = fread(fileBuffer, 1, MAX_PACKET_DATA_SIZE, fp)) > 0) {
            int dataPacketSize = buildDataPacket(fileBuffer, bytesRead, dataPacket);
            written = llwrite(dataPacket, dataPacketSize);
            if (written < 0) {
                printf("Error sending data packet\n");
                fclose(fp);
                llclose(0);
                return;
            }
            totalSent += bytesRead;

            // calcula percentagem
            int percent = (int)((totalSent * 100) / fileSize);
            
            // imprimi barra de progresso
            printf("\r["); 
            int pos = (percent * barWidth) / 100;
            for (int i = 0; i < barWidth; i++) {
                if (i < pos)
                    printf("*");
                else
                    printf(" ");
            }
            printf("] %d%%", percent);
            fflush(stdout);
        }
        fclose(fp);
        
        // constroi e manda o pacote de controlo END
        controlPacketSize = buildControlPacket(END, fileSize, filename, controlPacket);
        int write_end = llwrite(controlPacket, controlPacketSize);
        if (controlPacketSize < 0 || write_end < 0) {
            printf("\nError sending END packet\n");
            llclose(0);
            return;
        }
        printf("\nFile sent successfully.\n");
    
    }
    else if (connectionParameters.role == LlRx) {
        // Receiver
        unsigned char packetBuffer[1 + 2 + MAX_PACKET_DATA_SIZE] = {0};
        int packetSize;
        
        // espera pelo pacote de controlo START
        packetSize = llread(packetBuffer);

        if (packetSize < 0) {
            printf("Error reading START packet\n");
            llclose(0);
            return;
        }
        // verifica se é o pacote START
        int ctrlType = 0;
        long fileSize = 0;
        char receivedFileName[MAX_FILENAME] = {0};
        int parse_result = parseControlPacket(packetBuffer, packetSize, &ctrlType, &fileSize, receivedFileName);
        if (parse_result < 0 || ctrlType != START) {
            printf("Error: Expected START packet\n");
            llclose(0);
            return;
        }
        printf("START packet received: file size = %ld, file name = %s\n", fileSize, receivedFileName);
        
        // abrir o ficheiro para escrita 
        FILE *fp = fopen(filename, "wb");
        if (!fp) {
            printf("Error creating file %s\n", filename);
            llclose(0);
            return;
        }
        
        long totalBytesReceived = 0;

        const int barWidth = 50;
        // receber os dados
        int finish = 1;
        while (finish) {
            packetSize = llread(packetBuffer);
            if (packetSize < 0) {
                printf("Error reading packet\n");
                fclose(fp);
                llclose(0);
                return;
            }
            
            int packetType = packetBuffer[0];
            // verifica se é um pacote de dados
            if (packetType == DATA) {
                int payloadSize;
                unsigned char fileBuffer[MAX_PACKET_DATA_SIZE];
                if (parseDataPacket(packetBuffer, packetSize, &payloadSize, fileBuffer) < 0) {
                    printf("Error parsing data packet\n");
                    fclose(fp);
                    llclose(0);
                    return;
                }
                fwrite(fileBuffer, 1, payloadSize, fp);
                totalBytesReceived += payloadSize;
                
                // imprime a barra de progresso
                int percent = (int)((totalBytesReceived * 100) / fileSize);
                printf("\r["); 
                int pos = (percent * barWidth) / 100;
                for (int i = 0; i < barWidth; i++) {
                    if (i < pos)
                        printf("*");
                    else
                        printf(" ");
                }
                printf("] %d%%", percent);
                fflush(stdout);
            }
            else if (packetType == END) {
                // verifica se é o pacote END
                if (parseControlPacket(packetBuffer, packetSize, &ctrlType, &fileSize, receivedFileName) < 0 || ctrlType != END) {
                    printf("Error parsing END packet\n");
                    fclose(fp);
                    llclose(0);
                    return;
                }
                finish = 0;
                printf("\nEND packet received\n");
            }
            
        }
        fclose(fp);
        printf("\nFile received successfully, total bytes = %ld\n", totalBytesReceived);
        
    }
    
    // fecha a conexão
    llclose(FALSE);
}


