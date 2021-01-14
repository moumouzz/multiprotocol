#include <stdio.h>  
#include <string.h>  
#include <stdlib.h>  
#include <signal.h>  
#include <fcntl.h>  
#include <unistd.h>
#include <sys/socket.h>  
#include <termios.h> //set baud rate  
#include <netinet/in.h>
#include <sys/select.h>  
#include <sys/time.h>  
#include <sys/types.h>  
#include <errno.h>  
#include <sys/stat.h>
#include <arpa/inet.h>
#include <sys/ioctl.h>
#include <pthread.h>
#include <modbus/modbus.h>
#include "modbus_data.h"
#include "open62541.h"  
  
//#define rec_buf_wait_2s 2  
#define buffLen 1024  
#define rcvTimeOut 2  
#define MODBUS_SERVER_PORT   502
#define NB_CONNECTION        5
#define MAXDATASIZE 100
#define PORT 22468


int i=0,j=0;
//the last two bit is CRC(HART\FF\DP)          

//two part as a group		REAL(DP\HART\FF)
//unsigned char send_buff1[512] = {0x01,0x03,0x00,0x00,0x00,0x06,0xc8,0xc5};
//unsigned char send_buff2[512] = {0x01,0x03,0x00,0x06,0x00,0x06,0x25,0xc9};
//unsigned char send_buff3[512] = {0x01,0x03,0x00,0x08,0x00,0x04,0xc5,0xcb};


//single send
//FF
unsigned char send_buff1[16] = {0x01,0x03,0x00,0x00,0x00,0x02,0xc4,0x0b};
#if 0
unsigned char send_buff2[16] = {0x01,0x03,0x00,0x02,0x00,0x02,0x65,0xcb};
unsigned char send_buff3[16] = {0x01,0x03,0x00,0x04,0x00,0x02,0x85,0xca};
//HART
unsigned char send_buff4[16] = {0x01,0x03,0x00,0x06,0x00,0x02,0x24,0x0a};
unsigned char send_buff5[16] = {0x01,0x03,0x00,0x08,0x00,0x02,0x45,0xc9};
unsigned char send_buff6[16] = {0x01,0x03,0x00,0x0a,0x00,0x02,0xe4,0x09};
//DP
unsigned char send_buff7[16] = {0x01,0x03,0x00,0x0c,0x00,0x02,0x04,0x08};
unsigned char send_buff8[16] = {0x01,0x03,0x00,0x0e,0x00,0x02,0xa5,0xc8};
unsigned char send_buff9[16] = {0x01,0x03,0x00,0x10,0x00,0x02,0xc5,0xce};
unsigned char send_buff10[16] = {0x01,0x03,0x00,0x12,0x00,0x02,0x64,0x0e};
//unsigned char send_buff11[16] = {0x01,0x03,0x00,0x14,0x00,0x02,0x84,0x10};

/*TEST**///unsigned char send_buff12[16] = {0x01,0x03,0x00,0x0c,0x00,0x08,0x84,0x0f};


//total send
unsigned char send_buff[16] = {0x01,0x03,0x00,0x00,0x00,0x14,0x45,0xc5};
#endif


unsigned char recv_buff[64]; 

#define OPCUA_SERVER_PORT 16664
UA_Server *server;

typedef struct _DATA_SOURCE{
    char* name;
    float data;
}DATA_SOURCE;

//send_buff start address
DATA_SOURCE source[]=
{
    {"FF_1",0.0},//00,01
    {"FF_2",0.0},//02,03
    {"FF_3",0.0},//04,05
    {"HART_1",0.0},//06,07
    {"HART_2",0.0},//08,09
    {"HART_3",0.0},//0A,0B
    {"DP_1",0.0},//0C,0D
    {"DP_2",0.0},//0E,0F
    {"DP_3",0.0},//10,11
    {"DP_4",0.0}
};


//CRC校验函数
unsigned short ModBusCRC16 (unsigned char *ptr,unsigned char size)
{
    unsigned short a,b,tmp,CRC16,V;
    CRC16=0xffff;                 //CRC寄存器初始值
    for (a=0;a<size;a++)            //N个字节
    {
        CRC16=*ptr^CRC16;
        for (b=0;b<8;b++)            //8位数据
        {
            tmp=CRC16 & 0x0001;
            CRC16 =CRC16 >>1;       //右移一位
        if (tmp)
        CRC16=CRC16 ^ 0xa001;    //异或多项式
        }
        *ptr++;
    }
    V = ((CRC16 & 0x00FF) << 8) | ((CRC16 & 0xFF00) >> 8); //高低字节转换
    return V;
}
#if 0
//explain the meaning of send_buff
unsigned char Send(unsigned char REGISTERS_ADDRESS,unsigned char REGISTERS_NUM)
{
	unsigned char send_buff[20];
	memset(send_buff,'\0',sizeof(send_buff));
	send_buff[0] = 0x01;
	send_buff[1] = 0x03;
	send_buff[2] = 0x00;
	send_buff[3] = REGISTERS_ADDRESS;
	send_buff[4] = 0x00;
	send_buff[5] = REGISTERS_NUM;
	unsigned short CRC = ModBusCRC16(send_buff,6);
	send_buff[6] = (unsigned char)(CRC >> 8 & 0x00FF);
	send_buff[7] = (unsigned char)(CRC & 0x00FF);
	printf("Send cmd is ");
	for(int i=0;i<8;i++)
		printf("0x%x ",send_buff[i]);
	printf("\n");
	return send_buff;
}

# endif 

int server_socket = -1;
void close_sigint(int dummy)
{
    if (server_socket != -1) {
        close(server_socket);
    }
    modbus_free(ctx);
    modbus_mapping_free(mb_mapping);

    exit(dummy);
}

