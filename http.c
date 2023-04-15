#include <stdio.h> // 标准输入输头文件
#include <stdlib.h> // 标准库头文件 atoi转换
#include <sys/types.h> // 
#include <sys/socket.h>
#include <netinet/in.h> // Internet Protocol family
#include <string.h> // 需要用到字符串比较函数
#include <ctype.h> // 字符类型判断
#include <sys/stat.h> // 获取文件状态
// 是Unix Standard的缩写，所定义的接口通常都是大量针对系统调用的封装
// provides access to the POSIX operating system API
// 如fork()、pipe()
#include <unistd.h> 
#include <time.h>


#define ISspace(x) isspace((int)(x)) // From <ctype.h>
#define SERVER_STRING "Server: macOS Monterey 12.0.1(httpd-0.1.0)\r\n"

int startup(u_short *port);
int GetLine(int sock, char *buf, int size);




void error_die(const char *sc);

void ServeFile(int client, const char *filename);
void Headers(int client, const char *filename);
void Cat(int client, FILE *resource);
void OK(int client);
void NotFound(int client);
void accept_request(int client);
void BadRequest(int client);
void CannotExecute(int client);
void Execute(int client, const char *path,const char *method, const char *query_string);




void CannotExecute(int client)
{
    char buf[1024];
    sprintf(buf, "HTTP/1.0 500 Internal Server Error\r\n");
    send(client, buf, strlen(buf), 0);
    sprintf(buf, "Content-type: text/html\r\n");
    send(client, buf, strlen(buf), 0);
    sprintf(buf, "\r\n");
    send(client, buf, strlen(buf), 0);
    sprintf(buf, "<P>Error prohibited CGI execution.\r\n");
    send(client, buf, strlen(buf), 0);
    return;
}
void BadRequest(int client)
{
    char buf[1024];
    sprintf(buf, "HTTP/1.0 400 BAD REQUEST\r\n");
    send(client, buf, sizeof(buf), 0);
    sprintf(buf, "Content-type: text/html\r\n");
    send(client, buf, sizeof(buf), 0);
    sprintf(buf, "\r\n");
    send(client, buf, sizeof(buf), 0);
    sprintf(buf, "<P>Your browser sent a bad request, ");
    send(client, buf, sizeof(buf), 0);
    sprintf(buf, "such as a POST without a Content-Length.\r\n");
    send(client, buf, sizeof(buf), 0);
    return;
}


