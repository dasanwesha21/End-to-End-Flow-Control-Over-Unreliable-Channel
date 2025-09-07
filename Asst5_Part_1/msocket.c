#include <stdio.h>
#include <pthread.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/socket.h>
#include <semaphore.h>
#include <sys/mman.h>
#include <errno.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include "msocket.h"

static socket_t *sm; // Global socket metadata array

// Function to find a free entry in the socket metadata array
int find_free_entry() {
    for (int i = 0; i < MAX_SOCKETS; i++) {
        if (!sm[i].used) {
            return i;
        }
    }
    return -1; // No free entry found
}

// Function to initialize the socket metadata array
int initialize_sm() {    
    int key = ftok(SHM_NAME, 'R');
    if(key < 0){
        perror("ftok");
        exit(1);
    }

    int shmid = shmget(key, sizeof(socket_t) * MAX_SOCKETS, IPC_CREAT | 0666);
    if(shmid < 0){
        perror("shmget");
        exit(1);
    }

    sm = (socket_t *)shmat(shmid, NULL, 0);

    for (int i = 0; i < MAX_SOCKETS; i++) {
        sm[i].used = 0;
        sm[i].sockfd = -1;
        sm[i].bound = 0;

        // Initialize message buffers
        for (int j = 0; j < MAX_SEND_BUFF_SIZE; j++) {
            sm[i].send_buff[j][0] = '\0'; // Set the first character to null character to indicate empty buffer
        }
        for (int j = 0; j < MAX_RECV_BUFF_SIZE; j++) {
            sm[i].recv_buff[j][0] = '\0'; // Set the first character to null character to indicate empty buffer
        }
    }

    return key;
}

// Function to open an MTP socket
int m_socket(int domain, int type, int protocol) {
    int key = initialize_sm(); // Initialize socket metadata array

    // Check if domain and type are valid for MTP
    if (domain != AF_INET || type != SOCK_MTP) {
        errno = EPROTONOSUPPORT;
        perror("Invalid domain or type");
        return -1;
    }

    // Find a free entry in the socket metadata array
    int idx = find_free_entry();
    if (idx == -1) {
        errno = ENFILE;
        perror("No free entry found");
        return -1; // No free entry found
    }

    // Create a UDP socket
    int sockfd = socket(AF_INET, SOCK_DGRAM, protocol);
    if (sockfd < 0) {
        errno = ESOCKTNOSUPPORT;
        perror("Error creating UDP socket");
        return -1; // Error creating UDP socket
    }

    // Initialize socket metadata
    sm[idx].used = 1;
    sm[idx].sockfd = sockfd;

    return sockfd;
}

// Function to bind an MTP socket
int m_bind(int sockfd, const int src_ip, const int src_port, const int dest_ip, const int dest_port) {
    //find the index of this socket;
    int idx;
    for(int i = 0; i < MAX_SOCKETS; i++){
        if(sm[i].sockfd == sockfd){
            idx = i;
            break;
        }
    }

    // Check if the socket is already bound
    if (sm[idx].bound) {
        errno = EADDRINUSE;
        perror("Socket already bound");
        return -1; // Socket already bound
    }

    // Set source and destination IP and port
    sm[idx].source_addr.sin_family = AF_INET;
    sm[idx].source_addr.sin_addr.s_addr = htonl(src_ip);
    sm[idx].source_addr.sin_port = htons(src_port);
    sm[idx].destination_addr.sin_family = AF_INET;
    sm[idx].destination_addr.sin_addr.s_addr = htonl(dest_ip);
    sm[idx].destination_addr.sin_port = htons(dest_port);

    // Bind the UDP socket
    int ret = bind(sm[idx].sockfd, (struct sockaddr *)&sm[idx].source_addr, sizeof(sm[idx].source_addr));
    if (ret < 0) {
        perror("Error binding UDP socket");
        return -1; // Error binding UDP socket
    }

    sm[idx].bound = 1; // Mark the socket as bound

    return 0; // Success
}

// Function to send data through an MTP sockket
ssize_t m_sendto(int sockfd, const void *buf, ssize_t len, int flags, const struct sockaddr_in *dest_addr) {
    int idx;
    for(int i = 0; i < MAX_SOCKETS; i++){
        if(sm[i].sockfd == sockfd){
            idx = i;
            break;
        }
    }

    // Check if destination IP and port match with bounded IP and port
    if (dest_addr->sin_addr.s_addr != sm[idx].destination_addr.sin_addr.s_addr || dest_addr->sin_port != sm[idx].destination_addr.sin_port) {
        errno = ENOTBOUND;
        perror("Destination address invalid");
        return -1; // Destination address required
    }

    if(len > MAX_LEN){
        errno = EMSGSIZE;
        perror("Message too long");
        return -1; // Message too long
    }

    for(int i = 0; i < MAX_SEND_BUFF_SIZE; i++){
        if(sm[idx].send_buff[i][0] == '\0'){
            strcpy(sm[idx].send_buff[i], (char *)buf);
            break;
        }else if(i == MAX_SEND_BUFF_SIZE - 1){
            errno = ENOBUFS;
            perror("Send buffer full");
            return -1; // Send buffer full
        }
    }

    return len; // Placeholder for success
}

// Function to receive data from an MTP socket
ssize_t m_recvfrom(int sockfd, void *buf, ssize_t len, int flags, struct sockaddr_in *src_addr) {
    int idx;
    for(int i = 0; i < MAX_SOCKETS; i++){
        if(sm[i].sockfd == sockfd){
            idx = i;
            break;
        }
    }

    if(len > MAX_LEN){
        errno = EMSGSIZE;
        perror("Message too long");
        return -1; // Message too long
    }

    if(src_addr->sin_addr.s_addr != sm[idx].destination_addr.sin_addr.s_addr || src_addr->sin_port != sm[idx].destination_addr.sin_port){
        errno = ENOTBOUND;
        perror("Source address invalid");
        return -1; // Source address required
    }

    for(int i = 0; i < MAX_RECV_BUFF_SIZE; i++){
        if(sm[idx].recv_buff[i][0] != '\0'){
            strcpy((char *)buf, sm[idx].recv_buff[i]);
            sm[idx].recv_buff[i][0] = '\0';
            break;
        }else if(i == MAX_RECV_BUFF_SIZE - 1){
            errno = EAGAIN;
            perror("Receive buffer empty");
            return -1; // Receive buffer empty
        }
    }

    return len; // Placeholder for success
}

// Function to close an MTP socket
int m_close(int sockfd) {
    int idx = sockfd; // Using sockfd as index into socket metadata array

    // Close the UDP socket
    close(sm[idx].sockfd);

    // Clean up socket metadata
    sm[idx].used = 0;
    sm[idx].sockfd = -1;
    sm[idx].bound = 0;

    // Clean up message buffers
    for (int j = 0; j < MAX_SEND_BUFF_SIZE; j++) {
        sm[idx].send_buff[j][0] = '\0'; // Set the first character to null character to indicate empty buffer
    }
    for (int j = 0; j < MAX_RECV_BUFF_SIZE; j++) {
        sm[idx].recv_buff[j][0] = '\0'; // Set the first character to null character to indicate empty buffer
    }

    //clear the source and destination addresses
    memset(&sm[idx].source_addr, 0, sizeof(sm[idx].source_addr));
    memset(&sm[idx].destination_addr, 0, sizeof(sm[idx].destination_addr));

    return 0; // Success
}