void *Modbus_Server(void *arg)
{
    uint8_t query[MODBUS_TCP_MAX_ADU_LENGTH];
    int buf[buffLen];
    int master_socket;
    int rc;
    fd_set refset;
    fd_set rdset;
    /* Maximum file descriptor number */
    int *a=0x130;
    int fdmax;

    ctx = modbus_new_tcp(INADDR_ANY, MODBUS_SERVER_PORT);

    mb_mapping = modbus_mapping_new_start_address(
            UT_BITS_ADDRESS, UT_BITS_NB,
            UT_INPUT_BITS_ADDRESS, UT_INPUT_BITS_NB,
            UT_REGISTERS_ADDRESS, UT_REGISTERS_NB_MAX,
            UT_INPUT_REGISTERS_ADDRESS, UT_INPUT_REGISTERS_NB
    );
    if (mb_mapping == NULL) {
        fprintf(stderr, "Failed to allocate the mapping: %s\n",
                modbus_strerror(errno));
        modbus_free(ctx);
        exit(EXIT_FAILURE);
    }

    /* Initialize values of INPUT REGISTERS */
    for (int i=0; i < UT_INPUT_REGISTERS_NB; i++) {
        mb_mapping->tab_input_registers[i] = UT_INPUT_REGISTERS_TAB[i];
    }

    server_socket = modbus_tcp_listen(ctx, NB_CONNECTION);
    if (server_socket == -1) {
        fprintf(stderr, "Unable to listen TCP connection\n");
        modbus_free(ctx);
        exit(EXIT_FAILURE);
    }

    signal(SIGINT, close_sigint);

    /* Clear the reference set of socket */
    FD_ZERO(&refset);
    /* Add the server socket */
    FD_SET(server_socket, &refset);

    /* Keep track of the max file descriptor */
    fdmax = server_socket;

    for (;;) 
    {
        rdset = refset;
        if (select(fdmax+1, &rdset, NULL, NULL, NULL) == -1)
        {
            perror("Server select() failure.");
            close_sigint(1);
        }

        /* Run through the existing connections looking for data to be
         * read */
        for (master_socket = 0; master_socket <= fdmax; master_socket++) 
        {

            if (!FD_ISSET(master_socket, &rdset)) 
            {
                continue;
            }

            if (master_socket == server_socket) 
            {
                /* A client is asking a new connection */
                socklen_t addrlen;
                struct sockaddr_in clientaddr;
                int newfd;

                /* Handle new connections */
                addrlen = sizeof(clientaddr);
                memset(&clientaddr, 0, sizeof(clientaddr));
                newfd = accept(server_socket, (struct sockaddr *)&clientaddr, &addrlen);
                if (newfd == -1) 
                {
                    perror("Server accept() error");
                } else 
                {
                    FD_SET(newfd, &refset);

                    if (newfd > fdmax) 
                    {
                        /* Keep track of the maximum */
                        fdmax = newfd;
                    }
                    printf("New connection from %s:%d on socket %d\n",
                           inet_ntoa(clientaddr.sin_addr), clientaddr.sin_port, newfd);
                }
            } else 
            {
                modbus_set_socket(ctx, master_socket);
                rc = modbus_receive(ctx, query);
                if (rc > 0) 
                {
                    modbus_reply(ctx, query, rc, mb_mapping);
                } else if (rc == -1) 
                {
                    /* This example server in ended on connection closing or
                     * any errors. */
                    printf("Connection closed on socket %d\n", master_socket);
                    close(master_socket);

                    /* Remove from reference set */
                    FD_CLR(master_socket, &refset);

                    if (master_socket == fdmax)
                    {
                        fdmax--;
                    }
                }
            }
        }
    }
}

UA_Boolean running = true;
static void stopHandler(int sig) {
    running = false;
}

/*通过名字读数据*/
void* nodeIdFindData(const UA_NodeId nodeId)
{
    int i;
    for(i=0;i<sizeof(source)/sizeof(DATA_SOURCE);i++) {
        if(strncmp((char*)nodeId.identifier.string.data, source[i].name, strlen(source[i].name)) == 0) 
            return &source[i].data;
        }           
    printf("not find:%s!\n",nodeId.identifier.string.data);
    return NULL;
}

static UA_StatusCode
readFloatDataSource(UA_Server *server, const UA_NodeId *sessionId,
                          void *sessionContext, const UA_NodeId *nodeId,
                          void *nodeContext, UA_Boolean sourceTimeStamp,
                          const UA_NumericRange *range, UA_DataValue *value) {
    if(range) {
        value->hasStatus = true;
        value->status = UA_STATUSCODE_BADINDEXRANGEINVALID;
        return UA_STATUSCODE_GOOD;
    }       
    UA_Float currentFloat;

    if(nodeIdFindData(*nodeId) != NULL)
        currentFloat = *(UA_Float*)nodeIdFindData(*nodeId);
    else 
        currentFloat = -1;
    value->sourceTimestamp = UA_DateTime_now();
    value->hasSourceTimestamp = true;
    UA_Variant_setScalarCopy(&value->value, &currentFloat, &UA_TYPES[UA_TYPES_FLOAT]);
    value->hasValue = true;
    return UA_STATUSCODE_GOOD;
}

void add_dataSource_to_opcServer()
{
    int i;
    for(i=0;i<sizeof(source)/sizeof(DATA_SOURCE);i++) {
            UA_DataSource dateDataSource = (UA_DataSource) { .read = readFloatDataSource, .write = NULL};
            UA_VariableAttributes *attr_float = UA_VariableAttributes_new();
            UA_VariableAttributes_init(attr_float);
            
            attr_float->description = UA_LOCALIZEDTEXT("en_US",source[i].name);
            attr_float->displayName = UA_LOCALIZEDTEXT("en_US",source[i].name);
            attr_float->accessLevel = UA_ACCESSLEVELMASK_READ | UA_ACCESSLEVELMASK_WRITE;
            UA_NodeId myIntegerNodeId = UA_NODEID_STRING(1, source[i].name);
            UA_QualifiedName myIntegerName = UA_QUALIFIEDNAME(1, source[i].name);
            UA_NodeId parentNodeId = UA_NODEID_STRING(1, "Mutiprotocol");
            UA_NodeId parentReferenceNodeId = UA_NODEID_NUMERIC(0, UA_NS0ID_ORGANIZES);
            UA_Server_addDataSourceVariableNode(server, myIntegerNodeId,parentNodeId,
                                                    parentReferenceNodeId, myIntegerName,
                                                UA_NODEID_NULL, *attr_float, dateDataSource, NULL,NULL);     
    }   
}

