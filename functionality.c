#include "header.h"


/**
 * This function is responsible for dealing with the local clients.
 *
 * @param client_
 * @return
 */
void * handleLocalClient(void * client__){
    int * client_ = client__;
    int client = *client_;
    pthread_mutex_unlock(&setup_mutex);

    void * payload = NULL;
    void * bytestream = NULL;
    struct metaData info;

    while(1){
        //printf("[Local] Ready to receive \n");
        //Wait for local client to make request
        bytestream = handleHandShake(client, sizeof(struct metaData));
        memcpy(&info,bytestream,sizeof(struct metaData));
        //Handle request accordingly
        switch (info.action){
            case 0:
                //client wants to send data to server (Copy)

                payload = receiveData(client,info.msg_size);

                //printf("[Local] Client wants to copy region %d with size %zd\n",info.region,info.msg_size);

                //***************CRITICAL REGION****************
                pthread_mutex_lock(&mutex[info.region]);
                setLocalRegion(info.region,payload,info.msg_size,NULL);
                new_data[info.region] = true;
                //Signal RegionWatch to spread new data
                pthread_cond_signal(&cond[info.region]);
                pthread_mutex_unlock(&mutex[info.region]);
                //***************CRITICAL REGION****************

                printClipboard();

                break;
            case 1:
                //client is requesting data from server (Paste)

                //printf("[Local] Client wants to paste region %d\n",info.region);

                //***************CRITICAL REGION****************
                pthread_rwlock_rdlock(&rwlocks[info.region]);

                info = getLocalClipboardInfo(info.region);
                payload = getLocalClipboardData(info.region);

                pthread_rwlock_unlock(&rwlocks[info.region]);
                //***************CRITICAL REGION****************

                memcpy(bytestream,&info, sizeof(struct metaData));
                handShake(client,bytestream,sizeof(struct metaData));
                sendData(client,info.msg_size,payload);

                //printf("[Local] Paste completed\n");
                break;
            case 2:
                //client is logging out
                printf("[Local] Client is exiting\n");
                if(bytestream != NULL){
                    free(bytestream);
                }
                pthread_exit(NULL);
                break;
                /*case 3:
                    //client is requesting wait
                    printf("[Local] Client wants to wait for a new addition to region %d\n",info.region);
                    /***************CRITICAL REGION****************
                     pthread_mutex_lock(&mutex[info.region]);

                     while(!new_data[info.region]){  //FIXME
                         pthread_cond_wait(&cond[info.region],&mutex[info.region]);
                     }

                    pthread_rwlock_rdlock(&rwlocks[info.region]);
                    info.msg_size = clipboard[info.region].size;
                    //Informar cliente do tamanho da mensagem
                    memcpy(bytestream_pst,&info,sizeof(struct metaData));
                    handShake(client,bytestream_pst,sizeof(struct metaData));

                    //this sucks, but other option is to malloc which is shit too!!
                    sendData(client,info.msg_size,clipboard[info.region].payload);
                    pthread_rwlock_unlock(&rwlocks[info.region]);
                    pthread_mutex_unlock(&mutex[info.region]);
                    /***************CRITICAL REGION****************
                    break;
                     }*/
            default:
                //Error:
                if(bytestream != NULL){
                    free(bytestream);
                }
                pthread_exit(NULL);
                break;
        }
        if(bytestream != NULL){
            free(bytestream);
        }
    }
}

/**
 * This function waits for connections to the internet socket and launches threads to deal with them
 *
 * @param parent_
 * @return
 */
void * ClipHub (void * useless){
    int sock, port, err, id =1;
    pthread_t clipboard_comm;
    int clip_read,clip_write;
    struct node * listNode = NULL;
    struct argument * sonArg = malloc(sizeof(struct argument));
    
    //Generating random socket
    srand(getpid());
    port = rand()%63714 + 1024; //generate random port between 1024 and 64738
    //Create Socket
    sock = createSocket(AF_INET,SOCK_STREAM);
    //Bind Local socket
    InternetServerSocket(sock, port ,5);
    printf("[Cliphub] Waiting for connections on port : %d \n", port);

    
    sonArg->isParent=false;

    //Wait for remote clipboard connections
    while(1){
        //printf("[Cliphub] Ready to accept \n");
        if((clip_read = accept(sock, NULL, NULL)) == -1) {
            perror("accept");
            exit(-1);
        }
        sonArg->id = id;
        sonArg->sock = clip_read;
        printf("[Cliphub] New clipboard connected \n");
        //After a remote clipboard connects, it creates 2 sockets with it:
        // One to receive updates
        if(pthread_create(&clipboard_comm, NULL, handleClipboard,sonArg ) != 0){
            perror("[Cliphub] Creating thread");
            exit(-1);
        }
        //And one to spread updates whenever there's one
        if((clip_write = accept(sock, NULL, NULL)) == -1) {
            perror("[Cliphub] accept");
            exit(-1);
        }
        listNode = malloc(sizeof(struct node));
        listNode->sock = clip_write;
        listNode->id = id++;
        printList();
        head = criaNovoNoLista(head,listNode,&err);
        printList();
        if(err !=0){
            //FIXME handle error
            printf("\t[ClipHub] !!!!!!!!!!!! Error creating new node\n");
        }

    }
    pthread_exit(NULL);
}


/**
 * This function is responsible for receiving information from other clipboards and handling
 * the data accordingly: Either save it to the local clipboard or refusing it.
 *
 * @param arg
 * @return
 */