void Execute(int client, const char *path,const char *method, const char *query_string)
{
    char c;
    int status;
    char buf[1024];
    int content_length = -1;
    if(strcasecmp(method, "POST") == 0) // 如果是POST得获取content_length大小，GET啥也不做
    {
        while(GetLine(client, buf, sizeof(buf)))
        {
            buf[15] = '\0'; // 取得到的一行的buf前面0-14个字符串与"Content-Length:"比较
            if (strcasecmp(buf, "Content-Length:") == 0) // 即取这个字段的值
                content_length = atoi(&(buf[16])); // 取值，并将字符串转成整数
        }
        if(content_length == -1) {
            BadRequest(client);
            return;
        }
    }
    OK(client); // 输出响应头

    int cgi_output[2];
    int cgi_input[2];
    pid_t pid;
    // 创建管道用于进程间通信，失败时返回 -1，只能永远有血缘关系的进程之间通信
    // cgi_output[0]只能读，不能写，cgi_output[1]只能写不能读，即cgi_output是单向流动的
    if (pipe(cgi_output) < 0) {
        CannotExecute(client);
        return;
    }
    // cgi_input是相对于cgi_output的另外一条管道，通信方向不一样
    if(pipe(cgi_input) < 0) {
        CannotExecute(client);
        return;
    }
    // 创建一个子进程，这个子进程用于指向cgi程序
    if ((pid = fork()) < 0 ) {
        CannotExecute(client);
        return;
    }
    // CGI读数据是从标准输入stdin，写数据是写到标准输出stdout
    // fork执行一次返回2个值，在父进程返回子进程的进程id，子进程返回0
    if(pid == 0) // 执行子进程
    {
        char meth_env[255];
        // 1代表标准输出，dup2将标准输出绑定到了cgi_output[1]指向的管道文件
        // 代表着子进程的输出都将写入cgi_output的写入口1写入管道
        dup2(cgi_output[1], 1); 
        // 0代表标准输入，dup2将标准输入绑定到了cgi_input[0]
        // 这意味着子进程将从cgi_input[0]口从管道cgi_input读取数据
        dup2(cgi_input[0], 0);  
        // 子进程复制了父进程创建的这两个管道，它们共享这两个管道用来通信
        // 单个管道负责一个方向的通信 
        // cgi_output负责从子进程到父进程的数据
        // cgi_input 负责从父进程到子进程到数据
        // 因此在子进程中，有必要关闭2个用不到的端口
        close(cgi_output[0]);//关闭cgi_output管道的读取口
        close(cgi_input[1]);// 关闭cgi_input管道的写入口
        sprintf(meth_env, "REQUEST_METHOD=%s", method);
        putenv(meth_env); // 给系统增加环境变量，如果本来有，就改变，没有就添加
        // 如果是GET方法，那么CGI程序要从QUERY_STRING中读取信息
        if(strcasecmp(method, "GET") == 0) {
            char query_env[255];
            sprintf(query_env, "QUERY_STRING=%s", query_string); // 采用get方法时传输的信息
            putenv(query_env); // 添加环境变量
        }else{  
            // 如果是POST方法，那么CGI程序要根据这个CONTENT_LENGTH从标准输入读取信息
            char length_env[255];
            sprintf(length_env, "CONTENT_LENGTH=%d", content_length);// 有效信息长度
            putenv(length_env); // 添加环境变量
        }
        // 没有权限，那么无法执行该CGI程序
        if(execl(path, path, NULL) < 0) // 无法执行会返回-1 
        exit(0); //子进程结束

    }else{ // 执行父进程
        close(cgi_output[1]); // 父进程关闭没必要的cgi_output管道写口，父进行用这个管道来读
        close(cgi_input[0]); //  父进程关闭没必要的cgi_input管道读口，父进程用这个管道来写
        if(strcasecmp(method, "POST") == 0){
            
            for(int i = 0; i < content_length; i++) {
                recv(client, &c, 1, 0); // 从client读取content_length个字节
                // write()会把参数c所指的内存写入1个字节到cgi_input[1]所指的文件内
                write(cgi_input[1], &c, 1); // 然后将这个字节数据写入cgi_input管道
            }
        }
        // 读取cgi程序写入的数据
        // read函数从文件cgi_output[0]读取期望1个字节到C中，出错返回-1，否则实际读取到字节数
        while(read(cgi_output[0], &c, 1) > 0)
        {
            send(client, &c, 1, 0); // 发送给client
        }
        close(cgi_output[0]);
        close(cgi_input[1]);
        // 等待子进程结束，没有结束父进程阻塞
        waitpid(pid, &status, 0);// 等待进程pid子进程结束，status返回子进程退出状态
    }
}


void Headers(int client, const char *filename)
{
    char buf[1024];
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
    while(!feof(resource))
    {
        send(client, buf, strlen(buf), 0);
        fgets(buf, sizeof(buf), resource);
    }
    return;
}

