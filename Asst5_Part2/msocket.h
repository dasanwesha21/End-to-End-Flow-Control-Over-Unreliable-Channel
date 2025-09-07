#ifndef MSOCKET_H
#define MSOCKET_H
#include <stdio.h>
#include <pthread.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/sem.h>
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

#define T 5

#define MAX_SOCKETS 25 // Maximum number of MTP sockets
#define SOCK_MTP 100 // MTP socket type
#define ENOTBOUND EAGAIN// Destination not bound error
#define MAX_SEND_BUFF_SIZE 10 // Maximum send buffer size
#define MAX_RECV_BUFF_SIZE 5 // Maximum receive buffer size
#define MAX_LEN 1024 // Maximum length of a message
#define WINDOW_SIZE 5 // Window size
#define MAX_SEQ_NO 16

#define SHM_NAME "./initmsocket.c"
#define SEM_NAME "./msocket.c"
#define SOCK_NAME "./msocket.h"
#define NO_SPACE "./user1.c"
#define ACK_IDENTIFIER 99

#define GTHREAD_TIMEOUT 25

typedef struct{
    int win_start; //window start index
    int win_end; //window end index
    int win_size; //window size
    int interim_pointer; //for send window, points to the first message in window that 
                            //has not been sent, and for the receive window, points to
                            //the last message in window which has been received in order
    char window[MAX_SEQ_NO][MAX_LEN];
    time_t time[MAX_SEQ_NO];
}win_t;

// Structure to maintain socket metadata
typedef struct socket_{
    int used; // Indicates whether this entry is in use, i.e. socket already in use
    int sockfd; // UDP socket file descriptor(of source)
    int bound; // Indicates whether the socket is bound

    int pid; //pid of owner process

    struct sockaddr_in source_addr; // Source IP and port
    struct sockaddr_in destination_addr; // Destination IP and port

    //message buffers: one for sending and one for receiving
    char send_buff[MAX_SEND_BUFF_SIZE][MAX_LEN]; 
    char recv_buff[MAX_RECV_BUFF_SIZE][MAX_LEN]; 

    win_t swnd;
    win_t rwnd;
}socket_t;

typedef struct{
    int sock_id;
    int port;
    int ip;
    int err_no;
}sock_info;

// Function prototypes
int m_socket(int domain, int type, int protocol);
int m_bind(int sockfd, const char* src_ip, const int src_port, const char* dest_ip, const int dest_port);
ssize_t m_sendto(int sockfd, const void *buf, ssize_t len, int flags, struct sockaddr_in *dest_addr);
ssize_t m_recvfrom(int sockfd, void *buf, ssize_t len, int flags, struct sockaddr_in *src_addr);
int m_close(int sockfd);

#endif