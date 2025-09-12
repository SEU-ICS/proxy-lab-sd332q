#include "csapp.h"
//
#define MAX_NUM 10

/* Recommended max cache and object sizes */
#define MAX_CACHE_SIZE 1049000
#define MAX_OBJECT_SIZE 102400

#define NTHREADS 4
#define SBUFSIZE 16

/* You won't lose style points for including this long line in your code */
static const char *user_agent_hdr = "User-Agent: Mozilla/5.0 (X11; Linux x86_64; rv:10.0.3) Gecko/20120305 Firefox/10.0.3\r\n";

typedef struct {
    char url[MAXLINE];
    char object[MAX_OBJECT_SIZE];
    int LRU;
    int cnt;
    int size;
} block;

typedef struct {
    int num;
    block obj[MAX_NUM];
} Cache;

static Cache cache;
static sem_t mutex, w;
static int readcnt, timestamp;

void init_cache() {
    timestamp = 0;
    readcnt = 0;
    cache.num = 0;
    sem_init(&mutex, 0, 1);
    sem_init(&w, 0, 1);
}

int query_cache(rio_t *rio, char *url) {
    P(&mutex);
    readcnt++;
    if (readcnt == 1) {
        P(&w);
    }
    V(&mutex);

    int ishit = 0;
    for (int i = 0; i < MAX_NUM; i++) {
        if (strcmp(cache.obj[i].url, url) == 0) {
            P(&mutex);
            cache.obj[i].LRU = timestamp++;
            V(&mutex);
            rio_writen(rio->rio_fd, cache.obj[i].object, cache.obj[i].size);
            ishit = 1;
            break;
        }
    }

    P(&mutex);
    readcnt--;
    if (readcnt == 0) {
        V(&w);
    }
    V(&mutex);

    return ishit;
}

int add_cache(char *url, char *object, int cnt) {
    P(&w);

    int index = -1;
    if (cache.num < MAX_NUM) {
        index = cache.num;
        cache.num++;
    } else {
 
        int oldest = timestamp;
        for (int i = 0; i < MAX_NUM; i++) {
            if (cache.obj[i].LRU < oldest) {
                oldest = cache.obj[i].LRU;
                index = i;
            }
        }
    }

    if (index != -1) {
        strcpy(cache.obj[index].url, url);
        memcpy(cache.obj[index].object, object, cnt);
        cache.obj[index].size = cnt;
        cache.obj[index].LRU = timestamp++;
    }

    V(&w);
    return 0;
}
typedef struct {
    int *buf;          /* Buffer array */         
    int n;             /* Maximum number of slots */
    int front;         /* buf[(front+1)%n] is first item */
    int rear;          /* buf[rear%n] is last item */
    sem_t mutex;       /* Protects accesses to buf */
    sem_t slots;       /* Counts available slots */
    sem_t items;       /* Counts available items */
} sbuf_t;

void sbuf_init(sbuf_t *sp, int n)
{
    sp->buf = Calloc(n, sizeof(int)); 
    sp->n = n;                       /* Buffer holds max of n items */
    sp->front = sp->rear = 0;        /* Empty buffer iff front == rear */
    Sem_init(&sp->mutex, 0, 1);      /* Binary semaphore for locking */
    Sem_init(&sp->slots, 0, n);      /* Initially, buf has n empty slots */
    Sem_init(&sp->items, 0, 0);      /* Initially, buf has zero data items */
}

void sbuf_deinit(sbuf_t *sp)
{
    Free(sp->buf);
}

void sbuf_insert(sbuf_t *sp, int item)
{
    P(&sp->slots);                          /* Wait for available slot */
    P(&sp->mutex);                          /* Lock the buffer */
    sp->buf[(++sp->rear)%(sp->n)] = item;   /* Insert the item */
    V(&sp->mutex);                          /* Unlock the buffer */
    V(&sp->items);                          /* Announce available item */
}

int sbuf_remove(sbuf_t *sp)
{
    int item;
    P(&sp->items);                          /* Wait for available item */
    P(&sp->mutex);                          /* Lock the buffer */
    item = sp->buf[(++sp->front)%(sp->n)];  /* Remove the item */
    V(&sp->mutex);                          /* Unlock the buffer */
    V(&sp->slots);                          /* Announce available slot */
    return item;
}
void doit(int fd);
void clienterror(int fd, char *cause, char *errnum, char *shortmsg, char *longmsg);
void parse_uri(char *uri, char *host, char *path, char *port);
void build_http_request(char *request, char *host, char *path, char *method, rio_t *client_rio);
void *thread(void *vargp);

