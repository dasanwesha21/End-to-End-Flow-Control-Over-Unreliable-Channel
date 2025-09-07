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

// Function to open an MTP socket
int m_socket(int domain, int type, int protocol) {
    socket_t * sm;
    int key = ftok(SHM_NAME, 'R');
    if(key < 0){
        perror("ftok");
        exit(1);
    }

    int shmid = shmget(key, sizeof(socket_t) * MAX_SOCKETS, 0666);
    if(shmid < 0){
        perror("shmget");
        exit(1);
    }

    sm = (socket_t *)shmat(shmid, NULL, 0);

    // Check if domain and type are valid for MTP
    if (domain != AF_INET || type != SOCK_MTP) {
        errno = EPROTONOSUPPORT;
        perror("Invalid domain or type");
        return -1;
    }

    // Find a free entry in the socket metadata array
    int idx;
    for (int i = 0; i < MAX_SOCKETS; i++) {
        if (!sm[i].used) {
            idx = i;
            break;
        }
    }


    if (idx == -1) {
        errno = ENFILE;
        perror("No free entry found");
        return -1; // No free entry found
    }

    int semid1, semid2;
    int key1 = ftok(SEM_NAME, 'R');
    int key2 = ftok(SEM_NAME, 'S');

    semid1 = semget(key1, 1, 0666 | IPC_CREAT);
    semid2 = semget(key2, 1, 0666 | IPC_CREAT);

    if(semid1 < 0 || semid2 < 0){
        perror("semget");
        exit(1);
    }

    int sock_key = ftok(SOCK_NAME, 'R');
    int sock_info_id = shmget(sock_key, sizeof(sock_info), 0666);

    if(sock_info_id < 0){
        perror("shmget");
        exit(1);
    }

    sock_info * SOCK_INFO = (sock_info *)shmat(sock_info_id, NULL, 0);
    if(SOCK_INFO == (void *)-1){
        perror("shmat");
        exit(1);
    }

    SOCK_INFO->sock_id = 0;
    SOCK_INFO->port = 0;
    SOCK_INFO->ip = 0;
    SOCK_INFO->err_no = 0;

    //signal sem1, wait on sem2
    struct sembuf sb1;
    sb1.sem_num = 0;
    sb1.sem_op = 1;
    sb1.sem_flg = 0;
    struct sembuf sb2;
    sb2.sem_num = 0;
    sb2.sem_op = -1;
    sb2.sem_flg = 0;

    semop(semid1, &sb1, 1); // Signal sem1
    semop(semid2, &sb2, 1); // Wait on sem2
    // printf("m_socket: sem2 signalled to open, %d\n", SOCK_INFO->sock_id);
    // fflush(stdout);

    if(SOCK_INFO->err_no != 0){
        errno = SOCK_INFO->err_no;
        perror("m_socket");
        SOCK_INFO->sock_id = 0;
        SOCK_INFO->port = 0;
        SOCK_INFO->ip = 0;
        SOCK_INFO->err_no = 0;
        return -1;
    }

    // printf("m_socket: sockfd = %d\n", SOCK_INFO->sock_id);
    // fflush(stdout);

    // Initialize socket metadata
    sm[idx].used = 1;
    sm[idx].sockfd = SOCK_INFO->sock_id;
    sm[idx].bound = 0;
    sm[idx].source_addr.sin_family = AF_INET;
    
    sm[idx].swnd.win_start = 0;
    sm[idx].swnd.win_end = -1;
    sm[idx].swnd.win_size = WINDOW_SIZE;
    sm[idx].swnd.interim_pointer = -1;

    sm[idx].pid = getpid();

    for(int i = 0; i < MAX_SEND_BUFF_SIZE; i++){
        sm[idx].send_buff[i][0] = '\0';
    }

    sm[idx].rwnd.win_start = 0;
    sm[idx].rwnd.win_end = -1;
    sm[idx].rwnd.win_size = WINDOW_SIZE;
    sm[idx].rwnd.interim_pointer = -1;

    for(int i = 0; i < MAX_RECV_BUFF_SIZE; i++){
        sm[idx].recv_buff[i][0] = '\0';
    }

    // printf("m_socket: sockfd = %d\n", sm[idx].sockfd);
    // fflush(stdout);
    
    SOCK_INFO->sock_id = 0;
    SOCK_INFO->port = 0;
    SOCK_INFO->ip = 0;
    SOCK_INFO->err_no = 0;

    return sm[idx].sockfd;
}