void NotFound(int client)
{
    char buf[1024];
    sprintf(buf, "HTTP/1.0 404 NOT FOUND\r\n");
    send(client, buf, strlen(buf), 0);
    sprintf(buf, SERVER_STRING);
    send(client, buf, strlen(buf), 0);
    sprintf(buf, "Content-Type: text/html\r\n");
    send(client, buf, strlen(buf), 0);
    sprintf(buf, "\r\n");
    send(client, buf, strlen(buf), 0);
    sprintf(buf, "<HTML><TITLE>Not Found</TITLE>\r\n");
    send(client, buf, strlen(buf), 0);
    sprintf(buf, "<BODY><P>The server could not fulfill\r\n");
    send(client, buf, strlen(buf), 0);
    sprintf(buf, "your request because the resource specified\r\n");
    send(client, buf, strlen(buf), 0);
    sprintf(buf, "is unavailable or nonexistent.\r\n");
    send(client, buf, strlen(buf), 0);
    sprintf(buf, "</BODY></HTML>\r\n");
    send(client, buf, strlen(buf), 0);
    return;
}

void OK(int client)
{
    char buf[1024];
    strcpy(buf, "HTTP/1.0 200 OK\r\n");
    send(client, buf, strlen(buf), 0);
    strcpy(buf, SERVER_STRING);
    send(client, buf, strlen(buf), 0);
    time_t t;
    time(&t);
    sprintf(buf, "Time: %s",asctime(gmtime(&t)));
    send(client, buf, strlen(buf), 0);
    strcpy(buf, "Content-Type: text/html\r\n");
    send(client, buf, strlen(buf), 0);
    strcpy(buf, "\r\n");
    send(client, buf, strlen(buf), 0);
    return;
}
void ServeFile(int client, const char *filename)
{
    FILE *resource = NULL;
    resource = fopen(filename, "r");
    if(resource == NULL)
        NotFound(client);
    else
    {
        OK(client);
        Cat(client,resource);
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
/* 从请求报文中获取一行
 * 返回读取的字符数目，不包括
 * */
/**********************************************************************/
int GetLine(int sock, char *buf, int size)
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
                recv(sock,&c,1,0); // '\n'读取出去，这个时候表示一行读取结束
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
    int  cgi = 0;
    char buf[1024];
    GetLine(client, buf, sizeof(buf));
    //处理通过get方法生成表单，在问好'?'后面添加字段
    char *query_string = NULL;
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
    // 获取链接路径
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

    if(strcasecmp(method, "GET") && strcasecmp(method, "POST"))//既不是GET也不是POST
    {
        // 暂时未实现
        close(client);
        return;
    }
    if(strcasecmp(method, "GET") == 0)
    {
        while(GetLine(client, buf, sizeof(buf)));//清空GET请求头，否则位情况就关闭会导致出错
        // GET方法也可以提交表单信息，是在url+'?'+xxx==xxx'
        query_string = url;
        while((*query_string != '?') && (*query_string != '\0'))
            query_string++;
        if (*query_string == '?')
        {
            cgi = 1;
            *query_string = '\0';
            query_string++;
        }
    }
    if(strcasecmp(method, "POST") == 0)
    {
        //printf("动态文件1");
        cgi = 1;
    } 

    //加上htdoc生成源文件的路径
    char path[512];
    sprintf(path, "htdocs%s", url); 
    if(path[strlen(path) - 1] == '/') // 自动添加index.html，默认访问时返回主页index.html
        strcat(path, "index.html");
    //printf("%s\n",path);
    struct stat st; // 用于保存文件状态
    if(stat(path,&st) == -1 ) // 表示失败了,没有权限或者没找到对象
    {
        NotFound(client); // 404
    }else{
        // 文件是可执行文件，并且使用GET方法 如请求头 Get /color.cgi HTTP1.0，无论有没有参数
        // 否则文件只是会单纯显示出来
        // 当然你需要查看或者改变color.cgi脚步的执行权限
        if((st.st_mode & S_IXUSR) || (st.st_mode & S_IXGRP) || (st.st_mode & S_IXOTH))
            cgi = 1;
        if(cgi == 0) // 静态文件
        {
            ServeFile(client,path);
        }else{ // 请求动态文件
            Execute(client,path,method,query_string);
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
            //printf("accept\n");
            accept_request(client_sock);
        }
    }
    close(server_sock); // 关闭socket套接字接口
    return 0;

}