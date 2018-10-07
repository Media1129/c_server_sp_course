#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <sys/socket.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>

#define ERR_EXIT(a) { perror(a); exit(1); }
typedef struct { 
	int id;
	int amount;
	int price; 
} Item;

typedef struct {
    char hostname[512];  // server's hostname
    unsigned short port;  // port to listen
    int listen_fd;  // fd to wait for a new connection
} server;

typedef struct {
    char host[512];  // client's host
    int conn_fd;  // fd to talk with client
    char buf[512];  // data sent by/to client
    size_t buf_len;  // bytes used by buf
    // you don't need to change this.
	int item;
    int wait_for_write;  // used by handle_read to know if the header is read or not.
	int num;
	int times;
} request;

server svr;  // server
request* requestP = NULL;  // point to a list of requests
int maxfd;  // size of open file descriptor table, size of request list

const char* accept_read_header = "ACCEPT_FROM_READ";
const char* accept_write_header = "ACCEPT_FROM_WRITE";

// Forwards

static void init_server(unsigned short port);
// initailize a server, exit for error

static void init_request(request* reqP);
// initailize a request instance

static void free_request(request* reqP);
// free resources used by a request instance

static int handle_read(request* reqP);
// return 0: socket ended, request done.
// return 1: success, message (without header) got this time is in reqP->buf with reqP->buf_len bytes. read more until got <= 0.
// It's guaranteed that the header would be correctly set after the first read.
// error code:
// -1: client connection error