void handle_opcua_server(){


    signal(SIGINT, stopHandler);
	signal(SIGTERM, stopHandler);
	/* init the server */
	UA_ServerConfig *config = UA_ServerConfig_new_minimal(15000, NULL);
//	UA_ServerNetworkLayer nl = UA_ServerNetworkLayerTCP(UA_ConnectionConfig_default, 15000,);
//	config.networkLayers = &nl;
//	config.networkLayersSize = 1;
	server = UA_Server_new(config);
	 
	UA_ObjectAttributes object_attr;
    UA_ObjectAttributes_init(&object_attr);
    object_attr.description = UA_LOCALIZEDTEXT("en_US", "Mutiprotocol");
    object_attr.displayName = UA_LOCALIZEDTEXT("en_US", "Mutiprotocol");
    UA_Server_addObjectNode(server, UA_NODEID_STRING(1, "Mutiprotocol"),
        UA_NODEID_NUMERIC(0, UA_NS0ID_OBJECTSFOLDER),
        UA_NODEID_NUMERIC(0, UA_NS0ID_ORGANIZES), UA_QUALIFIEDNAME(1,"Mutiprotocol"),
        UA_NODEID_NUMERIC(0, UA_NS0ID_FOLDERTYPE), object_attr, NULL, NULL);  



	/* run the server loop */
	UA_StatusCode retval = UA_Server_run(server, &running);
	UA_Server_delete(server);
//	nl.deleteMembers(&nl);
	UA_ServerConfig_delete(config);

}


/*************Linux and Serial Port *********************/  
int openPort(int fd, int comport)  
{  
  
    if (comport == 1)  
    {  
        fd = open("/dev/ttyS0", O_RDWR | O_NOCTTY | O_NDELAY);  
        if (-1 == fd)  
        {  
            perror("Can't Open Serial Port");  
            return(-1);  
        }  
        else  
        {  
            printf("open ttyS0 .....\n");  
        }  
    }  
    else if (comport == 2)  
    {  
        fd = open("/dev/ttyS1", O_RDWR | O_NOCTTY | O_NDELAY);  
        if (-1 == fd)  
        {  
            perror("Can't Open Serial Port");  
            return(-1);  
        }  
        else  
        {  
            printf("open ttyS1 .....\n");  
        }  
    }  
    else if (comport == 3)  
    {  
        fd = open("/dev/ttyS2", O_RDWR | O_NOCTTY | O_NDELAY);  
        if (-1 == fd)  
        {  
            perror("Can't Open Serial Port");  
            return(-1);  
        }  
        else  
        {  
            printf("open ttyS2 .....\n");  
        }  
    }  
    /*************************************************/  
    else if (comport == 4)  
    {  
        fd = open("/dev/ttyUSB0", O_RDWR | O_NOCTTY | O_NDELAY);  
        if (-1 == fd)  
        {  
            perror("Can't Open Serial Port");  
            return(-1);  
        }  
        else  
        {  
            printf("open ttyUSB0 .....\n");  
        }  
    }  
  
    if (fcntl(fd, F_SETFL, 0)<0)  
    {  
        printf("fcntl failed!\n");  
    }  
    else  
    {  
        printf("fcntl=%d\n", fcntl(fd, F_SETFL, 0));  
    }  
    if (isatty(STDIN_FILENO) == 0)  
    {  
        printf("standard input is not a terminal device\n");  
    }  
    else  
    {  
        printf("is a tty success!\n");  
    }  
    printf("fd-open=%d\n", fd);  
    return fd;  
}  
  
int setOpt(int fd, int nSpeed, int nBits, char nEvent, int nStop)  
{  
    struct termios newtio, oldtio;  
    if (tcgetattr(fd, &oldtio) != 0)  
    {  
        perror("SetupSerial 1");  
        return -1;  
    }  
    bzero(&newtio, sizeof(newtio));  
    newtio.c_cflag |= CLOCAL | CREAD;  
    newtio.c_cflag &= ~CSIZE;  
  
    switch (nBits)  
    {  
    case 7:  
        newtio.c_cflag |= CS7;  
        break;  
    case 8:  
        newtio.c_cflag |= CS8;  
        break;  
    }  
  
    switch (nEvent)  
    {  
    case 'O':                     //奇校验  
        newtio.c_cflag |= PARENB;  
        newtio.c_cflag |= PARODD;  
        newtio.c_iflag |= (INPCK | ISTRIP);  
        break;  
    case 'E':                     //偶校验  
        newtio.c_iflag |= (INPCK | ISTRIP);  
        newtio.c_cflag |= PARENB;  
        newtio.c_cflag &= ~PARODD;  
        break;  
    case 'N':                    //无校验  
        newtio.c_cflag &= ~PARENB;  
        break;  
    }  
  
    switch (nSpeed)  
    {  
    case 2400:  
        cfsetispeed(&newtio, B2400);  
        cfsetospeed(&newtio, B2400);  
        break;  
    case 4800:  
        cfsetispeed(&newtio, B4800);  
        cfsetospeed(&newtio, B4800);  
        break;  
    case 9600:  
        cfsetispeed(&newtio, B9600);  
        cfsetospeed(&newtio, B9600);  
        break;  
    case 115200:  
        cfsetispeed(&newtio, B115200);  
        cfsetospeed(&newtio, B115200);  
        break;  
    default:  
        cfsetispeed(&newtio, B9600);  
        cfsetospeed(&newtio, B9600);  
        break;  
    }  
    if (nStop == 1)  
    {  
        newtio.c_cflag &= ~CSTOPB;  
    }  
    else if (nStop == 2)  
    {  
        newtio.c_cflag |= CSTOPB;  
    }  
    newtio.c_cc[VTIME] = 0;  
    newtio.c_cc[VMIN] = 0;  
    tcflush(fd, TCIFLUSH);  
    if ((tcsetattr(fd, TCSANOW, &newtio)) != 0)  
    {  
        perror("com set error");  
        return -1;  
    }  
    printf("set done!\n");  
    return 0;  
}  

int UART_Recv(int fd, char *rcv_buf,int data_len)
{
    int len,fs_sel;
    fd_set fs_read;
    
    struct timeval time;
    
    FD_ZERO(&fs_read);
    FD_SET(fd,&fs_read);
    
    time.tv_sec = 10;
    time.tv_usec = 0;
    
    //使用select实现串口的多路通信
    fs_sel = select(fd+1,&fs_read,NULL,NULL,&time);
    if(fs_sel)
    {
     len = read(fd,rcv_buf,data_len);    
     return len;
    } 
    else
    {
        return -1;
    }    
}

int sendDataTty(int fd, unsigned char *send_buf, int Len)  
{  
    ssize_t ret;  
  
    ret = write(fd, send_buf, Len);  
    if (ret == -1)  
    {  
        printf("write device error\n");  
        return -1;  
    }  
  
    return 1;  
} 

