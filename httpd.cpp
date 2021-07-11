#include <stdio.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <ctype.h>
#include <strings.h>
#include <string.h>
#include <sys/stat.h>
#include <pthread.h>
#include <sys/wait.h>
#include <stdlib.h>
#include <sys/epoll.h>
#include <iostream>
#include "threadpool.h"

//宏定义，是否是空格
#define ISspace(x) isspace((int)(x))
#define SERVER_STRING "Server: GuoJ's http/0.1.0\r\n"

//每次收到请求，创建一个线程来处理接受到的请求，把client_sock转成地址作为参数传入pthread_create
void *accept_request(void* client);

//错误请求
void bad_request(int);

//读取文件
void cat(int, FILE *);

//无法执行
void cannot_execute(int);

//错误输出
void error_die(const char *);

//执行cgi脚本
void execute_cgi(int, const char *, const char *, const char *);

//得到一行数据,只要发现c为\n,就认为是一行结束，如果读到\r,再用MSG_PEEK的方式读入一个字符，如果是\n，从socket用读出
//如果是下个字符则不处理，将c置为\n，结束。如果读到的数据为0中断，或者小于0，也视为结束，c置为\n
int get_line(int, char *, int);

//返回http头
void headers(int, const char *);

//没有发现文件
void not_found(int);

//如果不是CGI文件，直接读取文件返回给请求的http客户端
void serve_file(int, const char *);

//开启tcp连接，绑定端口等操作
int startup(u_short *);

//如果不是Get或者Post，就报方法没有实现
void unimplemented(int);