int main(int argc, char** argv) {
    int i, ret;

    struct sockaddr_in cliaddr;  // used by accept()
    int clilen;

    int conn_fd;  // fd for a new connection with client
    int file_fd;  // fd for file that we open for reading
    char buf[512];
    int buf_len;

    // Parse args.
    if (argc != 2) {
        fprintf(stderr, "usage: %s [port]\n", argv[0]);
        exit(1);
    }

    // Initialize server
    init_server((unsigned short) atoi(argv[1]));

    // Get file descripter table size and initize request table
    maxfd = getdtablesize();
    requestP = (request*) malloc(sizeof(request) * maxfd);
    if (requestP == NULL) {
        ERR_EXIT("out of memory allocating all requests");
    }
    for (i = 0; i < maxfd; i++) {
        init_request(&requestP[i]);
    }
    requestP[svr.listen_fd].conn_fd = svr.listen_fd;
    strcpy(requestP[svr.listen_fd].host, svr.hostname);

    // Loop for handling connections
    fprintf(stderr, "\nstarting on %.80s, port %d, fd %d, maxconn %d...\n", svr.hostname, svr.port, svr.listen_fd, maxfd);
    while (1) {
        // TODO: Add IO multiplexing
		fd_set readfds;
		int maxfd_select=0;
		int record_write[20];
		for(int i = 0;i<20;i++)
			record_write[i] = 0;
        // Check new connection
		while(1){
			//select 
			FD_ZERO(&readfds);
			maxfd_select = 3;
			FD_SET(3,&readfds);
			for(int i = 4;i < maxfd;i++){
				if(requestP[i].conn_fd != -1){
					FD_SET(requestP[i].conn_fd,&readfds);
					if(requestP[i].conn_fd > maxfd_select)
						maxfd_select = requestP[i].conn_fd;	
				}
			}
			struct timeval tv;
			tv.tv_sec = 2;
           	tv.tv_usec = 0;
			if ((ret = select(maxfd_select+1,&readfds,NULL,NULL,&tv)) == -1) {
        		fprintf(stderr,"Err in select");
    		}
			fprintf(stderr,"maxfd_select = %d\n",maxfd_select);
			for(int i = 3; i <= maxfd_select; i++) {
				if( FD_ISSET(i,&readfds) && i == 3){
 		       			clilen = sizeof(cliaddr);
        				conn_fd = accept(svr.listen_fd, (struct sockaddr*)&cliaddr, (socklen_t*)&clilen);
        				if (conn_fd < 0) {
            				if (errno == EINTR || errno == EAGAIN) continue;  // try again
            				if (errno == ENFILE) {
                				(void) fprintf(stderr, "out of file descriptor table ... (maxconn %d)\n", maxfd);
                				continue;
            				}
            				ERR_EXIT("accept")
        				}
        				requestP[conn_fd].conn_fd = conn_fd;
					//特別注意有沒有問題
						requestP[conn_fd].times = -1;
				
        				strcpy(requestP[conn_fd].host, inet_ntoa(cliaddr.sin_addr));
        				fprintf(stderr, "getting a new request... fd %d from %s\n", conn_fd, requestP[conn_fd].host);	

				}
				else if(FD_ISSET(i, &readfds) ) {	
					ret = handle_read(&requestP[i]); // parse data from client to requestP[conn_fd].buf
					if (ret < 0) {
						fprintf(stderr, "bad request from %s\n", requestP[i].host);
						continue;
					}
					#ifdef READ_SERVER
							//sprintf(buf,"%s : %s\n",accept_read_header,requestP[i].buf);
							//write(requestP[i].conn_fd, buf, strlen(buf));
							
							Item read_item;
							int read_fd = open("item_list",O_RDONLY);
    						size_t nbytes;
    						nbytes = sizeof(Item);
							int num = atoi(requestP[i].buf)-1;
							lseek(read_fd,num*nbytes,SEEK_SET);
							read(read_fd,&read_item,nbytes);
							
							struct flock lock;
							lock.l_type = F_RDLCK;
							lock.l_start = num*nbytes;;
							lock.l_whence = SEEK_SET;
							lock.l_len = nbytes;							
							int test = fcntl(read_fd,F_SETLK,&lock);
							fprintf(stderr,"test = %d\n",test);
							if(test == -1){
								char buf_h[] = "This item is locked.\n";
								write(requestP[i].conn_fd, buf_h, strlen(buf_h));				
							}
							else{
								//sleep(10);
								char buf_2[30];
								sprintf(buf_2,"item%d $%d remain: %d\n",read_item.id,read_item.price,read_item.amount);
								write(requestP[i].conn_fd, buf_2, strlen(buf_2));
								lock.l_type = F_UNLCK;
								fcntl(read_fd,F_SETLK,&lock);
							}
							//注意如果關掉file descriptor 所有的lock都會消失
							//close(read_fd);
						
							close(requestP[i].conn_fd);
							free_request(&requestP[i]);
					#else
						//印出統一在上面第一次讀到的資料
						//sprintf(buf,"%s : %s\n",accept_write_header,requestP[i].buf);
						//write(requestP[i].conn_fd, buf, strlen(buf));
						if(requestP[i].times == -1){
								int num = atoi(requestP[i].buf)-1;		
								requestP[i].num = num;
								requestP[i].times = 1;    					
								size_t nbytes;
								nbytes = sizeof(Item);
								
								struct flock lock;
								lock.l_type = F_WRLCK;
								lock.l_start = num*nbytes;;
								lock.l_whence = SEEK_SET;
								lock.l_len = nbytes;		
								
								//get the item_list
								int write_fd = open("item_list",O_RDWR);
								lseek(write_fd,num*nbytes,SEEK_SET);
											
								int test = fcntl(write_fd,F_SETLK,&lock);	
								fprintf(stderr,"test = %d--------\n",test);
								if(test == -1){
									char buff_t[] = "This item is locked.\n";
									write(requestP[i].conn_fd, buff_t, strlen(buff_t));
									close(requestP[i].conn_fd);
									free_request(&requestP[i]);
								}
								else{
									fprintf(stderr,"record[%d] = %d\n",num,record_write[num]);
									if(record_write[num] == 0){
											char buff_f[] = "This item is modifitable.\n";
											write(requestP[i].conn_fd, buff_f, strlen(buff_f));
											record_write[num] = 1;
											
									}
									else{
											char buff_t[] = "This item is locked.\n";
											write(requestP[i].conn_fd, buff_t, strlen(buff_t));
											close(requestP[i].conn_fd);
											free_request(&requestP[i]);	
									}
								}
								//如果close掉就不會繼續lock住file了
								//close(write_fd);  
						}
						else{
								//上面的num已經減過1了
								int num_2 = requestP[i].num;
								int write_fd = open("item_list",O_RDWR);
								size_t nbytes;
								nbytes = sizeof(Item);
								Item write_item,read_data;
								lseek(write_fd,num_2*nbytes,SEEK_SET);
								read(write_fd,&read_data,nbytes);
								//get the op and num
								char* pch;
								char op[10];
								pch = strtok(requestP[i].buf," ");
								strcpy(op,pch);
								char *pch_2;
								pch_2 = strtok(NULL," ");
								int change_num = atoi(pch_2);
								//change item_list
								if( strcmp(op,"buy") == 0 ){
									if( read_data.amount >= change_num){
										int write_num = read_data.amount - change_num;
										write_item.amount = write_num;
										write_item.price = read_data.price;
										write_item.id = read_data.id;
										lseek(write_fd,num_2*nbytes,SEEK_SET);
										write(write_fd,&write_item,nbytes);	
									}
									else{
										char buff_de[] = "Operation failed.\n";
										write(requestP[i].conn_fd, buff_de, strlen(buff_de));
									

									}
								}
								else if( strcmp(op,"sell") ==0 ){
									write_item.amount = read_data.amount + change_num;
									write_item.price = read_data.price;
									write_item.id = read_data.id;
									lseek(write_fd,num_2*nbytes,SEEK_SET);
									write(write_fd,&write_item,nbytes);
								}
								else if( strcmp(op,"price") == 0){
									if( change_num >= 0){
										write_item.amount = read_data.amount;
										write_item.price = change_num;
										write_item.id = read_data.id;
										lseek(write_fd,num_2*nbytes,SEEK_SET);
										write(write_fd,&write_item,nbytes);
									}
									else{	
										char buff_det[] = "Operation failed.\n";
										write(requestP[i].conn_fd, buff_det, strlen(buff_det));
									}
							
								}	
/////////////////////////////////////////////////////////////////////////////
								record_write[num_2] = 0;
								//close(write_fd);
								
								struct flock lock;
								lock.l_type = F_UNLCK;
								lock.l_start = num_2*nbytes;;
								lock.l_whence = SEEK_SET;
								lock.l_len = nbytes;
								fcntl(write_fd,F_SETLK,&lock);	
										
								close(requestP[i].conn_fd);
								free_request(&requestP[i]);	
							}
						
					#endif
							//close(requestP[i].conn_fd);
							//free_request(&requestP[i]);
							
				}
			}
		}
	}
    free(requestP);
    return 0;
}