sbuf_t sbuf;

int main(int argc, char** argv)
{
    int listenfd, connfd;
    char hostname[MAXLINE], port[MAXLINE];
    socklen_t clientlen;
    struct sockaddr_storage clientaddr;
    pthread_t tid;

    if (argc != 2) {
        fprintf(stderr, "usage: %s <port>\n", argv[0]);
        exit(1);
    }

    init_cache();
    listenfd = Open_listenfd(argv[1]);
    sbuf_init(&sbuf, SBUFSIZE);

    for (int i = 0; i < NTHREADS; i++) {
        Pthread_create(&tid, NULL, thread, NULL);
    }

    while (1) {
        clientlen = sizeof(clientaddr);
        connfd = Accept(listenfd, (SA *)&clientaddr, &clientlen);
        sbuf_insert(&sbuf, connfd);
        Getnameinfo((SA *) &clientaddr, clientlen, hostname, MAXLINE, port, MAXLINE, 0);
        printf("Accepted connection from (%s, %s)\n", hostname, port);
    }

    return 0;
}

void *thread(void *vargp)
{
    Pthread_detach(pthread_self());
    while (1) {
        int connfd = sbuf_remove(&sbuf);
        doit(connfd);
        Close(connfd);
    }
    return NULL;
}

void doit(int fd)
{
    char buf[MAXLINE], method[MAXLINE], uri[MAXLINE], version[MAXLINE];
    char hostname[MAXLINE], path[MAXLINE], port[MAXLINE];
    rio_t ClientRio, ServerRio;

    Rio_readinitb(&ClientRio, fd);
    if (!Rio_readlineb(&ClientRio, buf, MAXLINE))
        return;
    sscanf(buf, "%s %s %s", method, uri, version);

    if (strcasecmp(method, "GET")) {
        clienterror(fd, method, "501", "Not Implemented", "Proxy does not implement this method");
        return;
    }

    parse_uri(uri, hostname, path, port);

    char cache_key[MAXLINE];
    snprintf(cache_key, sizeof(cache_key), "%s:%s%s", hostname, port, path);

    if (query_cache(&ClientRio, cache_key)) {
        return;
    }

    int serverfd = Open_clientfd(hostname, port);
    if (serverfd < 0) {
        printf("connection failed\n");
        return;
    }

    Rio_readinitb(&ServerRio, serverfd);
    char request[MAXLINE];
    build_http_request(request, hostname, path, method, &ClientRio);
    Rio_writen(serverfd, request, strlen(request));

    char cache_buf[MAX_OBJECT_SIZE];
    ssize_t n;
    size_t total_size = 0;
    while ((n = Rio_readlineb(&ServerRio, buf, MAXLINE)) > 0) {
        Rio_writen(fd, buf, n);
        if (total_size + n < MAX_OBJECT_SIZE) {
            memcpy(cache_buf + total_size, buf, n);
            total_size += n;
        }
    }
    Close(serverfd);

    if (total_size > 0 && total_size < MAX_OBJECT_SIZE) {
        add_cache(cache_key, cache_buf, total_size);
    }
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

void parse_uri(char *uri, char *host, char *path, char *port)
{
    char *hostpose = strstr(uri, "//");
    if (hostpose == NULL)
    {
        char *pathpose = strstr(uri, "/");
        if (pathpose != NULL)
            strcpy(path, pathpose);
        strcpy(port, "80");
        return;
    }
    else
    {
        char *portpose = strstr(hostpose + 2, ":");
        if (portpose != NULL)
        {
            int tmp;
            sscanf(portpose + 1, "%d%s", &tmp, path);
            sprintf(port, "%d", tmp);
            *portpose = '\0';
        }
        else
        {
            char *pathpose = strstr(hostpose + 2, "/");
            if (pathpose != NULL)
            {
                strcpy(path, pathpose);
                strcpy(port, "80");
                *pathpose = '\0';
            }
        }
        strcpy(host, hostpose + 2);
    }
    return;
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