//处理监听到的 HTTP 请求
//后续主要是处理这个头
//
// GET / HTTP/1.1
// Host: 192.168.0.23:47310
// Connection: keep-alive
// Upgrade-Insecure-Requests: 1
// User-Agent: Mozilla/5.0 (Windows NT 6.1; WOW64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/55.0.2883.87 Safari/537.36
// Accept: text/html,application/xhtml+xml,application/xml;q=0.9,image/webp,*/*; q = 0.8
// Accept - Encoding: gzip, deflate, sdch
// Accept - Language : zh - CN, zh; q = 0.8
// Cookie: __guid = 179317988.1576506943281708800.1510107225903.8862; monitor_count = 5
//
// POST / color1.cgi HTTP / 1.1
// Host: 192.168.0.23 : 47310
// Connection : keep - alive
// Content - Length : 10
// Cache - Control : max - age = 0
// Origin : http ://192.168.0.23:40786
// Upgrade - Insecure - Requests : 1
// User - Agent : Mozilla / 5.0 (Windows NT 6.1; WOW64) AppleWebKit / 537.36 (KHTML, like Gecko) Chrome / 55.0.2883.87 Safari / 537.36
// Content - Type : application / x - www - form - urlencoded
// Accept : text / html, application / xhtml + xml, application / xml; q = 0.9, image / webp, */*;q=0.8
// Referer: http://192.168.0.23:47310/
// Accept-Encoding: gzip, deflate
// Accept-Language: zh-CN,zh;q=0.8
// Cookie: __guid=179317988.1576506943281708800.1510107225903.8862; monitor_count=281
// Form Data
// color=gray
void *accept_request(void* from_client)
{
    int client = *(int *)from_client;
    char buf[1024];
    int numchars;
    char method[255];
    char url[255];
    char path[512];
    size_t i, j;
    struct stat st;
    int cgi = 0;
    char *query_string = NULL;

    //读http 请求的第一行数据（request line），把请求方法存进 method 中
    numchars = get_line(client, buf, sizeof(buf));

    i = 0;
    j = 0;
    while (!ISspace(buf[j]) && (i < sizeof(method) - 1))
    {
        //提取其中的请求方式
        method[i] = buf[j];
        i++;
        j++;
    }
    method[i] = '\0';

    //如果请求的方法不是 GET 或 POST 任意一个的话就直接发送 response 告诉客户端没实现该方法
    if (strcasecmp(method, "GET") && strcasecmp(method, "POST"))
    {
        unimplemented(client);
        return NULL;
    }

    //如果是 POST 方法就将 cgi 标志变量置一(true)
    if (strcasecmp(method, "POST") == 0)
    {
        cgi = 1;
    }
    i = 0;
    //跳过所有的空白字符(空格)
    while (ISspace(buf[j]) && (j < sizeof(buf)))
    {
        j++;
    }

    //然后把 URL 读出来放到 url 数组中
    while (!ISspace(buf[j]) && (i < sizeof(url) - 1) && (j < sizeof(buf)))
    {
        url[i] = buf[j];
        i++; j++;
    }
    url[i] = '\0';


    //如果这个请求是一个 GET 方法的话
    //GET请求url可能会带有?,有查询参数
    if (strcasecmp(method, "GET") == 0)
    {
        //用一个指针指向 url
        query_string = url;
        //去遍历这个 url，跳过字符 ？前面的所有字符，如果遍历完毕也没找到字符 ？则退出循环
        while ((*query_string != '?') && (*query_string != '\0'))
        {
            query_string++;
        }

        /* 如果有?表明是动态请求, 开启cgi */
        if (*query_string == '?')
        {
            cgi = 1;
            *query_string = '\0';
            query_string++;
        }
    }
    //将前面分隔两份的前面那份字符串，拼接在字符串htdocs的后面之后就输出存储到数组 path 中。相当于现在 path 中存储着一个字符串
    sprintf(path, "httpdocs%s", url);

    //如果 path 数组中的这个字符串的最后一个字符是以字符 / 结尾的话，就拼接上一个"index.html"的字符串。首页的意思
    if (path[strlen(path) - 1] == '/')
    {
        strcat(path, "test.html");
    }
    //在系统上去查询该文件是否存在
    if (stat(path, &st) == -1)
    {
        //如果不存在，那把这次 http 的请求后续的内容(head 和 body)全部读完并忽略
        while ((numchars > 0) && strcmp("\n", buf))
        {
            numchars = get_line(client, buf, sizeof(buf));
        }
        //然后返回一个找不到文件的 response 给客户端
        not_found(client);
    }
    else
    {
        //文件存在，那去跟常量S_IFMT相与，相与之后的值可以用来判断该文件是什么类型的
        if ((st.st_mode & S_IFMT) == S_IFDIR)//S_IFDIR代表目录
            //如果请求参数为目录, 自动打开test.html
        {
            strcat(path, "/test.html");
        }

        //文件可执行, 不论是属于用户/组/其他这三者类型的，就将 cgi 标志变量置1
        if ((st.st_mode & S_IXUSR) ||
                (st.st_mode & S_IXGRP) ||
                (st.st_mode & S_IXOTH))
            //S_IXUSR:文件所有者具可执行权限
            //S_IXGRP:用户组具可执行权限
            //S_IXOTH:其他用户具可读取权限
        {
            cgi = 1;
        }

        if (!cgi)
        {
            //如果不需要 cgi 机制的话
            serve_file(client, path);
        }
        else
        {
            //如果需要则调用
            execute_cgi(client, path, method, query_string);
        }
    }

    close(client);
    //printf("connection close....client: %d \n",client);
    return NULL;
}



void bad_request(int client)
{
    char buf[1024];
    //发送400
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
}


void cat(int client, FILE *resource)
{
    //发送文件的内容
    char buf[1024];
    //从文件文件描述符中读取指定内容
    fgets(buf, sizeof(buf), resource);
    while (!feof(resource))
    {
        send(client, buf, strlen(buf), 0);
        fgets(buf, sizeof(buf), resource);
    }
}


void cannot_execute(int client)
{
    char buf[1024];
    //发送500
    sprintf(buf, "HTTP/1.0 500 Internal Server Error\r\n");
    send(client, buf, strlen(buf), 0);
    sprintf(buf, "Content-type: text/html\r\n");
    send(client, buf, strlen(buf), 0);
    sprintf(buf, "\r\n");
    send(client, buf, strlen(buf), 0);
    sprintf(buf, "<P>Error prohibited CGI execution.\r\n");
    send(client, buf, strlen(buf), 0);
}


void error_die(const char *sc)
{
    //基于当前的errno值，在标准错误上产生一条错误消息
    perror(sc);
    exit(1);
}


