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

    int bind = m_bind(sockfd, "127.0.0.1" , 8080, "127.0.0.1", 8081);
    if(bind < 0){
        perror("m_bind");
        exit(1);
    }

    struct sockaddr_in dest_addr;
    dest_addr.sin_family = AF_INET;
    dest_addr.sin_port = htons(8081);
    dest_addr.sin_addr.s_addr = inet_addr("127.0.0.1");

    for (int i = 0; i < 100; i++){
        char send_buff[MAX_LEN];
        sprintf(send_buff, "Sender message %d\n", i);
        ssize_t send = m_sendto(sockfd, send_buff, strlen(send_buff), 0, &dest_addr);
        if(send > 0){
            printf("Message written : %ld\n", send);
            fflush(stdout);
        }else if(send <= 0){
            printf("Error in send\n");
            fflush(stdout);
            exit(1);
        }
    }

    while(1);

    int close = m_close(sockfd);

    return 0;
}