float Readini()
{
    FILE *fp;
    char str[128];
    float a;
    if ((fp = fopen("./configini", "r")) == NULL) {
        printf("cannot open file/n");
    }
    fscanf(fp,"%s %f;", str,&a);
    fclose(fp);//feof监测文件结束符，如果未结束返回非0值，结束返回0值；
    return a;
}

void char2int(int *num, char *str)
{
    int len = strlen(str);
    *num = 0;
    for (int i = 0; i < len; i++) {
        *num = (*num) * 10 + str[i]-'0';
    }
}

int send_socket(void *arg)
{
    int *client_sockfd,len=0;
    client_sockfd=(int *)arg;
    char msg[MAXDATASIZE];

    while(1)
    {
        memset(msg,0,MAXDATASIZE);
        scanf("%s",msg);
        len = strlen(msg);
        
        if(send(*client_sockfd, msg,len,0) == -1){ 
            perror("send error"); 
        }
      

    }
    return 0;
}


int socket_server()
{
    pthread_t thread2;
    int ret_thread2;
    char buf[MAXDATASIZE];
    int len;
    int socketbuf;
    memset(buf,0,MAXDATASIZE);

    int server_sockfd,client_sockfd,numbytes;
    socklen_t server_len,client_len;

    struct  sockaddr_in server_sockaddr,client_sockaddr;

    /*create a socket.type is AF_INET,sock_stream*/
    server_sockfd = socket(AF_INET,SOCK_STREAM,0);
    
    server_sockaddr.sin_family = AF_INET;
    server_sockaddr.sin_port = htons(PORT);
    server_sockaddr.sin_addr.s_addr = htonl(INADDR_ANY);

    server_len = sizeof(server_sockaddr);

    printf("\n======================server initialization======================\n");
    
    int on;
    setsockopt(server_sockfd, SOL_SOCKET, SO_REUSEADDR,&on,sizeof(on));
    /*bind a socket or rename a sockt*/
    if(bind(server_sockfd, (struct sockaddr*)&server_sockaddr, server_len)==-1){
        printf("bind error");
        exit(1);
    }

    if(listen(server_sockfd, 5)==-1){
        printf("listen error");
        exit(1);
    }

    client_len = sizeof(client_sockaddr);

    pid_t ppid,pid;

    while(1) {

        if((client_sockfd = accept(server_sockfd, (struct sockaddr*)&client_sockaddr, &client_len)) == -1){
            printf("connect error");
            exit(1);
        } else {
            printf("create connection successfully\n");
            int error = send(client_sockfd, "You have conected the server", strlen("You have conected the server"), 0);
            printf("%d\n", error);
            break;
        }
    }

    ret_thread2=pthread_create(&thread2,NULL,send_socket,&(client_sockfd));
    if(ret_thread2==0)
        printf("\nsend_process do\n");

    while(1){
        bzero(buf,MAXDATASIZE); 
 //       printf("\nBegin receive...\n");
        if ((numbytes = recv(client_sockfd, buf, MAXDATASIZE, 0)) == -1){  
            perror("recv"); 
            exit(1);
        } else if (numbytes > 0) { 
            int len, bytes_sent;
            buf[numbytes] = '\0'; 
            printf("Received: %s\n",buf);
            char2int(&socketbuf,buf);

            UT_REGISTERS_TAB[0]=socketbuf;           //写寄存器？
            printf("register is %d\n",UT_REGISTERS_TAB[0]);

            } else { 
                printf("soket end!\n"); 
                break;
            } 
    }

    pthread_join(thread2,NULL);

        
 
    return 0;
}