//执行cgi动态解析
void execute_cgi(int client, const char *path,
                 const char *method, const char *query_string)
{
    //缓冲区
    char buf[1024];
    //两根管道
    int cgi_output[2];
    int cgi_input[2];

    //进程pid和状态
    pid_t pid;
    int status;

    int i;
    char c;

    //读取的字符数
    int numchars = 1;
    //http的content_length
    int content_length = -1;
    //默认字符
    buf[0] = 'A';
    buf[1] = '\0';
    //如果是 http 请求是 GET 方法的话读取并忽略请求剩下的内容
    if (strcasecmp(method, "GET") == 0)
    {
        //读取数据，把整个header都读掉，以为Get写死了直接读取index.html，没有必要分析余下的http信息了
        while ((numchars > 0) && strcmp("\n", buf))
        {
            numchars = get_line(client, buf, sizeof(buf));
        }
    }
    else
    {
        //只有 POST 方法才继续读内容
        numchars = get_line(client, buf, sizeof(buf));
        //这个循环的目的是读出指示 body 长度大小的参数，并记录 body 的长度大小。
        //其余的 header 里面的参数一律忽略
        //注意这里只读完 header 的内容，body 的内容没有读
        //POST请求，就需要得到Content-Length，Content-Length：这个字符串一共长为15位，所以
        //取出头部一句后，将第16位设置结束符，进行比较
        //第16位置为结束
        while ((numchars > 0) && strcmp("\n", buf))
        {
            buf[15] = '\0';
            if (strcasecmp(buf, "Content-Length:") == 0)
            {
                //记录 body 的长度大小
                //内存从第17位开始就是长度，将17位开始的所有字符串转成整数就是content_length
                content_length = atoi(&(buf[16]));
            }

            numchars = get_line(client, buf, sizeof(buf));
        }
        //如果 http 请求的 header 没有指示 body 长度大小的参数，则报错返回
        if (content_length == -1)
        {
            bad_request(client);
            return;
        }
    }

    sprintf(buf, "HTTP/1.0 200 OK\r\n");
    send(client, buf, strlen(buf), 0);

    //下面这里创建两个管道，用于两个进程间通信
    if (pipe(cgi_output) < 0) { //建立output管道
        cannot_execute(client);
        return;
    }
    if (pipe(cgi_input) < 0) {  //建立input管道
        cannot_execute(client);
        return;
    }

    //       fork后管道都复制了一份，都是一样的
    //       子进程关闭2个无用的端口，避免浪费
    //       ×<------------------------->1    output
    //       0<-------------------------->×   input

    //       父进程关闭2个无用的端口，避免浪费
    //       0<-------------------------->×   output
    //       ×<------------------------->1    input
    //       此时父子进程已经可以通信
    //创建一个子进程
    //fork进程，子进程用于执行CGI
    //父进程用于收数据以及发送子进程处理的回复数据
    if ( (pid = fork()) < 0 )
    {
        cannot_execute(client);
        return;
    }
    if (pid == 0)  /* 子进程: 运行CGI 脚本 */
    {
        char meth_env[255];
        char query_env[255];
        char length_env[255];

        //将子进程的输出由标准输出重定向到 cgi_output 的管道写端上
        dup2(cgi_output[1], 1);
        //将子进程的输出由标准输入重定向到 cgi_input 的管道读端上
        dup2(cgi_input[0], 0);

        //关闭无用管道口
        close(cgi_output[0]);//关闭了cgi_output中的读通道
        close(cgi_input[1]);//关闭了cgi_input中的写通道

        //构造一个环境变量
        sprintf(meth_env, "REQUEST_METHOD=%s", method);
        //将这个环境变量加进子进程的运行环境中
        putenv(meth_env);

        //根据http 请求的不同方法，构造并存储不同的环境变量
        if (strcasecmp(method, "GET") == 0)
        {
            //存储QUERY_STRING
            sprintf(query_env, "QUERY_STRING=%s", query_string);
            putenv(query_env);
        }
        else
        {
            /* POST */
            //存储CONTENT_LENGTH
            sprintf(length_env, "CONTENT_LENGTH=%d", content_length);
            putenv(length_env);
        }
        //最后将子进程替换成另一个进程并执行 cgi 脚本
        execl(path, path, NULL);//执行CGI脚本
        exit(0);
    }
    else
    {
        //关闭无用管道口
        //父进程则关闭 cgi_output管道的写端和 cgi_input 管道的读端
        close(cgi_output[1]);
        close(cgi_input[0]);
        //如果是 POST 方法的话就继续读 body 的内容，并写到 cgi_input 管道里让子进程去读
        if (strcasecmp(method, "POST") == 0)
        {
            for (i = 0; i < content_length; i++)
            {
                //得到post请求数据，写到input管道中，供子进程使用
                recv(client, &c, 1, 0);
                write(cgi_input[1], &c, 1);
            }
        }

        //读取cgi脚本返回数据
        //从 cgi_output 管道中读子进程的输出，并发送到客户端去
        //从output管道读到子进程处理后的信息，然后send出去
        while (read(cgi_output[0], &c, 1) > 0)
        {
            //发送给浏览器(客户端)
            send(client, &c, 1, 0);
        }

        //运行结束关闭
        close(cgi_output[0]);
        close(cgi_input[1]);

        //等待子进程返回退出
        waitpid(pid, &status, 0);
    }
}

