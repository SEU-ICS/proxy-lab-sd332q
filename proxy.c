#include <stdio.h>
#include "csapp.h"
/* Recommended max cache and object sizes */
#define MAX_CACHE_SIZE 1049000
#define MAX_OBJECT_SIZE 102400

/* You won't lose style points for including this long line in your code */
static const char *user_agent_hdr = "User-Agent: Mozilla/5.0 (X11; Linux x86_64; rv:10.0.3) Gecko/20120305 Firefox/10.0.3\r\n";

void doit(int fd);
void clienterror(int fd, char *cause, char *errnum, char *shortmsg, char *longmsg);
void parse_uri(char *uri, char *host, char *path, char *port);
void build_http_request(char *request, char *host, char *path, char *method, rio_t *client_rio);


int main(int argc,char** argv)
{
     
   int listenfd,connfd;
   char hostname[MAXLINE],port[MAXLINE];
   socklen_t clientlen;
   struct sockaddr_storage clientaddr;

   if (argc != 2)
   {
	fprintf(stderr, "usage: %s <port>\n", argv[0]);
	exit(1);
   }

   listenfd= Open_listenfd(argv[1]);
    while (1) {
	clientlen = sizeof(clientaddr);
	connfd = Accept(listenfd, (SA *)&clientaddr, &clientlen); //line:netp:tiny:accept
        Getnameinfo((SA *) &clientaddr, clientlen, hostname, MAXLINE, port, MAXLINE, 0);
        printf("Accepted connection from (%s, %s)\n", hostname, port);
	doit(connfd);                                             //line:netp:tiny:doit
	Close(connfd);  
   }
}
 
void doit(int fd)
{
    char buf[MAXLINE],method[MAXLINE],uri[MAXLINE],version[MAXLINE];
    char hostname[MAXLINE],path[MAXLINE],port[MAXLINE];
    rio_t ClientRio,ServerRio;

    Rio_readinitb(&ClientRio, fd);
    if (!Rio_readlineb(&ClientRio, buf, MAXLINE))  	 
        return;
    sscanf(buf,"%s %s %s",method,uri,version);

    if(strcasecmp(method,"GET"))
    {
        clienterror(fd,method,"501","NOT implemented","Tiny does not implement this method");
        return;
    }
    
      if (parse_uri(uri, hostname, path, port) < 0)
    {
        clienterror(fd, uri, "400", "Bad Request", "Proxy could not parse the URI");
        return;
    }

    int serverfd = Open_clientfd(hostname,port);
    if(serverfd < 0)
    {
        printf("connection failed\n");
        return;
    }

    Rio_readinitb(&ServerRio, serverfd);

    char request[MAXLINE * 2];
    build_http_request(request, hostname, path, method, &ClientRio);

    Rio_writen(serverfd, request, strlen(request));

    int char_nums;
    while((char_nums = Rio_readlineb(&serverfd, buf, MAXLINE)))
        Rio_writen(fd, buf, char_nums);

    Close(serverfd);
    
    
}
void clienterror(int fd, char *cause, char *errnum, 
		 char *shortmsg, char *longmsg) 
{
    char buf[MAXLINE], body[MAXBUF];

    /* Build the HTTP response body */
    sprintf(body, "<html><title>Tiny Error</title>");
    sprintf(body, "%s<body bgcolor=""ffffff"">\r\n", body);
    sprintf(body, "%s%s: %s\r\n", body, errnum, shortmsg);
    sprintf(body, "%s<p>%s: %s\r\n", body, longmsg, cause);
    sprintf(body, "%s<hr><em>The Tiny Web server</em>\r\n", body);

    /* Print the HTTP response */
    sprintf(buf, "HTTP/1.0 %s %s\r\n", errnum, shortmsg);
    Rio_writen(fd, buf, strlen(buf));
    sprintf(buf, "Content-type: text/html\r\n");
    Rio_writen(fd, buf, strlen(buf));
    sprintf(buf, "Content-length: %d\r\n\r\n", (int)strlen(body));
    Rio_writen(fd, buf, strlen(buf));
    Rio_writen(fd, body, strlen(body));
}
int parse_uri(char *uri, char *host, char *path, char *port)
{
   
    strcpy(path, "/");
    strcpy(port, "80");
    
    char *protocol = strstr(uri, "://");
    if (protocol == NULL)
    {
        strcpy(path, uri);
    }
    
    char *host_start = protocol + 3;
    char *host_end = strchr(host_start, '/');
    
    if (host_end != NULL)
    {
        strcpy(path, host_end);
        *host_end = '\0'; 
    }
    char *colon = strchr(host_start, ':');
    if (colon != NULL)
    {
        *colon = '\0'; 
        strcpy(host, host_start);
        strcpy(port, colon + 1);
    }
    else
    {
        strcpy(host, host_start);
    }
    if (host_end != NULL) *host_end = '/';
    if (colon != NULL) *colon = ':';
    
    return 0;
}

void build_http_request(char *request, char *host, char *path, char *method, rio_t *client)
{
    char buf[MAXLINE];
    
    sprintf(request, "%s %s HTTP/1.0\r\n", method, path);
    
    sprintf(request + strlen(request), "Host: %s\r\n", host);
    sprintf(request + strlen(request), "%s", user_agent_hdr);
    sprintf(request + strlen(request), "Connection: close\r\n");
    sprintf(request + strlen(request), "Proxy-Connection: close\r\n");
    
    while (Rio_readlineb(client, buf, MAXLINE) > 0)
    {
        if (!strcmp(buf, "\r\n"))
            break;
            
        if (strncasecmp(buf, "Host", 4) == 0 ||
            strncasecmp(buf, "Connection", 10) == 0 ||
            strncasecmp(buf, "Proxy-Connection", 16) == 0 ||
            strncasecmp(buf, "User-Agent", 10) == 0)
        {
            continue;
        }
        
        sprintf(request + strlen(request), "%s", buf);
    }
    
    sprintf(request + strlen(request), "\r\n");
}