int main()
{ 
	pthread_t modbus_server_thread; 
	pthread_t opcua_server_id;
    pthread_t server_socket_id;                //开启socket线程

	pthread_create(&modbus_server_thread, NULL, Modbus_Server, NULL);
	pthread_create(&opcua_server_id,NULL,(void *)handle_opcua_server,NULL);
    pthread_create(&server_socket_id,NULL,socket_server,NULL);          //开启socket线程

    int iSetOpt = 0;//SetOpt 的增量i  
    int fdSerial = 0;  
  
    //openPort  
    if ((fdSerial = openPort(fdSerial, 1))<0)//1--"/dev/ttyS0",2--"/dev/ttyS1",3--"/dev/ttyS2",4--"/dev/ttyUSB0" 小电脑上是2--"/dev/ttyS1"  
    {  
        perror("open_port error");  
        return -1;  
    }  

    //setOpt(fdSerial, 9600, 8, 'N', 1)  
    if ((iSetOpt = setOpt(fdSerial, 115200, 8, 'N', 1))<0)  
    {  
        perror("set_opt error");  
        return -1;  
    }  
    printf("Serial fdSerial = %d\n", fdSerial);  
  
    tcflush(fdSerial, TCIOFLUSH);//清掉串口缓存  
    fcntl(fdSerial, F_SETFL, 0);  
    memset(recv_buff,'\0',sizeof(recv_buff));
    int ret=0;
 
    while(1)
    {
        memset(recv_buff,'\0',sizeof(recv_buff));

        sleep(1);
#if 0 	//single send
    	//read FF1's data,modbus(0,1)
        sendDataTty(fdSerial,send_buff1, 8);
        printf("send_buff is %x,%x,%x,%x,%x,%x,%x,%x\n",send_buff1[0],send_buff1[1],send_buff1[2],send_buff1[3],send_buff1[4],send_buff1[5],send_buff1[6],send_buff1[7]); 
        sleep(1);
        ret=UART_Recv(fdSerial, recv_buff,64);  
        printf("recv_buff is %x,%x,%x,%x,%x,%x,%x,%x,%x\n",recv_buff[0],recv_buff[1],recv_buff[2],recv_buff[3],recv_buff[4],recv_buff[5],recv_buff[6],recv_buff[7],recv_buff[8]); 
        if(ret == -1)
            printf("Recv FF1 is incorrect!\n");
        unsigned char ff1[]={recv_buff[4],recv_buff[3],recv_buff[6],recv_buff[5]};
        UT_INPUT_REGISTERS_TAB[0] = (uint16_t)((recv_buff[3]<<8)+recv_buff[4]);
		UT_INPUT_REGISTERS_TAB[1] = (uint16_t)((recv_buff[5]<<8)+recv_buff[6]);
		float *Field1 = (float*)ff1;
		source[0].data = *Field1;
		printf("FF_1 = %f \n",*Field1);
		memset(recv_buff,'\0',sizeof(recv_buff));
        tcflush(fdSerial, TCIOFLUSH);//清掉串口缓存  
        printf("\n");
        sleep(1);
#endif

#if 0
        //read FF2's data,modbus(2,3)
        sendDataTty(fdSerial,send_buff2, 8);
        printf("send_buff is %x,%x,%x,%x,%x,%x,%x,%x\n",send_buff2[0],send_buff2[1],send_buff2[2],send_buff2[3],send_buff2[4],send_buff2[5],send_buff2[6],send_buff2[7]); 
        sleep(1);
        ret=UART_Recv(fdSerial, recv_buff,64);  
        printf("recv_buff is %x,%x,%x,%x,%x,%x,%x,%x,%x\n",recv_buff[0],recv_buff[1],recv_buff[2],recv_buff[3],recv_buff[4],recv_buff[5],recv_buff[6],recv_buff[7],recv_buff[8]); 
        if(ret == -1)
            printf("Recv FF2 is incorrect!\n");
        unsigned char ff2[]={recv_buff[4],recv_buff[3],recv_buff[6],recv_buff[5]};
        UT_INPUT_REGISTERS_TAB[2] = (uint16_t)((recv_buff[3]<<8)+recv_buff[4]);
		UT_INPUT_REGISTERS_TAB[3] = (uint16_t)((recv_buff[5]<<8)+recv_buff[6]);
		float *Field2 = (float*)ff2;
		source[1].data = *Field2;
		printf("FF_2 = %f \n",*Field2);
		memset(recv_buff,'\0',sizeof(recv_buff));
        tcflush(fdSerial, TCIOFLUSH);//清掉串口缓存
        printf("\n");
        sleep(1);

        //read FF3's data,modbus(4,5)
        sendDataTty(fdSerial,send_buff3, 8);
        printf("send_buff is %x,%x,%x,%x,%x,%x,%x,%x\n",send_buff3[0],send_buff3[1],send_buff3[2],send_buff3[3],send_buff3[4],send_buff3[5],send_buff3[6],send_buff3[7]); 
        sleep(1);
        ret=UART_Recv(fdSerial, recv_buff,64);  
        printf("recv_buff is %x,%x,%x,%x,%x,%x,%x,%x,%x\n",recv_buff[0],recv_buff[1],recv_buff[2],recv_buff[3],recv_buff[4],recv_buff[5],recv_buff[6],recv_buff[7],recv_buff[8]); 
        if(ret == -1)
            printf("Recv FF3 is incorrect!\n");
        unsigned char ff3[]={recv_buff[4],recv_buff[3],recv_buff[6],recv_buff[5]};
        UT_INPUT_REGISTERS_TAB[4] = (uint16_t)((recv_buff[3]<<8)+recv_buff[4]);
        UT_INPUT_REGISTERS_TAB[5] = (uint16_t)((recv_buff[5]<<8)+recv_buff[6]);
        float *Field3 = (float*)ff3;
        source[2].data = *Field3;
        printf("FF_3 = %f \n",*Field3);
        memset(recv_buff,'\0',sizeof(recv_buff));
        tcflush(fdSerial, TCIOFLUSH);//清掉串口缓存
        printf("\n");
        sleep(1);


    	//read HART1's data,modbus(4,5)
        sendDataTty(fdSerial,send_buff4, 8);
        printf("send_buff is %x,%x,%x,%x,%x,%x,%x,%x\n",send_buff4[0],send_buff4[1],send_buff4[2],send_buff4[3],send_buff4[4],send_buff4[5],send_buff4[6],send_buff4[7]); 
        sleep(1);
        ret=UART_Recv(fdSerial, recv_buff,64);  
        printf("recv_buff is %x,%x,%x,%x,%x,%x,%x,%x,%x\n",recv_buff[0],recv_buff[1],recv_buff[2],recv_buff[3],recv_buff[4],recv_buff[5],recv_buff[6],recv_buff[7],recv_buff[8]); 
        if(ret == -1)
            printf("Recv hart1 is incorrect!\n");
        unsigned char hart1[]={recv_buff[4],recv_buff[3],recv_buff[6],recv_buff[5]};
        UT_INPUT_REGISTERS_TAB[0] = (uint16_t)((recv_buff[3]<<8)+recv_buff[4]);
		UT_INPUT_REGISTERS_TAB[1] = (uint16_t)((recv_buff[5]<<8)+recv_buff[6]);
		float *HART1 = (float*)hart1;
		source[3].data = *HART1;
		printf("HART_1 = %f \n",*HART1);
		memset(recv_buff,'\0',sizeof(recv_buff));
        tcflush(fdSerial, TCIOFLUSH);//清掉串口缓存 
        printf("\n");
        sleep(1);

        //read HART2's data,modbus(6,7)
        sendDataTty(fdSerial,send_buff5, 8);
        printf("send_buff is %x,%x,%x,%x,%x,%x,%x,%x\n",send_buff5[0],send_buff5[1],send_buff5[2],send_buff5[3],send_buff5[4],send_buff5[5],send_buff5[6],send_buff5[7]); 
        sleep(1);
        ret=UART_Recv(fdSerial, recv_buff,64);  
        printf("recv_buff is %x,%x,%x,%x,%x,%x,%x,%x,%x\n",recv_buff[0],recv_buff[1],recv_buff[2],recv_buff[3],recv_buff[4],recv_buff[5],recv_buff[6],recv_buff[7],recv_buff[8]); 
        if(ret == -1)
            printf("Recv HART2 is incorrect!\n");
        unsigned char hart2[]={recv_buff[4],recv_buff[3],recv_buff[6],recv_buff[5]};
        UT_INPUT_REGISTERS_TAB[2] = (uint16_t)((recv_buff[3]<<8)+recv_buff[4]);
        UT_INPUT_REGISTERS_TAB[3] = (uint16_t)((recv_buff[5]<<8)+recv_buff[6]);
		float *HART2 = (float*)hart2;
		source[4].data = *HART2;
		printf("HART_2 = %f \n",*HART2);
		memset(recv_buff,'\0',sizeof(recv_buff));
        tcflush(fdSerial, TCIOFLUSH);//清掉串口缓存
        printf("\n");
        sleep(1);

        //read HART3's data,modbus(7,8)
        sendDataTty(fdSerial,send_buff6, 8);
        printf("send_buff is %x,%x,%x,%x,%x,%x,%x,%x\n",send_buff6[0],send_buff6[1],send_buff6[2],send_buff6[3],send_buff6[4],send_buff6[5],send_buff6[6],send_buff6[7]); 
        sleep(1);
        ret=UART_Recv(fdSerial, recv_buff,64);  
        printf("recv_buff is %x,%x,%x,%x,%x,%x,%x,%x,%x\n",recv_buff[0],recv_buff[1],recv_buff[2],recv_buff[3],recv_buff[4],recv_buff[5],recv_buff[6],recv_buff[7],recv_buff[8]); 
        if(ret == -1)
            printf("Recv HART3 is incorrect!\n");
        unsigned char hart3[]={recv_buff[4],recv_buff[3],recv_buff[6],recv_buff[5]};
        UT_INPUT_REGISTERS_TAB[4] = (uint16_t)((recv_buff[3]<<8)+recv_buff[4]);
        UT_INPUT_REGISTERS_TAB[5] = (uint16_t)((recv_buff[5]<<8)+recv_buff[6]);
        float *HART3 = (float*)hart3;
        source[5].data = *HART3;
        printf("HART_3 = %f \n",*HART3);
        memset(recv_buff,'\0',sizeof(recv_buff));
        tcflush(fdSerial, TCIOFLUSH);//清掉串口缓存
        printf("\n");
        sleep(1);

       
        //read DP1-1's data,modbus(8,9)
        sendDataTty(fdSerial,send_buff7, 8);
        printf("send_buff is %x,%x,%x,%x,%x,%x,%x,%x\n",send_buff7[0],send_buff7[1],send_buff7[2],send_buff7[3],send_buff7[4],send_buff7[5],send_buff7[6],send_buff7[7]); 
        sleep(1);
        ret=UART_Recv(fdSerial, recv_buff,64); 
        printf("recv_buff is %x,%x,%x,%x,%x,%x,%x,%x,%x\n",recv_buff[0],recv_buff[1],recv_buff[2],recv_buff[3],recv_buff[4],recv_buff[5],recv_buff[6],recv_buff[7],recv_buff[8]); 
        if(ret == -1)
            printf("Recv DP1 is incorrect!\n");
        unsigned char dp1[]={recv_buff[4],recv_buff[3],recv_buff[6],recv_buff[5]};
        UT_INPUT_REGISTERS_TAB[8] = (uint16_t)((recv_buff[3]<<8)+recv_buff[4]);
        UT_INPUT_REGISTERS_TAB[9] = (uint16_t)((recv_buff[5]<<8)+recv_buff[6]);
		float *DP1 = (float*)dp1;
		source[6].data = *DP1;
		printf("DP_1 = %f \n",*DP1);
		memset(recv_buff,'\0',sizeof(recv_buff));
        tcflush(fdSerial, TCIOFLUSH);//清掉串口缓存  
        printf("\n");
        sleep(1);

        //read DP2-2's data,modbus(10,11)
        sendDataTty(fdSerial,send_buff8, 8);
        printf("send_buff is %x,%x,%x,%x,%x,%x,%x,%x\n",send_buff8[0],send_buff8[1],send_buff8[2],send_buff8[3],send_buff8[4],send_buff8[5],send_buff8[6],send_buff8[7]); 
        sleep(2);
        ret=UART_Recv(fdSerial, recv_buff,64);  
        printf("recv_buff is %x,%x,%x,%x,%x,%x,%x,%x,%x\n",recv_buff[0],recv_buff[1],recv_buff[2],recv_buff[3],recv_buff[4],recv_buff[5],recv_buff[6],recv_buff[7],recv_buff[8]); 
        if(ret == -1)
            printf("Recv DP2 is incorrect!\n");
        unsigned char dp2[]={recv_buff[4],recv_buff[3],recv_buff[6],recv_buff[5]};
        UT_INPUT_REGISTERS_TAB[10] = (uint16_t)((recv_buff[3]<<8)+recv_buff[4]);
        UT_INPUT_REGISTERS_TAB[11] = (uint16_t)((recv_buff[5]<<8)+recv_buff[6]);
		float *DP2 = (float*)dp2;
		source[7].data = *DP2;
		printf("DP_2  = %f \n",*DP2);
		memset(recv_buff,'\0',sizeof(recv_buff));
        tcflush(fdSerial, TCIOFLUSH);//清掉串口缓存  
        printf("\n");
        sleep(2);
        
        //read DP3's data,modbus(10,11)
        sendDataTty(fdSerial,send_buff9, 8);
        printf("send_buff is %x,%x,%x,%x,%x,%x,%x,%x\n",send_buff9[0],send_buff9[1],send_buff9[2],send_buff9[3],send_buff9[4],send_buff9[5],send_buff9[6],send_buff9[7]); 
        sleep(2);
        ret=UART_Recv(fdSerial, recv_buff,64);  
        printf("recv_buff is %x,%x,%x,%x,%x,%x,%x,%x,%x\n",recv_buff[0],recv_buff[1],recv_buff[2],recv_buff[3],recv_buff[4],recv_buff[5],recv_buff[6],recv_buff[7],recv_buff[8]); 
        if(ret == -1)
            printf("Recv DP3 is incorrect!\n");
        unsigned char dp3[]={recv_buff[4],recv_buff[3],recv_buff[6],recv_buff[5]};
        UT_INPUT_REGISTERS_TAB[10] = (uint16_t)((recv_buff[3]<<8)+recv_buff[4]);
        UT_INPUT_REGISTERS_TAB[11] = (uint16_t)((recv_buff[5]<<8)+recv_buff[6]);
        float *DP3 = (float*)dp3;
        source[8].data = *DP3;
        printf("DP_3  = %f \n",*DP3);
        memset(recv_buff,'\0',sizeof(recv_buff));
        tcflush(fdSerial, TCIOFLUSH);//清掉串口缓存  
        printf("\n");
        sleep(1);

        //read DP4's data,modbus(10,11)
        sendDataTty(fdSerial,send_buff10, 8);
        printf("send_buff is %x,%x,%x,%x,%x,%x,%x,%x\n",send_buff10[0],send_buff10[1],send_buff10[2],send_buff10[3],send_buff10[4],send_buff10[5],send_buff10[6],send_buff10[7]); 
        sleep(1);
        ret=UART_Recv(fdSerial, recv_buff,64);  
        printf("recv_buff is %x,%x,%x,%x,%x,%x,%x,%x,%x\n",recv_buff[0],recv_buff[1],recv_buff[2],recv_buff[3],recv_buff[4],recv_buff[5],recv_buff[6],recv_buff[7],recv_buff[8]); 
        if(ret == -1)
            printf("Recv DP4 is incorrect!\n");
        unsigned char dp4[]={recv_buff[4],recv_buff[3],recv_buff[6],recv_buff[5]};
        UT_INPUT_REGISTERS_TAB[10] = (uint16_t)((recv_buff[3]<<8)+recv_buff[4]);
        UT_INPUT_REGISTERS_TAB[11] = (uint16_t)((recv_buff[5]<<8)+recv_buff[6]);
        float *DP4 = (float*)dp4;
        source[9].data = *DP4;
        printf("DP_4 = %f \n",*DP4);
        memset(recv_buff,'\0',sizeof(recv_buff));
        tcflush(fdSerial, TCIOFLUSH);//清掉串口缓存  
        printf("\n");
        sleep(1);
/*
        //read DP5's data,modbus(10,11)
        sendDataTty(fdSerial,send_buff11, 8);
        printf("send_buff is %x,%x,%x,%x,%x,%x,%x,%x\n",send_buff11[0],send_buff11[1],send_buff11[2],send_buff11[3],send_buff11[4],send_buff11[5],send_buff11[6],send_buff11[7]); 
        sleep(1);
        ret=UART_Recv(fdSerial, recv_buff,64);  
        printf("recv_buff is %x,%x,%x,%x,%x,%x,%x,%x,%x\n",recv_buff[0],recv_buff[1],recv_buff[2],recv_buff[3],recv_buff[4],recv_buff[5],recv_buff[6],recv_buff[7],recv_buff[8]); 
        if(ret == -1)
            printf("Recv DP5 is incorrect!\n");
        unsigned char dp5[]={recv_buff[4],recv_buff[3],recv_buff[6],recv_buff[5]};
        float *DP5 = (float*)dp5;
        source[10].data = *DP5;
        printf("DP_5 = %f \n",*DP5);
        memset(recv_buff,'\0',sizeof(recv_buff));
        tcflush(fdSerial, TCIOFLUSH);//清掉串口缓存  
        printf("\n");
        sleep(1);
*/

/*****simular read the 0x0c to 0x14 two datas*****/
/*        sendDataTty(fdSerial,send_buff12, 8);
        printf("send_buff is %x,%x,%x,%x,%x,%x,%x,%x\n",send_buff12[0],send_buff12[1],send_buff12[2],send_buff12[3],send_buff12[4],send_buff12[5],send_buff12[6],send_buff12[7]); 
        sleep(1);
        ret=UART_Recv(fdSerial,recv_buff,64);
        printf("recv_buff is ");

        for(i=0;i<21;i++)
            printf("%x ",recv_buff[i]);
        printf("\n");

        if(ret == -1)
            printf("Recv is incorrect");

        unsigned char dp6[]={recv_buff[4],recv_buff[3],recv_buff[6],recv_buff[5]};
        unsigned char dp7[]={recv_buff[8],recv_buff[7],recv_buff[10],recv_buff[9]};
        unsigned char dp8[]={recv_buff[12],recv_buff[11],recv_buff[14],recv_buff[13]};
        unsigned char dp9[]={recv_buff[16],recv_buff[15],recv_buff[18],recv_buff[17]};

        float *DP6 = (float*)dp6;
        float *DP7 = (float*)dp7;
        float *DP8 = (float*)dp8;
        float *DP9 = (float*)dp9;

        printf("DP6 = %f \n DP7 = %f \n DP8 = %f \n DP9 = %f \n",*DP6,*DP7,*DP8,*DP9);
        memset(recv_buff,'\0',sizeof(recv_buff)); 
        printf("\n");
        sleep(1);
*/
#endif

#if 0	//two mote as a part
        //read two hart's data
        sendDataTty(fdSerial,send_buff1, 8);
        printf("send_buff is %x,%x,%x,%x,%x,%x,%x,%x\n",send_buff1[0],send_buff1[1],send_buff1[2],send_buff1[3],send_buff1[4],send_buff1[5],send_buff1[6],send_buff1[7]); 
        ret=UART_Recv(fdSerial,recv_buff,64);

        printf("recv_buff is ");
        int i=0;
        for(i=0;i<16;i++)
            printf("%x ",recv_buff[i]);
        printf("\n");

        if(ret == -1)
            printf("Recv is incorrect");

        unsigned char ff1[]={recv_buff[4],recv_buff[3],recv_buff[6],recv_buff[5]};
        unsigned char ff2[]={recv_buff[8],recv_buff[7],recv_buff[10],recv_buff[9]};
        unsigned char ff3[]={recv_buff[12],recv_buff[11],recv_buff[14],recv_buff[13]};

        float *Filed1 = (float*)ff1;
        float *FF2 = (float*)ff2;
        float *FF3 = (float*)ff3;

        source[0].data = *Filed1;
        source[1].data = *FF2;
        source[2].data = *FF3;

        printf("FF1 = %f \n FF2 = %f \n FF3 = %f \n ",*Filed1,*FF2,*FF3);
        int j=0;
        for(i=0;i<4;i=i+2)
        {
            for(j=3;j<8;j=j+4)
            {
                UT_INPUT_REGISTERS_TAB[i] = (uint16_t)((recv_buff[j]<<8)+recv_buff[j+1]);
                UT_INPUT_REGISTERS_TAB[i+1] = (uint16_t)((recv_buff[j+2]<<8)+recv_buff[j+3]); 
            }   
        }
        memset(recv_buff,'\0',sizeof(recv_buff));  
        printf("\n"); 
        sleep(3);
/*
 		//read two dp's data
        sendDataTty(fdSerial,send_buff2, 8);
        printf("send_buff is %x,%x,%x,%x,%x,%x,%x,%x\n",send_buff2[0],send_buff2[1],send_buff2[2],send_buff2[3],send_buff2[4],send_buff2[5],send_buff2[6],send_buff2[7]); 
        ret=UART_Recv(fdSerial,recv_buff,64);

        printf("recv_buff is ");
        for(i=0;i<13;i++)
            printf("%x ",recv_buff[i]);
        printf("\n");

        if(ret == -1)
            printf("Recv is incorrect");

        unsigned char dp1[]={recv_buff[4],recv_buff[3],recv_buff[6],recv_buff[5]};
        unsigned char dp2[]={recv_buff[8],recv_buff[7],recv_buff[10],recv_buff[9]};

        for(i=0;i<4;i=i+2)
        {
            for(j=3;j<8;j=j+4)
            {
                UT_INPUT_REGISTERS_TAB[i] = (uint16_t)((recv_buff[j]<<8)+recv_buff[j+1]);
                UT_INPUT_REGISTERS_TAB[i+1] = (uint16_t)((recv_buff[j+2]<<8)+recv_buff[j+3]); 
            }   
        }
        float *DP1 = (float*)dp1;
        float *DP2 = (float*)dp2;
        
        source[2].data = *DP1;
        source[3].data = *DP2;

        printf("DP1 = %f \n DP2 = %f \n",*DP1,*DP2);
        memset(recv_buff,'\0',sizeof(recv_buff)); 
        printf("\n");
        sleep(3);
*/
        //read two ff's data
        sendDataTty(fdSerial,send_buff2, 8);
        printf("send_buff is %x,%x,%x,%x,%x,%x,%x,%x\n",send_buff2[0],send_buff2[1],send_buff2[2],send_buff2[3],send_buff2[4],send_buff2[5],send_buff2[6],send_buff2[7]); 
        ret=UART_Recv(fdSerial,recv_buff,64);

        printf("recv_buff is ");
        for(i=0;i<16;i++)
            printf("%x ",recv_buff[i]);
        printf("\n");

        if(ret == -1)
            printf("Recv is incorrect");

        unsigned char hart1[]={recv_buff[4],recv_buff[3],recv_buff[6],recv_buff[5]};
        unsigned char hart2[]={recv_buff[8],recv_buff[7],recv_buff[10],recv_buff[9]};
        unsigned char hart3[]={recv_buff[12],recv_buff[11],recv_buff[14],recv_buff[13]};

        float *HART1 = (float*)hart1;
        float *HART2 = (float*)hart2;
        float *HART3 = (float*)hart3;

        source[4].data = *HART1;
        source[5].data = *HART2;
        source[6].data = *HART3;

        printf("HART1 = %f \n HART2 = %f \n HART3 = %f \n",*HART1,*HART2,*HART3);
        for(i=0;i<4;i=i+2)
        {
            for(j=3;j<8;j=j+4)
            {
                UT_INPUT_REGISTERS_TAB[i] = (uint16_t)((recv_buff[j]<<8)+recv_buff[j+1]);
                UT_INPUT_REGISTERS_TAB[i+1] = (uint16_t)((recv_buff[j+2]<<8)+recv_buff[j+3]); 
            }   
        }
        memset(recv_buff,'\0',sizeof(recv_buff)); 
        printf("\n");  
        sleep(3);
#endif

 #if 0	//total send
        sendDataTty(fdSerial,send_buff, 8);
        printf("send_buff is %x,%x,%x,%x,%x,%x,%x,%x\n",send_buff[0],send_buff[1],send_buff[2],send_buff[3],send_buff[4],send_buff[5],send_buff[6],send_buff[7]); 
        ret=UART_Recv(fdSerial,recv_buff,64);

        printf("recv_buff is ");
        int i=0;
        for(i=0;i<29;i++)
            printf("%x ",recv_buff[i]);
        printf("\n");

        if(ret == -1)
            printf("Recv is incorrect");

        unsigned char hart1[]={recv_buff[4],recv_buff[3],recv_buff[6],recv_buff[5]};
        unsigned char hart2[]={recv_buff[8],recv_buff[7],recv_buff[10],recv_buff[9]};
        unsigned char hart3[]={recv_buff[12],recv_buff[11],recv_buff[14],recv_buff[13]};
        unsigned char ff1[]={recv_buff[16],recv_buff[15],recv_buff[18],recv_buff[17]};
        unsigned char ff2[]={recv_buff[20],recv_buff[19],recv_buff[22],recv_buff[21]};
        unsigned char ff3[]={recv_buff[24],recv_buff[23],recv_buff[26],recv_buff[25]};
        unsigned char dp1[]={recv_buff[28],recv_buff[27],recv_buff[30],recv_buff[29]};
        unsigned char dp2[]={recv_buff[32],recv_buff[31],recv_buff[34],recv_buff[33]};
        unsigned char dp3[]={recv_buff[36],recv_buff[35],recv_buff[38],recv_buff[37]};
        unsigned char dp4[]={recv_buff[40],recv_buff[39],recv_buff[42],recv_buff[41]};


        float *HART1 = (float*)hart1;
        float *HART2 = (float*)hart2;
        float *HART3 = (float*)hart3;
        float *F1 = (float*)ff1;
        float *F2 = (float*)ff2;
        float *F3 = (float*)ff3;
        float *DP1 = (float*)dp1;
        float *DP2 = (float*)dp2;
        float *DP3 = (float*)dp3;
        float *DP4 = (float*)dp4;

        source[0].data = *HART1;
        source[1].data = *HART2;
        source[2].data = *F1;
        source[3].data = *F2;
        source[4].data = *DP1;
        source[5].data = *DP2;

        printf("\n HART1 = %f \nHART2 = %f \nHART3 = %f \nF1 = %f \nF2 = %f \nF3 = %f \n DP1 = %f \n DP2 = %f \nDP3 = %f \n DP4 = %f \n",*HART1,*HART2,*HART3,*F1,*F2,*F3,*DP1,*DP2,*DP3,*DP4);
        int j=0;
        for(i=0;i<12;i=i+2)
        {
            for(j=3;j<8;j=j+4)
            {
                UT_INPUT_REGISTERS_TAB[i] = (uint16_t)((recv_buff[j]<<8)+recv_buff[j+1]);
                UT_INPUT_REGISTERS_TAB[i+1] = (uint16_t)((recv_buff[j+2]<<8)+recv_buff[j+3]); 
            }
        }
        memset(recv_buff,'\0',sizeof(recv_buff));  
        printf("\n"); 
        sleep(2);
 #endif
        //int i=0;
        for (i=0; i < UT_REGISTERS_NB; i++) 
        {
        	mb_mapping->tab_registers[i] = UT_REGISTERS_TAB[i];
            if(i==0)
            printf("test data is %d\n",mb_mapping->tab_registers[i]);
        } 

    }

}      