//得到一行数据,只要发现c为\n,就认为是一行结束，如果读到\r,再用MSG_PEEK的方式读入一个字符，如果是\n，从socket用读出
//如果是下个字符则不处理，将c置为\n，结束。如果读到的数据为0中断，或者小于0，也视为结束，c置为\n
int get_line(int sock, char *buf, int size)
{
    int i = 0;
    char c = '\0';
    int n;

    while ((i < size - 1) && (c != '\n'))
    {
        n = recv(sock, &c, 1, 0);

        if (n > 0)
        {
            if (c == '\r')
            {
                //偷窥一个字节，如果是\n就读走
                n = recv(sock, &c, 1, MSG_PEEK);
                if ((n > 0) && (c == '\n'))
                {
                    recv(sock, &c, 1, 0);
                }
                else
                {
                    //不是\n（读到下一行的字符）或者没读到，置c为\n 跳出循环,完成一行读取
                    c = '\n';
                }
            }
            buf[i] = c;
            i++;
        }
        else
        {
            c = '\n';
        }
    }
    buf[i] = '\0';
    return(i);
}

//加入http的headers
void headers(int client, const char *filename)
{
    char buf[1024];
    (void)filename;  /* could use filename to determine file type */
    //发送HTTP头
    strcpy(buf, "HTTP/1.0 200 OK\r\n");
    send(client, buf, strlen(buf), 0);
    strcpy(buf, SERVER_STRING);
    send(client, buf, strlen(buf), 0);
    sprintf(buf, "Content-Type: text/html\r\n");
    send(client, buf, strlen(buf), 0);
    strcpy(buf, "\r\n");
    send(client, buf, strlen(buf), 0);
}

//如果资源没有找到得返回给客户端下面的信息
void not_found(int client)
{
    char buf[1024];
    //返回404
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
}

//如果不是CGI文件，也就是静态文件，直接读取文件返回给请求的http客户端
void serve_file(int client, const char *filename)
{
    FILE *resource = NULL;
    int numchars = 1;
    char buf[1024];
    //默认字符
    buf[0] = 'A';
    buf[1] = '\0';
    while ((numchars > 0) && strcmp("\n", buf))
    {
        numchars = get_line(client, buf, sizeof(buf));
    }

    //打开文件
    resource = fopen(filename, "r");
    if (resource == NULL)
    {
        not_found(client);
    }
    else
    {
        //打开成功后，将这个文件的基本信息封装成 response 的头部(header)并返回
        headers(client, filename);
        //接着把这个文件的内容读出来作为 response 的 body 发送到客户端
        cat(client, resource);
    }
    fclose(resource);//关闭文件句柄
}

//启动服务端
int startup(u_short *port)
{
    int httpd = 0,option;
    struct sockaddr_in name;
    //设置http socket
    httpd = socket(PF_INET, SOCK_STREAM, 0);
    if (httpd == -1)
    {
        error_die("socket");//连接失败
    }

    socklen_t optlen;
    optlen = sizeof(option);
    option = 1;
    setsockopt(httpd, SOL_SOCKET, SO_REUSEADDR, (void *)&option, optlen);

    memset(&name, 0, sizeof(name));
    name.sin_family = AF_INET;
    name.sin_port = htons(*port);
    name.sin_addr.s_addr = htonl(INADDR_ANY);

    //如果传进去的sockaddr结构中的 sin_port 指定为0，这时系统会选择一个临时的端口号
    if (bind(httpd, (struct sockaddr *)&name, sizeof(name)) < 0)
    {
        error_die("bind");//绑定失败
    }
    if (*port == 0)  /*动态分配一个端口 */
    {
        socklen_t  namelen = sizeof(name);
        //调用getsockname()获取系统给 httpd 这个 socket 随机分配的端口号
        if (getsockname(httpd, (struct sockaddr *)&name, &namelen) == -1)
        {
            error_die("getsockname");
        }
        *port = ntohs(name.sin_port);
    }

    //最初的 BSD socket 实现中，backlog 的上限是5
    if (listen(httpd, 5) < 0)
    {
        error_die("listen");
    }
    return(httpd);
}