// Function to bind an MTP socket
int m_bind(int sockfd, const char* src_ip, const int src_port, const char* dest_ip, const int dest_port) {

    socket_t * sm;
    int key = ftok(SHM_NAME, 'R');
    if(key < 0){
        perror("ftok");
        exit(1);
    }

    int shmid = shmget(key, sizeof(socket_t) * MAX_SOCKETS, 0666);

    if(shmid < 0){
        perror("shmget");
        exit(1);
    }

    sm = (socket_t *)shmat(shmid, NULL, 0);

    int idx;
    for(int i = 0; i < MAX_SOCKETS; i++){
        if(sm[i].sockfd == sockfd){
            idx = i;
            break;
        }
    }

    // printf("m_bind: idx = %d\n", idx);
    // fflush(stdout);

    // Check if the socket is already bound
    if (sm[idx].bound) {
        errno = EADDRINUSE;
        perror("Socket already bound");
        return -1; // Socket already bound
    }

    int semid1, semid2;
    int key1 = ftok(SEM_NAME, 'R');
    int key2 = ftok(SEM_NAME, 'S');

    // Set source and destination IP and port
    sm[idx].source_addr.sin_family = AF_INET;
    sm[idx].source_addr.sin_addr.s_addr = inet_addr(src_ip);
    sm[idx].source_addr.sin_port = htons(src_port);
    sm[idx].destination_addr.sin_family = AF_INET;
    sm[idx].destination_addr.sin_addr.s_addr = inet_addr(dest_ip);
    sm[idx].destination_addr.sin_port = htons(dest_port);

    semid1 = semget(key1, 1, 0666 | IPC_CREAT);
    semid2 = semget(key2, 1, 0666 | IPC_CREAT);

    if(semid1 < 0 || semid2 < 0){
        perror("semget");
        exit(1);
    }

    int sock_key = ftok(SOCK_NAME, 'R');
    int sock_info_id = shmget(sock_key, sizeof(sock_info), IPC_CREAT | 0666);

    if(sock_info_id < 0){
        perror("shmget");
        exit(1);
    }

    sock_info *SOCK_INFO = (sock_info *)shmat(sock_info_id, NULL, 0);

    //info of source sent
    SOCK_INFO->sock_id = sockfd;
    SOCK_INFO->port = (sm[idx].source_addr.sin_port);
    SOCK_INFO->ip = sm[idx].source_addr.sin_addr.s_addr;
    SOCK_INFO->err_no = 0;

    //signal sem1, wait on sem2
    struct sembuf sb1;
    sb1.sem_num = 0;
    sb1.sem_op = 1;
    sb1.sem_flg = 0;
    struct sembuf sb2;
    sb2.sem_num = 0;
    sb2.sem_op = -1;
    sb2.sem_flg = 0;

    semop(semid1, &sb1, 1); // Signal sem1
    semop(semid2, &sb2, 1); // Wait on sem2

    if(SOCK_INFO->err_no != 0){
        errno = SOCK_INFO->err_no;
        perror("m_bind");
        SOCK_INFO->sock_id = 0;
        SOCK_INFO->port = 0;
        SOCK_INFO->ip = 0;
        SOCK_INFO->err_no = 0;
        return -1;
    }

    sm[idx].bound = 1; // Mark the socket as bound

    // printf("m_bind: bind successful\n");
    
    SOCK_INFO->sock_id = 0;
    SOCK_INFO->port = 0;
    SOCK_INFO->ip = 0;
    SOCK_INFO->err_no = 0;

    // printf("m_bind: %d, %s, %d, %s\n", ntohs(sm[idx].source_addr.sin_port), inet_ntoa(sm[idx].source_addr.sin_addr), ntohs(sm[idx].destination_addr.sin_port), inet_ntoa(sm[idx].destination_addr.sin_addr));
    // printf("m_bind: %d\n", idx);

    return 0; // Success
}

