#include <stdio.h> // 标准输入输头文件
#include <stdlib.h> // 标准库头文件 
#include <sys/types.h> // 
#include <sys/socket.h>
#include <netinet/in.h> // Internet Protocol family
#include <string.h> // 需要用到字符串比较函数
#include <ctype.h> // 字符类型判断
#include <sys/stat.h> // 获取文件状态

// 是Unix Standard的缩写，所定义的接口通常都是大量针对系统调用的封装
// provides access to the POSIX operating system API
#include <unistd.h> 


#define ISspace(x) isspace((int)(x)) // From <ctype.h>
#define SERVER_STRING "Server: macOS Monterey 12.0.1(httpd-0.1.0)\r\n"

int startup(u_short *port);
void error_die(const char *sc);
int get_line(int sock, char *buf, int size);
void ServeFile(int client, const char *filename);
void Headers(int client, const char *filename);
void Cat(int client, FILE *resource);



void accept_request(int client);
void NotFound();



void execute_cgi(int client, const char *path,const char *method, const char *query_string)
{

}





void Headers(int client, const char *filename)
{
    char buf[1024];
    //(void)filename;  /* could use filename to determine file type */
    strcpy(buf, "HTTP/1.0 200 OK\r\n");
    send(client, buf, strlen(buf), 0);
    strcpy(buf, SERVER_STRING);
    send(client, buf, strlen(buf), 0);
    sprintf(buf, "Content-Type: text/html\r\n");
    send(client, buf, strlen(buf), 0);
    strcpy(buf, "\r\n");
    send(client, buf, strlen(buf), 0);
    return;
}
void Cat(int client, FILE *resource)
{
    char buf[1024];
    fgets(buf, sizeof(buf), resource);
    while (!feof(resource))
    {
        send(client, buf, strlen(buf), 0);
        fgets(buf, sizeof(buf), resource);
    }
    return;
}

void NotFound()
{
    printf("404 error");
}

void ServeFile(int client, const char *filename)
{
    FILE *resource = NULL;
    resource = fopen(filename, "r");
    if (resource == NULL)
        NotFound();
    else
    {
        Headers(client, filename);
        Cat(client, resource);
    }
    fclose(resource);
}

/**********************************************************************/
/* Print out an error message with perror() (for system errors; based
 * on value of errno, which indicates system call errors) and exit the
 * program indicating an error. */
/**********************************************************************/
void error_die(const char *sc) // print erron message
{
    perror(sc); // from stdio.h
    exit(1); // end the program exit() function is from stdlib.h
}



int startup(u_short *port)
{
    int httpd = 0;
    // Socket address, internet style. from  <netinet/in.h>
    struct sockaddr_in name; 
    /**********************************************************************/
    /* 
    * NAME:socket - create an endpoint for communication
    * FROM: #include <sys/socket.h>
    * SYNOPSIS:int socket(int domain, int type, int protocol);
    * Domain ----The address domain requested, either AF_INET, AF_INET6, AF_UNIX, or AF_RAW 
    * Type ----The type of socket created, either SOCK_STREAM(for TCP), SOCK_DGRAM(for udp), or SOCK_RAW
    * protocol -----The protocol requested. Some possible values are 0, IPPROTO_UDP, or IPPROTO_TCP
    * 
    * socket() returns a nonnegative integer, the socket file descriptor upon successful completion
    * Active Sockets created with the socket() function are initially unnamed
    * */
    /**********************************************************************/

    httpd = socket(AF_INET, SOCK_STREAM, 0); 
    if (httpd == -1) // a value of -1 is returned and errno is set to indicate the error
        error_die("socket"); // error type "socket"
    memset(&name, 0, sizeof(name)); // Fill block of memory for struct sockaddr_in name with 0 // memset from string.h
    name.sin_family = AF_INET; //  internetwork: UDP, TCP, etc.

    // IP协议中定义网络字节序为大端序
    // converts the unsigned short integer hostshort from host byte order to network byte order.
    name.sin_port = htons(*port); 
    // converts the unsigned integer hostlong from host byte order to network byte order.
    // INADDR_ANY 表示任意地址 含义是让服务器端任意IP地址均可以做为服务器IP地址
    // If this field is set to the constant INADDR_ANY, as defined in netinet/in.h
    // the caller is requesting that the socket be bound to all network interfaces on the host.
    // UDP packets and TCP connections from all interfaces (which match the bound name) 
    // are routed to the application
    name.sin_addr.s_addr = htonl(INADDR_ANY);


    /**********************************************************************/
    /* 
    * NAME:socket - bind - bind a name to a socket
    * FROM: #include <sys/socket.h>
    * Description: transform an active socket into a passive socket by binding a name to the socket with the bind() call
    * SYNOPSIS:int bind(int socket, const struct sockaddr *address,socklen_t address_len);
    * socket----The socket descriptor returned by a previous socket() call.
    * address----The pointer to a sockaddr structure containing the name that is to be bound to socket.
    * address_len----The size of address in bytes.
    * */
    /**********************************************************************/
    if (bind(httpd, (struct sockaddr *)&name, sizeof(name)) == -1)
        error_die("bind");
    
    //  If sin_port is set to 0, the caller leaves it to the system to assign an available port
    // The application can call getsockname() to discover the port number assigned.
    if (*port == 0)  
    {
        socklen_t namelen = sizeof(name);
        // int getsockname(int socket, struct sockaddr *name, size_t *namelen);
        if (getsockname(httpd, (struct sockaddr *)&name, &namelen) == -1)
            error_die("getsockname");
        *port = ntohs(name.sin_port); //网络字节序转主机字节序,获取系统动态分配的端口信息
    }

    /**********************************************************************/
    /* NAME:listen for socket connections and limit the queue of incoming connections
    * FROM:#include <sys/socket.h>
    * SYNOPSIS:int listen(int socket, int backlog);
    * socket
    *      The socket descriptor.
    * backlog
    *      Defines the maximum length for the queue of pending connections.
    * Description:
    * Prepare the server for incoming client requests
    * The listen() function applies only to stream sockets
    * It indicates a readiness to accept client connection requests
    * creates a connection request queue of length backlog to queue incoming connection requests.
    * Once full, additional connection requests are rejected.
    * It transforms an active socket into a passive socket.
    * Once called, socket can never be used as an active socket to initiate connection requests
    * */
    /**********************************************************************/
    if (listen(httpd, 10) == -1)
        error_die("listen");
    return(httpd); // 返回系统socket套接字文件描述符
}