//如果方法没有实现，就返回此信息
void unimplemented(int client)
{
    char buf[1024];
    //发送501说明相应方法没有实现
    sprintf(buf, "HTTP/1.0 501 Method Not Implemented\r\n");
    send(client, buf, strlen(buf), 0);
    sprintf(buf, SERVER_STRING);
    send(client, buf, strlen(buf), 0);
    sprintf(buf, "Content-Type: text/html\r\n");
    send(client, buf, strlen(buf), 0);
    sprintf(buf, "\r\n");
    send(client, buf, strlen(buf), 0);
    sprintf(buf, "<HTML><HEAD><TITLE>Method Not Implemented\r\n");
    send(client, buf, strlen(buf), 0);
    sprintf(buf, "</TITLE></HEAD>\r\n");
    send(client, buf, strlen(buf), 0);
    sprintf(buf, "<BODY><P>HTTP request method not supported.\r\n");
    send(client, buf, strlen(buf), 0);
    sprintf(buf, "</BODY></HTML>\r\n");
    send(client, buf, strlen(buf), 0);
}

/**********************************************************************/

int main(void)
{
    int server_sock = -1;
    u_short port = 6379;//默认监听端口号 port 为6379
    int client_sock = -1;
    struct sockaddr_in client_name;
    socklen_t client_name_len = sizeof(client_name);

#if 0
    pthread_t newthread;
    server_sock = startup(&port);

    printf("http server_sock is %d\n", server_sock);
    printf("http running on port %d\n", port);
    while (1)
    {
        client_sock = accept(server_sock,
                             (struct sockaddr *)&client_name,
                             &client_name_len);

        printf("New connection....  ip: %s , port: %d\n",inet_ntoa(client_name.sin_addr),ntohs(client_name.sin_port));
        if (client_sock == -1)
        {
            error_die("accept");
        }

        //每次收到请求，创建一个线程来处理接受到的请求
        //把client_sock转成地址作为参数传入pthread_create
        if (pthread_create(&newthread , NULL, accept_request, (void*)&client_sock) != 0)
        {
            perror("pthread_create");
        }
    }
#else   //使用线程池+epoll
    //创建线程池
    ThreadPool pool(4);
    printf("Create threadpool success!\n");
    server_sock = startup(&port);

    //创建epooll事件
    int epfd, nfds;
    //生成用于处理accept的epoll专用的文件描述符
    epfd = epoll_create(5);
    struct epoll_event ev, events[20];
    ev.data.fd = server_sock;
    //设置要处理的事件类型
    ev.events = EPOLLIN | EPOLLET;
    //注册epoll事件
    epoll_ctl(epfd, EPOLL_CTL_ADD, server_sock, &ev);
    printf("httpd running on port: %d\n", port);

    while(true)
    {
        //等待epoll事件的发生
        nfds = epoll_wait(epfd, events, 20, 500);
        //处理所发生的所有事件
        for(int i = 0; i < nfds; i++)
        {
            //如果新检测到一个socket用户连接到了绑定的socket端口，建立新的连接
            if(events[i].data.fd == server_sock)
            {
                client_sock = accept(server_sock, (sockaddr*)&client_name, &client_name_len);
                if(client_sock < 0)
                {
                    perror("client_sock < 0!");
                    exit(1);
                }
                char* str = inet_ntoa(client_name.sin_addr);
                printf("accept a connnection from %s!\n", str);

                ev.data.fd = client_sock;
                //设置用于注册的读操作事件
                ev.events = EPOLLIN | EPOLLET;
                //注册ev
                epoll_ctl(epfd, EPOLL_CTL_ADD, client_sock, &ev);
            } else if(events[i].events & EPOLLIN)
            {
                //如果是已经连接的用户，并且收到数据，那么进行读入
                std::cout<< "start worker thread ID:" << std::this_thread::get_id() << std::endl;
                pool.enqueue(accept_request, (void*)&client_sock);
            }
        }
    }
#endif

    close(server_sock);
    return(0);
}
