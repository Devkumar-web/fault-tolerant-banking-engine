#include "app_config.h"
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/ip.h>
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <termios.h>

#include "socket_utils.h"

void connectionHandler(int socketFileDescriptor);
void hide_input(char *buffer, int size);

int main(int argc, char *argv[])
{
    int socketFileDescriptor;
    int connectStatus;
    struct sockaddr_in address;
    int port = 8080;
    if (argc > 1) {
        port = atoi(argv[1]);
    }
    
    socketFileDescriptor = socket(AF_INET, SOCK_STREAM, 0);
    if (socketFileDescriptor == -1)
    {
        perror("Socket creation failed");
        exit(-1);
    }
    printf("Client socket successfully created!\n");

    address.sin_addr.s_addr = htonl(INADDR_ANY);
    address.sin_family = AF_INET;
    address.sin_port = htons(port);

    // Connecting to server
    connectStatus = connect(socketFileDescriptor, (struct sockaddr *)&address, sizeof(address));
    if(connectStatus == -1 && argc <= 1)
    {
        // Try fallback port 8081 if port 8080 was unavailable
        close(socketFileDescriptor);
        socketFileDescriptor = socket(AF_INET, SOCK_STREAM, 0);
        port = 8081;
        address.sin_port = htons(port);
        connectStatus = connect(socketFileDescriptor, (struct sockaddr *)&address, sizeof(address));
    }
    if(connectStatus == -1)
    {
        perror("Connection to server failed. Is the server running?");
        exit(-1);
    }
    printf("Connected to Banking Management System Server on port %d!\n", port);

    // Handling server communication
    connectionHandler(socketFileDescriptor);

    close(socketFileDescriptor);
    return 0;
}

void connectionHandler(int socketFileDescriptor)
{
    char readBuffer[PACKET_SIZE], writeBuffer[PACKET_SIZE], tempBuffer[PACKET_SIZE];
    ssize_t readBytes, writeBytes;

    do
    {
        memset(readBuffer, 0, sizeof(readBuffer));
        readBytes = recv_all(socketFileDescriptor, readBuffer, sizeof(readBuffer));
        if (readBytes < 0)
        {
            printf("Error reading from server.\n");
            break;
        }
        else if (readBytes == 0)
        {
            printf("Connection closed by server.\n");
            break;
        }
        else
        {
            memset(writeBuffer, 0, sizeof(writeBuffer));
            memset(tempBuffer, 0, sizeof(tempBuffer));

            if (strcmp(readBuffer, "Client logging out...\n") == 0)
            {
                printf("Logged out successfully. Goodbye!\n");
                close(socketFileDescriptor);
                return;
            }
            // Check if server is prompting for password
            else if ((strstr(readBuffer, "password") != NULL || strstr(readBuffer, "Password") != NULL) &&
                     strchr(readBuffer, '^') == NULL)
            {
                printf("%s", readBuffer);
                fflush(stdout);
                hide_input(writeBuffer, sizeof(writeBuffer));
            }
            // Check if server is sending an acknowledgement message with '^' delimiter
            else if (strchr(readBuffer, '^') != NULL)
            {
                if (strlen(readBuffer) > 1)
                {
                    memcpy(tempBuffer, readBuffer, strlen(readBuffer) - 1);
                    tempBuffer[strlen(readBuffer) - 1] = '\0';
                    printf("%s\n", tempBuffer);
                }
                strcpy(writeBuffer, "");
            }
            // Normal interactive prompt
            else
            {
                printf("%s", readBuffer);
                fflush(stdout);
                if (fgets(writeBuffer, sizeof(writeBuffer), stdin) != NULL)
                {
                    writeBuffer[strcspn(writeBuffer, "\r\n")] = 0;
                }
            }
            
            writeBytes = send_all(socketFileDescriptor, writeBuffer, sizeof(writeBuffer));
            if (writeBytes < 0)
            {
                printf("Error writing to server. Closing connection.\n");
                break;
            }
        }
    } while (readBytes > 0);

    close(socketFileDescriptor);
}

// Hiding Password input
void hide_input(char *buffer, int size) {
    struct termios oldt, newt;
    
    // Get current terminal settings
    tcgetattr(STDIN_FILENO, &oldt);
    newt = oldt;

    // Disable echoing of characters
    newt.c_lflag &= ~ECHO;
    tcsetattr(STDIN_FILENO, TCSANOW, &newt);

    if (fgets(buffer, size, stdin) != NULL) {
        buffer[strcspn(buffer, "\r\n")] = 0;
    }
    printf("\n");

    // Restore terminal settings
    tcsetattr(STDIN_FILENO, TCSANOW, &oldt);
}
