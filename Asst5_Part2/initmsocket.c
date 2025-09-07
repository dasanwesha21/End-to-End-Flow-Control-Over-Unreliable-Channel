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
#include <sys/sem.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/socket.h>
#include <sys/mman.h>
#include <errno.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include "msocket.h"
#include <sys/select.h>

pthread_mutex_t mutex; // for send and receive 

int minfunc(int a, int b){
    if(a < b){
        return a;
    }else{
        return b;
    }
}

void *R_function(void *arg){ //receive thread
    int key = ftok(SHM_NAME, 'R');
    if(key < 0){
        exit(1);
    }

    int shmid = shmget(key, sizeof(socket_t) * MAX_SOCKETS, 0666);
    if(shmid < 0){
        exit(1);
    }

    socket_t *sm = (socket_t *)shmat(shmid, NULL, 0);
    if(sm == (void *)-1){
        exit(1);
    }

    fd_set readfds;
    int max_sd = 0;
    struct timeval tv;
    FD_ZERO(&readfds);

    tv.tv_sec = 5;
    tv.tv_usec = 0;

    for(int i = 0; i < MAX_SOCKETS; i++){
        if(sm[i].used){
            FD_SET(sm[i].sockfd, &readfds);
            if(sm[i].sockfd > max_sd){
                max_sd = sm[i].sockfd;
            }
        }
    }

    int flg_key = ftok(NO_SPACE, 'R');
    int flg_id = shmget(flg_key, MAX_SOCKETS * sizeof(int), 0666);

    int *flg = (int *)shmat(flg_id, NULL, 0);

    // printf("rthread: started\n");
    // fflush(stdout);

    while(1) {
        int activity = select(max_sd + 1, &readfds, NULL, NULL, &tv);

        pthread_mutex_lock(&mutex);
        // printf("rthread: mutex acquired\n");
        // fflush(stdout);
        
        if(activity){
            // printf("in r thread: activity\n");
            // fflush(stdout);
            for(int i = 0; i < MAX_SOCKETS; i++){
                if(sm[i].used){
                    if(sm[i].bound){
                        if(FD_ISSET(sm[i].sockfd, &readfds)){
                            //rearrange the receive buffer and recv window and update rwnd size 
                            // printf("rthread: activity for idx: %d\n", i);
                            // fflush(stdout);
                                        
                            int k = 0;
                            for(int j = 0; j < MAX_RECV_BUFF_SIZE; j++){
                                if(sm[i].recv_buff[j][0] == '\0'){
                                    //do nothing
                                }else{
                                    memcpy(sm[i].recv_buff[k], sm[i].recv_buff[j], strlen(sm[i].recv_buff[j]));
                                    memset(sm[i].recv_buff[j], 0, MAX_LEN);
                                    k++;
                                }
                            }

                            //k points to the first empty buffer
                            // printf("rthread: k = %d\n", k);
                            // fflush(stdout);

                            //update rwnd size and win_end
                            if(k != MAX_RECV_BUFF_SIZE){
                                //receive buffer is empty, partially or fully
                                if(sm[i].rwnd.interim_pointer == -1){
                                    if(sm[i].rwnd.win_end == -1){
                                        //no data in window
                                        sm[i].rwnd.win_size = WINDOW_SIZE;
                                    }else{
                                        int count = 0;
                                        for(int m = 0; m < WINDOW_SIZE; m++){
                                            if(sm[i].rwnd.window[(sm[i].rwnd.win_end + m) % MAX_SEQ_NO][0] == '\0'){
                                                count++;
                                            }
                                        }
                                        sm[i].rwnd.win_size = count;
                                        if(count == 0){
                                            flg[i] = 1;
                                        }
                                    }
                                }else if(sm[i].rwnd.interim_pointer == sm[i].rwnd.win_end){
                                    //all data in window are of type received and acked, can send data to buffer
                                    int l = (sm[i].rwnd.win_start - 1 + MAX_SEQ_NO) % MAX_SEQ_NO;
                                    while(l != sm[i].rwnd.win_end && k <= MAX_RECV_BUFF_SIZE){
                                        l = (l + 1) % MAX_SEQ_NO;

                                        memcpy(sm[i].recv_buff[k], sm[i].rwnd.window[l], strlen(sm[i].rwnd.window[l]));
                                        memset(sm[i].rwnd.window[l], 0, MAX_LEN);
                                        k++;
                                    }
                                    //update
                                    if(l = sm[i].rwnd.win_end){
                                        sm[i].rwnd.win_start = (sm[i].rwnd.win_end + 1) % MAX_SEQ_NO;                                        
                                        sm[i].rwnd.win_end = sm[i].rwnd.interim_pointer = -1; //no data in window
                                        sm[i].rwnd.win_size = WINDOW_SIZE;
                                    }else{
                                        for(int m = 0; m < MAX_SEQ_NO; m++){
                                            if(sm[i].rwnd.window[(sm[i].rwnd.win_start + m) % MAX_SEQ_NO][0] != '\0'){
                                                //this is the start 
                                                sm[i].rwnd.win_start = (sm[i].rwnd.win_start + m) % MAX_SEQ_NO;
                                            }
                                        }
                                    
                                        sm[i].rwnd.win_size = WINDOW_SIZE - (sm[i].rwnd.win_end - sm[i].rwnd.win_start + 1 + MAX_SEQ_NO) % MAX_SEQ_NO;
                                        if(sm[i].rwnd.win_size == 0){
                                            flg[i] = 1;
                                        }                                        
                                    }
                                }else{
                                    //transfer data from start to interim pointer to buffer
                                    int l = (sm[i].rwnd.win_start - 1 + MAX_SEQ_NO) % MAX_SEQ_NO;
                                    while(l != sm[i].rwnd.interim_pointer && k != MAX_RECV_BUFF_SIZE){
                                        l = (l + 1) % MAX_SEQ_NO;

                                        memcpy(sm[i].recv_buff[k], sm[i].rwnd.window[l], strlen(sm[i].rwnd.window[l]));
                                        memset(sm[i].rwnd.window[l], 0, MAX_LEN);
                                        k++;
                                    }
                                    //update
                                    if(l = sm[i].rwnd.interim_pointer){
                                        sm[i].rwnd.win_size += (sm[i].rwnd.interim_pointer - sm[i].rwnd.win_start + 1 + MAX_SEQ_NO) % MAX_SEQ_NO;
                                        sm[i].rwnd.win_start = (sm[i].rwnd.interim_pointer + 1) % MAX_SEQ_NO;
                                        sm[i].rwnd.interim_pointer = -1;
                                    }else{
                                        int temp = sm[i].rwnd.win_start;
                                        for(int m = 0; m < MAX_SEQ_NO; m++){
                                            if(sm[i].rwnd.window[((sm[i].rwnd.win_start + m) % MAX_SEQ_NO)][0] != '\0'){
                                                //this is the start 
                                                sm[i].rwnd.win_start = (sm[i].rwnd.win_start + m) % MAX_SEQ_NO;
                                            }
                                        }
                                        sm[i].rwnd.win_size += (sm[i].rwnd.win_start - temp + MAX_SEQ_NO) % MAX_SEQ_NO;                                    
                                    }
                                }
                            }else if(k == MAX_RECV_BUFF_SIZE){
                                //receive buffer is full
                                if(sm[i].rwnd.interim_pointer == -1){
                                    if(sm[i].rwnd.win_end == -1){
                                        //no data in window
                                        sm[i].rwnd.win_size = WINDOW_SIZE;
                                        flg[i] = 0;
                                    }else{
                                        //all data is of received but not acked type, cannot send any data to buffer
                                        sm[i].rwnd.win_size = 0;
                                        flg[i] = 1;
                                    }
                                }
                            }

                            // printf("rtherad: win_start = %d, win_end = %d, win_size = %d, interim_pointer = %d\n", sm[i].rwnd.win_start, sm[i].rwnd.win_end, sm[i].rwnd.win_size, sm[i].rwnd.interim_pointer);
                            // fflush(stdout);


                            //receive the message
                            char buff[MAX_LEN + 1]; //maximum length of data + 1 byte for header, ACK is of 3 bytes
                            memset(buff, 0, sizeof(buff));
                            socklen_t sz = sizeof(sm[i].destination_addr);
                            int bytes_received = recvfrom(sm[i].sockfd, buff, MAX_LEN, 0, (struct sockaddr *)&sm[i].destination_addr, &sz);

                            // printf("rthread: message received: %s\n", buff);
                            // fflush(stdout);

                            //check the first byte of buffer, if between 0 to 15, it is a data message, else an ACK
                            int seq_no = buff[0];
                            // printf("rthread: seq_no = %d\n", seq_no);
                            // fflush(stdout);
                            if( seq_no >= 0 && seq_no < MAX_SEQ_NO){
                                //data message: receiver side
                                if(flg[i] == 1){
                                    //window size is 0, send ack of the last in order message
                                    char ack[4];
                                    // sprintf(&ack[0], "%d", ACK_IDENTIFIER);
                                    // sprintf(&ack[1], "%d", sm[i].rwnd.win_size); //window size
                                    ack[0] = ACK_IDENTIFIER;
                                    ack[1] = sm[i].rwnd.win_size;
                                    if(sm[i].rwnd.interim_pointer == -1){                                        
                                        // sprintf(&ack[2], "%d", (sm[i].rwnd.win_start - 1 + MAX_SEQ_NO) % MAX_SEQ_NO);
                                        ack[2] = (char)((sm[i].rwnd.win_start - 1 + MAX_SEQ_NO) % MAX_SEQ_NO);
                                    }else{
                                        while(1){
                                            if(sm[i].rwnd.window[(sm[i].rwnd.interim_pointer + 1) % MAX_SEQ_NO][0] != '\0'){
                                                sm[i].rwnd.interim_pointer = (sm[i].rwnd.interim_pointer + 1) % MAX_SEQ_NO;
                                            }else{
                                                break;
                                            }
                                        }
                                        // sprintf(&ack[2], "%d", sm[i].rwnd.interim_pointer);
                                        ack[2] = sm[i].rwnd.interim_pointer;
                                    }

                                    // printf("rthread: flag set, sending ack: %d %d %d\n", atoi(&ack[0]), atoi(&ack[1]), atoi(&ack[2]));
                                    // fflush(stdout);

                                    ack[3] = '\0';

                                    int bytes_sent = sendto(sm[i].sockfd, ack, strlen(ack), 0, (struct sockaddr *)&sm[i].destination_addr, sizeof(sm[i].destination_addr));
                                    if(bytes_sent < 0){
                                        printf("rthread: error in sending\n");
                                    }

                                    memset(ack, 0, sizeof(ack));
                                    continue;
                                }

                                //check if sequence number is in window or not
                                int temp = (sm[i].rwnd.win_start + sm[i].rwnd.win_size - 1) % MAX_SEQ_NO;
                                if((sm[i].rwnd.win_start <= temp && seq_no >= sm[i].rwnd.win_start && seq_no <= temp) || (sm[i].rwnd.win_start > temp && (seq_no > sm[i].rwnd.win_start || seq_no < temp))){
                                    //check if it is a duplicate message
                                    if(sm[i].rwnd.window[seq_no][0] != '\0'){
                                        char ack[4];
                                        // sprintf(&ack[0], "%d", ACK_IDENTIFIER);
                                        // sprintf(&ack[1], "%d", sm[i].rwnd.win_size); //window size
                                        ack[0] = ACK_IDENTIFIER;
                                        ack[1] = sm[i].rwnd.win_size;
                                        if(sm[i].rwnd.interim_pointer == -1){                                        
                                            // sprintf(&ack[2], "%d", (sm[i].rwnd.win_start - 1 + MAX_SEQ_NO) % MAX_SEQ_NO);
                                            // ack[2] = ((sm[i].rwnd.win_start - 1 + MAX_SEQ_NO) % MAX_SEQ_NO + '0');
                                            ack[2] = (char)((sm[i].rwnd.win_start - 1 + MAX_SEQ_NO) % MAX_SEQ_NO);
                                        }else{
                                            while(1){
                                                if(sm[i].rwnd.window[(sm[i].rwnd.interim_pointer + 1) % MAX_SEQ_NO][0] != '\0'){
                                                    sm[i].rwnd.interim_pointer = (sm[i].rwnd.interim_pointer + 1) % MAX_SEQ_NO;
                                                }else{
                                                    break;
                                                }
                                            }
                                            // sprintf(&ack[2], "%d", sm[i].rwnd.interim_pointer);
                                            // ack[2] = (sm[i].rwnd.interim_pointer + '0');
                                            ack[2] = sm[i].rwnd.interim_pointer;
                                        }

                                        // printf("rthread: duplicate msg, sending ack:  %d %d %d\n", atoi(&ack[0]), atoi(&ack[1]), atoi(&ack[2]));
                                        // fflush(stdout);

                                        ack[3] = '\0';

                                        int bytes_sent = sendto(sm[i].sockfd, ack, strlen(ack), 0, (struct sockaddr *)&sm[i].destination_addr, sizeof(sm[i].destination_addr));
                                        if(bytes_sent < 0){
                                            printf("rthread: error in sending\n");
                                        }

                                        memset(ack, 0, sizeof(ack));
                                        continue;

                                    }else{
                                        //add the data to the window, after removing the header
                                        memcpy(sm[i].rwnd.window[seq_no], &buff[1], strlen(&buff[1]));

                                        //update window size and end
                                        sm[i].rwnd.win_size -= 1;
                                        if(sm[i].rwnd.win_end == -1){
                                            sm[i].rwnd.win_end = seq_no;
                                        }else{
                                            //if seq_no comes later in the window
                                            if((sm[i].rwnd.win_start + sm[i].rwnd.win_size - 1) % MAX_SEQ_NO > sm[i].rwnd.win_start){
                                                if(seq_no > sm[i].rwnd.win_end && seq_no <= ((sm[i].rwnd.win_start + sm[i].rwnd.win_size - 1) % MAX_SEQ_NO)){
                                                    sm[i].rwnd.win_end = seq_no;
                                                }
                                            }else{
                                                if(sm[i].rwnd.win_end < sm[i].rwnd.win_start){
                                                    if(seq_no > sm[i].rwnd.win_end && seq_no <= (sm[i].rwnd.win_start + sm[i].rwnd.win_size - 1) % MAX_SEQ_NO){
                                                        sm[i].rwnd.win_end = seq_no;
                                                    }
                                                }else{
                                                    if(seq_no > sm[i].rwnd.win_end || seq_no <= (sm[i].rwnd.win_start + sm[i].rwnd.win_size - 1) % MAX_SEQ_NO){
                                                        sm[i].rwnd.win_end = seq_no;
                                                    }
                                                }
                                            }
                                        }

                                        char ack[4];
                                        // sprintf(&ack[0], "%d", ACK_IDENTIFIER);
                                        // sprintf(&ack[1], "%d", sm[i].rwnd.win_size); //window size
                                        ack[0] = ACK_IDENTIFIER;
                                        ack[1] = sm[i].rwnd.win_size;
                                        if(sm[i].rwnd.interim_pointer == -1){
                                            // ack[2] = ((sm[i].rwnd.win_start - 1 + MAX_SEQ_NO) % MAX_SEQ_NO + '0');
                                            ack[2] = (char)((sm[i].rwnd.win_start - 1 + MAX_SEQ_NO) % MAX_SEQ_NO);
                                        }else{
                                            while(1){
                                                if(sm[i].rwnd.window[(sm[i].rwnd.interim_pointer + 1) % MAX_SEQ_NO][0] != '\0'){
                                                    sm[i].rwnd.interim_pointer = (sm[i].rwnd.interim_pointer + 1) % MAX_SEQ_NO;
                                                }else{
                                                    break;
                                                }
                                            }
                                            // sprintf(&ack[2], "%d", sm[i].rwnd.interim_pointer);
                                            // ack[2] = (sm[i].rwnd.interim_pointer + '0');
                                            ack[2] = sm[i].rwnd.interim_pointer;
                                        }

                                        // printf("rthread: normal, sending ack: %d %d %d\n", atoi(&ack[0]), atoi(&ack[1]), atoi(&ack[2]));
                                        // fflush(stdout);

                                        ack[3] = '\0';

                                        int bytes_sent = sendto(sm[i].sockfd, ack, strlen(ack), 0, (struct sockaddr *)&sm[i].destination_addr, sizeof(sm[i].destination_addr));
                                        if(bytes_sent < 0){
                                            printf("rthread: error in sending\n");
                                        }
                                        memset(ack, 0, sizeof(ack));
                                    }
                                }else{
                                    char ack[4];
                                    // sprintf(&ack[0], "%d", ACK_IDENTIFIER);
                                    // sprintf(&ack[1], "%d", sm[i].rwnd.win_size); //window size
                                    ack[0] = ACK_IDENTIFIER;
                                    ack[1] = sm[i].rwnd.win_size;
                                    if(sm[i].rwnd.interim_pointer == -1){
                                        // ack[2] = ((sm[i].rwnd.win_start - 1 + MAX_SEQ_NO) % MAX_SEQ_NO + '0');
                                        ack[2] = (char)((sm[i].rwnd.win_start - 1 + MAX_SEQ_NO) % MAX_SEQ_NO);
                                    }else{
                                        while(1){
                                            if(sm[i].rwnd.window[(sm[i].rwnd.interim_pointer + 1) % MAX_SEQ_NO][0] != '\0'){
                                                sm[i].rwnd.interim_pointer = (sm[i].rwnd.interim_pointer + 1) % MAX_SEQ_NO;
                                            }else{
                                                break;
                                            }
                                        }
                                        // ack[2] = (sm[i].rwnd.interim_pointer + '0');
                                        ack[2] = sm[i].rwnd.interim_pointer;
                                    }

                                    // printf("rthread: seq_no not in window, sending ack:  %d %d %d\n", atoi(&ack[0]), atoi(&ack[1]), atoi(&ack[2]));
                                    // fflush(stdout);

                                    ack[3] = '\0';

                                    int bytes_sent = sendto(sm[i].sockfd, ack, strlen(ack), 0, (struct sockaddr *)&sm[i].destination_addr, sizeof(sm[i].destination_addr));
                                    if(bytes_sent < 0){
                                        printf("rthread: error in sending\n");
                                    }

                                    memset(ack, 0, sizeof(ack));
                                    continue;
                                }

                            }else{
                                //ACK message: sender side
                                // printf("rthread: ack received %d %d %d\n", buff[0], buff[1], buff[2]);
                                // fflush(stdout);

                                int size_rwnd = buff[1];
                                seq_no = buff[2];                                

                                //check if sequence number is in window or not
                                int temp = (sm[i].swnd.win_start + sm[i].swnd.win_size - 1) % MAX_SEQ_NO;
                                if(sm[i].swnd.win_start <= temp && seq_no >= sm[i].swnd.win_start && seq_no <= temp){
                                    //update start(this is a cumulative ack)
                                    sm[i].swnd.win_start = (seq_no + 1) % MAX_SEQ_NO;
                                }else if(sm[i].swnd.win_start > temp && (seq_no > sm[i].swnd.win_start || seq_no < temp)){
                                    //update start(this is a cumulative ack)
                                    sm[i].swnd.win_start = (seq_no + 1) % MAX_SEQ_NO;
                                }else{
                                    //not in window, drop the ack
                                }

                                //update window size
                                sm[i].swnd.win_size = size_rwnd;
                            }                            
                        }
                    }
                }
            }
            FD_ZERO(&readfds);
            for(int i = 0; i < MAX_SOCKETS; i++){
                if(sm[i].used){
                    if(sm[i].bound){
                        FD_SET(sm[i].sockfd, &readfds);
                        if(sm[i].sockfd > max_sd){
                            max_sd = sm[i].sockfd;
                        }
                    }
                }
            }  
            tv.tv_sec = 5;
            tv.tv_usec = 0;      
        }
        
        if(activity == 0){
            FD_ZERO(&readfds);
            for(int i = 0; i < MAX_SOCKETS; i++){
                if(sm[i].used){
                    if(sm[i].bound){
                        // printf("in r thread: no activity\n");
                        // fflush(stdout);
                        if(flg[i] == 1){ //-----------------------------------------------------------------------if error in timeout try removing this check
                            //check if rwnd has space
                            int space_count = 0;
                            for(int j = 0; j < WINDOW_SIZE; j++){
                                if(sm[i].rwnd.window[(sm[i].rwnd.win_start + j) % MAX_SEQ_NO][0] == '\0'){
                                    space_count++;
                                }
                            }

                            if(space_count > 0){
                                //send ack of the last in order message
                                // printf("rthread: sending ack and resetting flag\n");
                                // fflush(stdout);

                                char ack[4];
                                sprintf(&ack[0], "%d", ACK_IDENTIFIER);
                                sprintf(&ack[1], "%d", sm[i].rwnd.win_size); //window size
                                if(sm[i].rwnd.interim_pointer == -1){                                        
                                    // sprintf(&ack[2], "%d", (sm[i].rwnd.win_start - 1 + MAX_SEQ_NO) % MAX_SEQ_NO);
                                    ack[2] = ((sm[i].rwnd.win_start - 1 + MAX_SEQ_NO) % MAX_SEQ_NO + '0');
                                }else{
                                    while(1){
                                        if(sm[i].rwnd.window[(sm[i].rwnd.interim_pointer + 1) % MAX_SEQ_NO][0] != '\0'){
                                            sm[i].rwnd.interim_pointer = (sm[i].rwnd.interim_pointer + 1) % MAX_SEQ_NO;
                                        }else{
                                            break;
                                        }
                                    }
                                    // sprintf(&ack[2], "%d", sm[i].rwnd.interim_pointer);
                                    ack[2] = (sm[i].rwnd.interim_pointer + '0');
                                }

                                printf("rthread: reset flag and ack:  %d %d %d\n", atoi(&ack[0]), atoi(&ack[1]), atoi(&ack[2]));
                                fflush(stdout);

                                ack[3] = '\0';

                                int bytes_sent = sendto(sm[i].sockfd, ack, strlen(ack), 0, (struct sockaddr *)&sm[i].destination_addr, sizeof(sm[i].destination_addr));
                                if(bytes_sent < 0){
                                    printf("rthread: error in sending\n");
                                }

                                memset(ack, 0, sizeof(ack));
                            }
                        }
                        FD_SET(sm[i].sockfd, &readfds);
                        if(sm[i].sockfd > max_sd){
                            max_sd = sm[i].sockfd;
                        }
                    }
                }
            }
            tv.tv_sec = 5;
            tv.tv_usec = 0;
        }

        if(activity < 0){
            exit(1);
        }

        pthread_mutex_unlock(&mutex);
        // printf("rthread: mutex released\n");
        // fflush(stdout);
    }
}

