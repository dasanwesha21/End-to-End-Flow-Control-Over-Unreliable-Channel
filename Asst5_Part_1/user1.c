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

int main(){
    int sockfd = m_socket(AF_INET, SOCK_MTP, 0);
    if(sockfd < 0){
        perror("m_socket");
        exit(1);
    }

    int bind = m_bind(sockfd, INADDR_ANY, 8080, INADDR_ANY, 8081);
    if(bind < 0){
        perror("m_bind");
        exit(1);
    }

    char send_buff[MAX_LEN] = "Hello";
    ssize_t send = m_sendto(sockfd, send_buff, strlen(send_buff), 0, NULL);

    char recv_buff[MAX_LEN];
    memset(recv_buff, 0, MAX_LEN);

    ssize_t recv = m_recvfrom(sockfd, recv_buff, MAX_LEN, 0, NULL);
    if(!strcmp(recv_buff, "Bye")){
        printf("Error in receive\n");
    }

    int close = m_close(sockfd);

    return 0;
}