// ======================================================================================================
// You don't need to know how the following codes are working
#include <fcntl.h>

static void* e_malloc(size_t size);


static void init_request(request* reqP) {
    reqP->conn_fd = -1;
    reqP->buf_len = 0;
    reqP->item = 0;
    reqP->wait_for_write = 0;
}

static void free_request(request* reqP) {
    /*if (reqP->filename != NULL) {
        free(reqP->filename);
        reqP->filename = NULL;
    }*/
    init_request(reqP);
}

// return 0: socket ended, request done.
// return 1: success, message (without header) got this time is in reqP->buf with reqP->buf_len bytes. read more until got <= 0.
// It's guaranteed that the header would be correctly set after the first read.
// error code:
// -1: client connection error
static int handle_read(request* reqP) {
    int r;
    char buf[512];

    // Read in request from client
    r = read(reqP->conn_fd, buf, sizeof(buf));
    if (r < 0) return -1;
    if (r == 0) return 0;
	char* p1 = strstr(buf, "\015\012");
	int newline_len = 2;
	// be careful that in Windows, line ends with \015\012
	if (p1 == NULL) {
		p1 = strstr(buf, "\012");
		newline_len = 1;
		if (p1 == NULL) {
			ERR_EXIT("this really should not happen...");
		}
	}
	size_t len = p1 - buf + 1;
	memmove(reqP->buf, buf, len);
	reqP->buf[len - 1] = '\0';
	reqP->buf_len = len-1;
    return 1;
}

static void init_server(unsigned short port) {
    struct sockaddr_in servaddr;
    int tmp;

    gethostname(svr.hostname, sizeof(svr.hostname));
    svr.port = port;

    svr.listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (svr.listen_fd < 0) ERR_EXIT("socket");

    bzero(&servaddr, sizeof(servaddr));
    servaddr.sin_family = AF_INET;
    servaddr.sin_addr.s_addr = htonl(INADDR_ANY);
    servaddr.sin_port = htons(port);
    tmp = 1;
    if (setsockopt(svr.listen_fd, SOL_SOCKET, SO_REUSEADDR, (void*)&tmp, sizeof(tmp)) < 0) {
        ERR_EXIT("setsockopt");
    }
    if (bind(svr.listen_fd, (struct sockaddr*)&servaddr, sizeof(servaddr)) < 0) {
        ERR_EXIT("bind");
    }
    if (listen(svr.listen_fd, 1024) < 0) {
        ERR_EXIT("listen");
    }
}

static void* e_malloc(size_t size) {
    void* ptr;

    ptr = malloc(size);
    if (ptr == NULL) ERR_EXIT("out of memory");
    return ptr;
}