void *S_function(void *arg){ //send thread
    int key = ftok(SHM_NAME, 'R');
    if(key < 0){
        exit(1);
    }

    int shmid = shmget(key, sizeof(socket_t) * MAX_SOCKETS, IPC_CREAT | 0666);
    if(shmid < 0){
        exit(1);
    }

    socket_t *sm = (socket_t *)shmat(shmid, NULL, 0);
    if(sm == (void *)-1){
        exit(1);
    }

    // printf("sthread: started\n");
    // fflush(stdout);

    while(1){
        sleep((int)(T/2));
        pthread_mutex_lock(&mutex);
        // printf("sthread: mutex acquired\n");
        // fflush(stdout);

        for(int i = 0; i < MAX_SOCKETS; i++){
            if(sm[i].used){
                if(sm[i].bound){
                    if(sm[i].swnd.win_size > 0){
                        if((sm[i].swnd.win_start + sm[i].swnd.win_size - 1) % MAX_SEQ_NO <= sm[i].swnd.win_end){
                            //as win_end is outside window, data already present
                            //check for timeout
                            int j;
                            int temp = sm[i].swnd.win_size;
                            for(j = 0; j < temp; j++){
                                if(sm[i].swnd.time[(sm[i].swnd.win_start + j) % MAX_SEQ_NO] - time(NULL) >= T){
                                    //timeout has occured, send all the messages in the window
                                    sm[i].swnd.interim_pointer = (sm[i].swnd.win_start + sm[i].swnd.win_size) % MAX_SEQ_NO; //outside window size 

                                    int l = sm[i].swnd.win_start;
                                    for(int k = 0; k < temp; k++){
                                        char buff[MAX_LEN + 1];
                                        // sprintf(buff, "%d%s", l, sm[i].swnd.window[l]);
                                        buff[0] = l;
                                        memcpy(&buff[1], sm[i].swnd.window[l], strlen(sm[i].swnd.window[l]));

                                        int bytes_sent = sendto(sm[i].sockfd, buff, strlen(buff), 0, (struct sockaddr *)&sm[i].destination_addr, sizeof(sm[i].destination_addr));
                                        if(bytes_sent < 0){
                                            printf("sthread: error in sending\n");
                                        }

                                        sm[i].swnd.time[l] = time(NULL);
                                        memset(buff, 0, sizeof(buff));

                                        l = (l + 1) % MAX_SEQ_NO;                                    
                                    }
                                    break;
                                }
                            }
                            //if no timeout, nothing has to be sent
                        }else{
                            // add data to window
                            for(int j = 0; j < MAX_SEND_BUFF_SIZE; j++){
                                if(sm[i].send_buff[j][0] != '\0'){
                                    if(sm[i].swnd.win_end == -1){
                                        sm[i].swnd.win_end = sm[i].swnd.win_start;
                                        sm[i].swnd.interim_pointer = sm[i].swnd.win_start;
                                    }else{
                                        sm[i].swnd.win_end = (sm[i].swnd.win_end + 1) % MAX_SEQ_NO;
                                    }

                                    memcpy(sm[i].swnd.window[sm[i].swnd.win_end], sm[i].send_buff[j], strlen(sm[i].send_buff[j]));
                                    memset(sm[i].send_buff[j], 0, MAX_LEN);

                                    if(sm[i].swnd.win_end == (sm[i].swnd.win_start + sm[i].swnd.win_size - 1) % MAX_SEQ_NO){
                                        break;
                                    }
                                }else{
                                    break;
                                }
                            }

                            //rearrange the send buffer
                            int k = 0;
                            if(sm[i].swnd.win_end == -1){
                                //no data in window to send
                                sm[i].swnd.interim_pointer = -1;
                                continue;
                            }else{
                                for(int j = 0; j < MAX_SEND_BUFF_SIZE; j++){
                                    if(sm[i].send_buff[j][0] != '\0'){
                                        memcpy(sm[i].send_buff[k], sm[i].send_buff[j], strlen(sm[i].send_buff[j]));
                                        memset(sm[i].send_buff[j], 0, MAX_LEN);
                                        k++;
                                    }
                                }

                                //check for timeout
                                int j;
                                int temp = (sm[i].swnd.win_end - sm[i].swnd.win_start + 1 + MAX_SEQ_NO) % MAX_SEQ_NO;
                                for(j = 0; j < temp; j++){
                                    if(sm[i].swnd.time[(sm[i].swnd.win_start + j) % MAX_SEQ_NO] - time(NULL) >= T){
                                        //timeout has occured, send all the messages in the window                                        
                                        sm[i].swnd.interim_pointer = -1; //no more data in window to send

                                        int l = (sm[i].swnd.win_start - 1 + MAX_SEQ_NO) % MAX_SEQ_NO;
                                        while(l != sm[i].swnd.win_end){
                                            l = (l + 1) % MAX_SEQ_NO;

                                            char buff[MAX_LEN + 1];
                                            // sprintf(buff, "%d%s", l, sm[i].swnd.window[l]);
                                            buff[0] = l;
                                            memcpy(&buff[1], sm[i].swnd.window[l], strlen(sm[i].swnd.window[l]));

                                            int bytes_sent = sendto(sm[i].sockfd, buff, strlen(buff), 0, (struct sockaddr *)&sm[i].destination_addr, sizeof(sm[i].destination_addr));
                                            if(bytes_sent < 0){
                                                printf("sthread: error in sending\n");
                                            }

                                            // printf("sthread: sent as timeout: %s, idx: %d, win_idx = %d\n", buff, i, l);
                                            // fflush(stdout);

                                            sm[i].swnd.time[l] = time(NULL);
                                            memset(buff, 0, sizeof(buff));
                                        
                                        }
                                        break;
                                    }
                                }

                                if(j == temp){
                                    //no timeout, send all the data not sent
                                    if(sm[i].swnd.interim_pointer == -1){
                                        // no data to send, waiting for ack
                                    }else{
                                        //send all data from interim pointer to end pointer
                                        int l = (sm[i].swnd.interim_pointer - 1 + MAX_SEQ_NO) % MAX_SEQ_NO;
                                        while(l != sm[i].swnd.win_end){
                                            l = (l + 1) % MAX_SEQ_NO;

                                            char buff[MAX_LEN + 1];
                                            // sprintf(buff, "%d%s", l, sm[i].swnd.window[l]);
                                            buff[0] = l;
                                            memcpy(&buff[1], sm[i].swnd.window[l], strlen(sm[i].swnd.window[l]));

                                            int bytes_sent = sendto(sm[i].sockfd, buff, strlen(buff), 0, (struct sockaddr *)&sm[i].destination_addr, sizeof(sm[i].destination_addr));
                                            if(bytes_sent < 0){
                                                printf("sthread: error in sending\n");
                                            }

                                            // printf("sthread: sent as normal: %s,  idx: %d, win_idx = %d\n", buff, i, l);
                                            // fflush(stdout);

                                            sm[i].swnd.time[l] = time(NULL);
                                            memset(buff, 0, sizeof(buff));                                        
                                        }

                                        sm[i].swnd.interim_pointer = -1; //as all data in window sent
                                    }
                                }
                            }
                        }
                    }else{
                        //no data to send
                    }
                }            
            }
        } 
        pthread_mutex_unlock(&mutex);
        // printf("sthread: mutex released\n");
        // fflush(stdout);
    }
}

