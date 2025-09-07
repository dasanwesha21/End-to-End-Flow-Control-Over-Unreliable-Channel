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

    int bind = m_bind(sockfd, "127.0.0.1", 8081, "127.0.0.1", 8080);
    if(bind < 0){
        perror("m_bind");
        exit(1);
    }

    char recv_buff[MAX_LEN];
    memset(recv_buff, 0, MAX_LEN);

    struct sockaddr_in src_addr;
    src_addr.sin_family = AF_INET;
    src_addr.sin_port = htons(8080);
    src_addr.sin_addr.s_addr = inet_addr("127.0.0.1");

    ssize_t recv;
    while(1){
        sleep(2);
        recv = m_recvfrom(sockfd, recv_buff, MAX_LEN, 0, &src_addr);
        if(recv > 0){
            printf("Message received : %s\n", recv_buff);
            break;
        }
    } 
    
    int close = m_close(sockfd);

    return 0;

}