// Function to send data through an MTP sockket
ssize_t m_sendto(int sockfd, const void *buf, ssize_t len, int flags, struct sockaddr_in *dest_addr) {
    int idx;

    socket_t * sm;
    int key = ftok(SHM_NAME, 'R');
    if(key < 0){
        perror("ftok");
        exit(1);
    }

    int shmid = shmget(key, sizeof(socket_t) * MAX_SOCKETS, 0666);

    if(shmid < 0){
        perror("shmget");
        exit(1);
    }

    sm = (socket_t *)shmat(shmid, NULL, 0);

    for(int i = 0; i < MAX_SOCKETS; i++){
        if(sm[i].sockfd == sockfd){ //find the socket with the given sockfd
            idx = i;
            break;
        }
    }

    int semid1, semid2;
    int key1 = ftok(SEM_NAME, 'R');
    int key2 = ftok(SEM_NAME, 'S');

    semid1 = semget(key1, 1, 0666 | IPC_CREAT);
    semid2 = semget(key2, 1, 0666 | IPC_CREAT);

    if(semid1 < 0 || semid2 < 0){
        perror("semget");
        exit(1);
    }

    int sock_key = ftok(SOCK_NAME, 'R');
    int sock_info_id = shmget(sock_key, sizeof(sock_info), IPC_CREAT | 0666);

    if(sock_info_id < 0){
        perror("shmget");
        exit(1);
    }

    sock_info *SOCK_INFO = (sock_info *)shmat(sock_info_id, NULL, 0);

    // Check if destination IP and port match with bounded IP and port
    if(dest_addr->sin_addr.s_addr != sm[idx].destination_addr.sin_addr.s_addr || dest_addr->sin_port != (sm[idx].destination_addr.sin_port)){
        // printf("m_sendto: destination does not match\n");
        // fflush(stdout);
        // printf("m_sendto: %s : %d, %s : %d\n", inet_ntoa(dest_addr->sin_addr), ntohs(dest_addr->sin_port), inet_ntoa(sm[idx].destination_addr.sin_addr), ntohs(sm[idx].destination_addr.sin_port));
        // fflush(stdout);
        perror("Destination address invalid");
        return -1; 
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

    // printf("m_sendto: Message written successfully : index %d : %s\n", idx, sm[idx].send_buff[0]);
    // fflush(stdout);

    return len; // Placeholder for success
}

// Function to receive data from an MTP socket
ssize_t m_recvfrom(int sockfd, void *buf, ssize_t len, int flags, struct sockaddr_in *src_addr) {
    socket_t * sm;
    int key = ftok(SHM_NAME, 'R');
    if(key < 0){
        perror("ftok");
        exit(1);
    }


    int shmid = shmget(key, sizeof(socket_t) * MAX_SOCKETS, 0666);

    if(shmid < 0){
        perror("shmget");
        exit(1);
    }

    sm = (socket_t *)shmat(shmid, NULL, 0);
    
    int idx;
    for(int i = 0; i < MAX_SOCKETS; i++){
        if(sm[i].sockfd == sockfd){
            idx = i;
            break;
        }else{
            if(i == MAX_SOCKETS - 1){
                // printf("invalid socket\n");
                // fflush(stdout);
                while(1);
            }
        }
    }

    if(len > MAX_LEN){
        errno = EMSGSIZE;
        perror("Message too long");
        return -1; // Message too long
    }    

    if(src_addr->sin_addr.s_addr != sm[idx].destination_addr.sin_addr.s_addr || src_addr->sin_port != sm[idx].destination_addr.sin_port){
        errno = ENOTBOUND;
        return -1; // Source address required
    }

    // printf("m_recvfrom: message received:  %d, %s\n", idx, sm[idx].recv_buff[0]);
    // fflush(stdout);

    int i;
    for( i = 0; i < MAX_RECV_BUFF_SIZE; i++){
        if(sm[idx].recv_buff[i][0] != '\0'){
            strcpy((char *)buf, sm[idx].recv_buff[i]);
            errno = 0;
            memset(sm[idx].recv_buff[i], 0, MAX_LEN);
            break;
        }else if(i == MAX_RECV_BUFF_SIZE - 1){
            errno = EAGAIN;
        }
    }

    if(errno == EAGAIN){
        // printf("Error in receive\n");
        // fflush(stdout);
        return -1;
    }else{
        // printf("Message received in idx %d: %s\n", idx, (char *)buf);
        // fflush(stdout);

        return len; // Placeholder for success
    }
}

// Function to close an MTP socket
int m_close(int sockfd) {
    int idx = sockfd; // Using sockfd as index into socket metadata array

    socket_t * sm;
    int key = ftok(SHM_NAME, 'R');
    if(key < 0){
        perror("ftok");
        exit(1);
    }

    int shmid = shmget(key, sizeof(socket_t) * MAX_SOCKETS, 0666);

    if(shmid < 0){
        perror("shmget");
        exit(1);
    }

    sm = (socket_t *)shmat(shmid, NULL, 0);

    // printf("in close of m_socket.c\n");
    // fflush(stdout);

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