void *G_function(void *arg){
    int key = ftok(SHM_NAME, 'R');
    if(key < 0){
        exit(1);
    }

    int shmid = shmget(key, sizeof(socket_t) * MAX_SOCKETS, IPC_CREAT | 0666);
    if(shmid < 0){
        exit(1);
    }

    socket_t *sm = (socket_t *)shmat(shmid, NULL, 0);
    if(sm == (void *)-1){
        exit(1);
    }

    while(1){
        sleep(GTHREAD_TIMEOUT);
        pthread_mutex_lock(&mutex);
        for(int i = 0; i < MAX_SOCKETS; i++){
            if(sm[i].used){
                //check if owner is alive
                int is_alive = kill(sm[i].pid, 0);
                if(is_alive == 0 && errno != ESRCH){
                    //owner is alive
                    continue;
                }else{
                    //owner is dead so remove the socket
                    if(sm[i].bound){
                        close(sm[i].sockfd);
                    }
                    memset(&sm[i], 0, sizeof(socket_t));
                }
            }                
        }
        pthread_mutex_unlock(&mutex);
    }
}

int main(){

    key_t key = ftok(SHM_NAME, 'R');
    if(key < 0){
        perror("ftok");
        exit(1);
    }
    
    int shmid = shmget(key, sizeof(socket_t) * MAX_SOCKETS, IPC_CREAT | 0666);
    if(shmid < 0){
        printf("1");
        perror("shmget");
        exit(1);
    }

    socket_t *sm = (socket_t *)shmat(shmid, NULL, 0);
    if(sm == (void *)-1){
        perror("shmat");
        exit(1);
    }
    memset(sm, 0, sizeof(socket_t) * MAX_SOCKETS);

    pthread_mutex_init(&mutex, NULL);
    pthread_mutex_trylock(&mutex);
    pthread_mutex_unlock(&mutex);

    int semid1, semid2;
    int key1, key2;
    key1 = ftok(SEM_NAME, 'R');
    key2 = ftok(SEM_NAME, 'S');

    semid1 = semget(key1, 1, 0666 | IPC_CREAT);
    semid2 = semget(key2, 1, 0666 | IPC_CREAT);

    semctl(semid1, 0, SETVAL, 0);
    semctl(semid2, 0, SETVAL, 0);

    if(semid1 < 0 || semid2 < 0){
        perror("semget");
        exit(1);
    }

    int sock_key = ftok(SOCK_NAME, 'R');
    if (sock_key < 0){
        perror("ftok");
        exit(1);
    }

    int sock_info_id = shmget(sock_key, sizeof(sock_info), IPC_CREAT | 0666);

    if(sock_info_id < 0){
        printf("2");
        perror("shmget");
        exit(1);
    }

    sock_info * SOCK_INFO = (sock_info *)shmat(sock_info_id, NULL, 0);
    if(SOCK_INFO == (void *)-1){
        perror("shmat");
        exit(1);
    }

    int flg_key = ftok(NO_SPACE, 'R');
    int flg_id = shmget(flg_key, MAX_SOCKETS * sizeof(int), IPC_CREAT | 0666);

    int *flg = (int *)shmat(flg_id, NULL, 0);
    for(int i = 0; i < MAX_SOCKETS; i++){
        flg[i] = 0;
    }

    pthread_t thread_R, thread_S, thread_G; //receive thread, send thread, garbage collector thread

    pthread_create(&thread_R, NULL, R_function, NULL);
    pthread_create(&thread_S, NULL, S_function, NULL);
    pthread_create(&thread_G, NULL, G_function, NULL);

    while(1){
        //wait on sem1
        struct sembuf sb1; //wait
        sb1.sem_num = 0;
        sb1.sem_op = -1;
        sb1.sem_flg = 0;

        struct sembuf sb2; //signal
        sb2.sem_num = 0;
        sb2.sem_op = 1;
        sb2.sem_flg = 0;

        semop(semid1, &sb1, 1); //wait on sem1
        printf("initmsocket: sem1 signalled to open\n");

        if(SOCK_INFO->ip == 0 && SOCK_INFO->port == 0 && SOCK_INFO->sock_id == 0){
            // printf("initmsocket: m_socket call\n");
            // fflush(stdout);
            // Create a UDP socket
            int sockfd = socket(AF_INET, SOCK_DGRAM, 0);
            if (sockfd < 0) {
                errno = ESOCKTNOSUPPORT;
                SOCK_INFO->err_no = errno;
                SOCK_INFO->sock_id = -1;
                SOCK_INFO->ip = 0;
                SOCK_INFO->port = 0;
            }else{
                SOCK_INFO->sock_id = sockfd;
                SOCK_INFO->err_no = 0;
            }

            // printf("initmsocket: signalling sem2 to open, %d\n", SOCK_INFO->sock_id);
            // fflush(stdout);
            semop(semid2, &sb2, 1); //signal on sem2

        }else{
            //m_bind call
            // printf("initmsocket: m_bind call\n");
            // fflush(stdout);

            struct sockaddr_in source_addr;
            source_addr.sin_family = AF_INET;
            source_addr.sin_port = SOCK_INFO->port;
            source_addr.sin_addr.s_addr = SOCK_INFO->ip;

            int ret = bind(SOCK_INFO->sock_id, (struct sockaddr *)&source_addr, sizeof(source_addr));

            if (ret < 0) {
                SOCK_INFO->err_no = errno;
                SOCK_INFO->sock_id = -1;
                SOCK_INFO->ip = 0;
                SOCK_INFO->port = 0;            
            }else{
                // printf("initmsocket: m_bind successful\n");
                // fflush(stdout);
            }

            // printf("initmsocket: signalling sem2 to open\n");
            // fflush(stdout);
            semop(semid2, &sb2, 1);            
        }
    }

    pthread_join(thread_R, NULL);
    pthread_join(thread_S, NULL);
    // pthread_join(thread_G, NULL);

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