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

typedef struct{
    int sock_id;
    int port;
    int ip;
    int err_no;
}sock_info;

typedef struct{
    int win_size;
    int seq_nos[MAX_SEND_BUFF_SIZE];
}win_t;

typedef struct{
    int in_use;
    int pid;
    int udp_sockid;
    int mtp_sockid;
    int ip;
    int port;
    int rec_buff[MAX_RECV_BUFF_SIZE][MAX_LEN];
    int send_buff[MAX_SEND_BUFF_SIZE][MAX_LEN];
    win_t swnd;
    win_t rwnd;
}SM;

static socket_t *sm;

void *R_function(void *arg){
    while(1){
        for(int i = 0; i < MAX_SOCKETS; i++){
            if(sm[i].used){
                if(sm[i].bound){
                    //receive data
                    struct sockaddr_in src_addr;
                    int src_addr_len = sizeof(src_addr);
                    int bytes_received = recvfrom(sm[i].sockfd, sm[i].recv_buff[0], MAX_LEN, 0, (struct sockaddr *)&src_addr, &src_addr_len);
                    if(bytes_received < 0){
                        sm[i].recv_buff[0][0] = '\0';
                    }
                    else{
                        sm[i].recv_buff[0][bytes_received] = '\0';
                    }
                }
            }
        }
    }
}

void *S_function(void *arg){
    while(1){
        for(int i = 0; i < MAX_SOCKETS; i++){
            if(sm[i].used){
                if(sm[i].bound){
                    //send data
                    if(sm[i].send_buff[0][0] != '\0'){
                        int bytes_sent = sendto(sm[i].sockfd, sm[i].send_buff[0], strlen(sm[i].send_buff[0]), 0, (struct sockaddr *)&sm[i].destination_addr, sizeof(sm[i].destination_addr));
                        if(bytes_sent < 0){
                            sm[i].send_buff[0][0] = '\0';
                        }
                        else{
                            sm[i].send_buff[0][0] = '\0';
                        }
                    }
                }
            }
        }
    }
}

void *G_function(void *arg){
    while(1){
        for(int i = 0; i < MAX_SOCKETS; i++){
            if(sm[i].used){
                if(sm[i].bound){
                    //garbage collection
                    for(int j = 0; j < MAX_SEND_BUFF_SIZE; j++){
                        if(sm[i].send_buff[j][0] != '\0'){
                            int bytes_sent = sendto(sm[i].sockfd, sm[i].send_buff[j], strlen(sm[i].send_buff[j]), 0, (struct sockaddr *)&sm[i].destination_addr, sizeof(sm[i].destination_addr));
                            if(bytes_sent < 0){
                                sm[i].send_buff[j][0] = '\0';
                            }
                            else{
                                sm[i].send_buff[j][0] = '\0';
                            }
                        }
                    }
                }
            }
        }
    }
}

int main(){

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
    if(sm == (void *)-1){
        perror("shmat");
        exit(1);
    }

    pthread_t thread_R, thread_S, thread_G; //receive thread, send thread, garbage collector thread

    pthread_create(&thread_R, NULL, R_function, NULL);
    pthread_create(&thread_S, NULL, S_function, NULL);
    pthread_create(&thread_G, NULL, G_function, NULL);

    pthread_join(thread_R, NULL);
    pthread_join(thread_S, NULL);
    pthread_join(thread_G, NULL);

    //destroy shared memory
    if(shmdt(sm) == -1){
        perror("shmdt");
        exit(1);
    }

    if(shmctl(shmid, IPC_RMID, NULL) == -1){
        perror("shmctl");
        exit(1);
    }

    return 0;    
}