/**********************************************************************/
/* 
 * 从请求报文中获取一行
 * 返回读取的字符数目，不包括
 * */
/**********************************************************************/
int get_line(int sock, char *buf, int size)
{
    int i = 0;
    char c = '\0';
    int size_t = 0;
    // i < size - 1 确保留有空格填写'\0'
    while (i < size - 1) // 仍然有空间填写
    {
        size_t = recv(sock,&c,1,0); // 读取一个字节
        if(size_t > 0)
        {
            if(c == '\r')
            {
                size_t = recv(sock,&c,1,0); // '\n'读取出去，这个时候表示一行读取结束
                break;
            }else{
                buf[i] = c;
                i++;
            }
        }else
            break;
    }
    buf[i] = '\0';
    return i; // 返回读取的字符数目
}
/**********************************************************************/
/* A request has caused a call to accept() on the server port to
 * return.  Process the request appropriately.
 * Parameters: the socket connected to the client */
/**********************************************************************/
void accept_request(int client)
{
    int cgi = 0;
    char buf[1024];
    int num = get_line(client, buf, sizeof(buf));
    //printf("%s\n",buf);
    unsigned long i = 0,j = 0;
    // 获取方法字符串
    char method[255];// 请求行方法
    while(!ISspace(buf[j]) && (i < sizeof(method) - 1) && (j < sizeof(buf)))
    {
        method[i] = buf[j];
        i++; 
        j++;
    }
    method[i] = '\0';//要以0结尾，C风格字符串
    //printf("%s\n",method);
    // 获取文件路径
    i = 0;
    while(ISspace(buf[j]) && ( j < sizeof(buf)))
        j++; // 跳过空格
    char url[255];
    while(!ISspace(buf[j]) && (i < sizeof(url) - 1) && (j < sizeof(buf)))
    {
        url[i] = buf[j];
        i++; 
        j++;
    }
    url[i] = '\0';// 读取到URL

    //处理通过get方法生成表单，在问好'?'后面添加字段
    char *q = NULL;
    if(strcasecmp(method, "GET") == 0)
    {
        q = url;
        while((*q != '?') && (*q != '\0'))
            q++;
        if(*q == '?')
        {
            cgi = 1;// 
            *q = '\0'; // 把路径和后面的字段隔开
            q++;
        }
    }
    if (strcasecmp(method, "POST") == 0)
        cgi = 1;

    char path[512];
    sprintf(path, "htdocs%s", url); //加上htdoc生成源文件的路径
    if(path[strlen(path) - 1] == '/') // 自动添加index.html，默认访问时返回主页index.html
        strcat(path, "index.html");
    printf("%s\n",path);

    struct stat st; // 用于保存文件状态
    if(stat(path,&st) == -1 ) // 表示失败了,没有权限或者没找到对象
    {
        while(get_line(client, buf, sizeof(buf)));
        NotFound(); // 404
    }else{
        if(cgi == 0)
        {
            ServeFile(client, path);
        }else{

        }
    }
    close(client);
}




int main()
{
    int server_sock = -1;
    int client_sock = -1;
    // FROM：#include <netinet/in.h> 
    struct sockaddr_in client_name; //Socket的接口地址类，接口地址包括：ip和端口号
    socklen_t client_name_len = sizeof(client_name);
    u_short port = 0; // 端口
    // server_sock用来建立连接
    server_sock = startup(&port);  // server_sock是服务器socke套接字文件描述符
    printf("httpd running on port %d\n", port);
    while(1)
    {
        /* NAME: accept a new connection on a socket
        ** FROM: #include <sys/socket.h>
        ** SYNOPSIS: int accept(int socket, struct sockaddr *restrict address,socklen_t *restrict address_len);
        ** DESCRIPTION:     
        **  The accept() function shall extract the first connection on the queue of pending connections,
        **  create a new socket with the same socket type protocol and address family as the specified socket, 
        **  and allocate a new file descriptor for that socket.
        */
        // client_sock用来通信
        // client_sock --- The new socket descriptor cannot be used to accept new connections
        // server_sock --- The original socket, socket, remains available to accept more connection requests.
        client_sock = accept(server_sock,(struct sockaddr *)&client_name,&client_name_len);
        if (client_sock == -1)
            error_die("accept");
        else{
            accept_request(client_sock);
        }
    }
    close(server_sock); // 关闭socket套接字接口
    return 0;

}