void * handleClipboard(void * arg){
    struct argument * arg_ = arg;
    struct argument remoteClipboard;
    remoteClipboard.sock = arg_->sock;
    remoteClipboard.isParent = arg_->isParent;
    remoteClipboard.id = arg_->id;
    //FIXME we have to create a lock for this passage of arguments
    void * payload = NULL;
    struct metaData info;
    int error=0, logout = 0;

    printf("[HandleClipboard]\n");
    /*Whenever a remote clipboard connects to this one,
     the current local clipboard is send out*/
    if(!remoteClipboard.isParent){
        printf("[HandleClipboard] A child contacted me for the first time, sending all the data \n");
        for(int i=0;i<REGION_SIZE;i++){
            info = getLocalClipboardInfo(i);
            info.action = 0;
            payload = getLocalClipboardData(i);
            if(sendDataToRemote(remoteClipboard.sock,info,payload)!=0){
                //FIXME
            }
        }
        printf("[HandleClipboard] Child is updated \n");
    }


    //Wait for an update by the remote clipboard
    while(1){
        //Receive information about the new data and verify if it's actually new
        payload = getRemoteData(remoteClipboard.sock, &info, remoteClipboard.isParent,&error,&logout);
        if(logout == 1){
            //Handle clipboard disconnection
            //FIXME
            printf("\t[HandleClipboard] Remote Client is logging out\n" );
            logout=0;
            continue;
        }
        if(error == 1){
            //Handle error
            printf("\t[HandleClipboard] Error receiving information!\n");
            //FIXME
            exit(0);
            error = 0;
            continue;
        }
        if(payload == NULL){
            printf("\t[HandleClipboard] I already had this information \n");
            continue;
        }
        //Update Local Clipboard
        setLocalRegion(info.region,payload,info.msg_size,info.hash);
        printf("[HandleClipboard] Received and updated: [%d] - %s - %s \n",info.region,info.hash ,(char*)payload);


        //***************CRITICAL REGION****************
        pthread_mutex_lock(&mutex[info.region]);
        new_data[info.region]=true;
        from_parent[info.region]=remoteClipboard.isParent;
        pthread_mutex_unlock(&mutex[info.region]);
        //***************CRITICAL REGION****************

        //Send signal to spread new data
        pthread_cond_signal(&cond[info.region]);

        printClipboard();

    }

    pthread_exit(NULL);
}


/**
 * This function waits for changes to a particular region of the local clipboard
 * and spreads the information to other clipboards
 *
 * @param region_
 * @return
 */
void * regionWatch(void * region_){
    int * region__ = region_;
    int region = *region__;
    pthread_mutex_unlock(&setup_mutex);

    struct metaData info;
    int list_size=0;
    struct node * node;
    void * payload = NULL;
    bool parent = 0;
    t_lista * aux = head;
    t_lista * prev = NULL;

    while(1){
        pthread_mutex_lock(&mutex[region]);
        //Wait for clipboard region to receive new update
        pthread_cond_wait(&cond[region],&mutex[region]);
        //Get New Data when unlocked
        //printf("[Region watch - %d] Out of Conditional wait \n", region);
        info = getLocalClipboardInfo(region);
        info.action = 0;
        payload = getLocalClipboardData(region);
        //printf("[Region watch] [%d] - %s - %zd - %s\n",info.region,info.hash,info.msg_size,(char*)payload);
        //Cleanup for condition wait
        new_data[region] = false;
        parent = from_parent[region];   //check if info came from parent
        from_parent[region] = 0;
        pthread_mutex_unlock(&mutex[region]);
        //printf("[Region watch] Cleaned things up \n");

        //Run through clipboards list and spread the new data
        if(parent){
            printf("[Region watch] Message came from parent \n");
            list_size = numItensNaLista(head) -1;
        }else{
            printf("[Region watch] Message came from son/local \n");
            list_size = numItensNaLista(head);
        }
        printList();
        pthread_mutex_lock(&list_mutex);
        aux = head;
        prev = NULL;
        if(list_size != 0){
            printf("[Region watch] Starting to spread the message. List has size %d \n",list_size);
            for(int i=0;i<list_size;i++){
                //printf("[Region watch] list position %d \n",i);
                node = getItemLista(aux);
                if(sendDataToRemote(node->sock,info,payload)!=0){
                    printf("\t[Region watch] !!!!!!!!!!!!!Failed to spread!!!!!!!!!!!!!!!!!!!!!! \n");
                    aux = free_node(&prev, aux,freePayload);
                    continue;
                }
                prev = aux;
                aux = getProxElementoLista(aux);
                printf("[Region watch] Node %d (%d) done [%s] \n",i,node->id,(char*)payload);
            }
        }
        pthread_mutex_unlock(&list_mutex);
        printf("[Region Watch] Message [%s] is spread \n",(char*)payload);
    }
    pthread_exit(NULL);
}


/**
 * This function receives the 10 clipboard regions from the parent clipboard and
 * writes them to the local clipboard.
 *
 * @param parent_id
 * @return
 */
int ClipSync(int parent_id){
    struct metaData info;
    void * received;
    int error, logout;
    printf("[ClipSync] Initiating process\n");
    //Request Data

    for(int i=0;i<REGION_SIZE;i++){
        received = getRemoteData(parent_id,&info,false,&error,&logout);
        if(error == 1){
            //Handle error
            //FIXME
            error = 0;
            continue;
        }
        printf("[ClipSync] Received [%d] - %s \t %s \n",info.region,info.hash ,(char*)received);
        setLocalRegion(i,received,info.msg_size,info.hash);
        printf("[ClipSync] Updated Local Region %d\n",i);
    }
    printf("[ClipSync] Done \n");

    return(0);
}


void printList(){
    int * client;
    t_lista* aux = head;
    printf("====\n");
    while(aux != NULL){
        client = getItemLista(aux);
        printf("%d\t",*client);
        aux = getProxElementoLista(aux);
    }
    printf("\n====\n");
}
