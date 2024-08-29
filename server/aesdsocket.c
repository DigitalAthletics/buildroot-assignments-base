#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <syslog.h>
#include <errno.h>
#include <string.h>
#include <stdio.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <stdlib.h>
#include <signal.h>

#define OUT_FILE "/var/tmp/aesdsocketdata"

int run = 1;
int working = 0;
int sockfd = -1;

void handle_signal(int signal) 
{
    syslog(LOG_DEBUG, "Caught signal. exiting");
    if (working) run = 0;
    else 
    {
        if(sockfd != -1) close(sockfd);
        remove(OUT_FILE);
        closelog();
        exit(0);
    };
}

void find_and_kill_process_using_port(int port) 
{
    char command[256];
    snprintf(command, sizeof(command), "lsof -t -i :%d", port);
    FILE *fp = popen(command, "r");
    if (fp == NULL) 
    {
        perror("popen");
        exit(EXIT_FAILURE);
    }

    char pid_str[10];
    if (fgets(pid_str, sizeof(pid_str), fp) != NULL) 
    {
        int pid = atoi(pid_str);
        printf("Process using port %d: PID %d\n", port, pid);
        pclose(fp);

        // Kill the process
        if (kill(pid, SIGKILL) == -1) 
        {
            perror("kill");
            exit(EXIT_FAILURE);
        } 
        else 
        {
            printf("Process %d killed successfully\n", pid);
        }
    } 
    else 
    {
        printf("No process found using port %d\n", port);
        pclose(fp);
    }
}

int main(int argc, char** argv) 
{
    openlog("aesdsocket", 0, LOG_USER);
    char server_ip[INET_ADDRSTRLEN];
    char client_ip[INET_ADDRSTRLEN];
    struct addrinfo hints, *res;
    int deamon = 0;
    uint8_t buffer[BUFSIZ];
    int opt = 1;
    //char a[99];
    static int fd;
    
    remove(OUT_FILE);
    signal(SIGINT, handle_signal);
    signal(SIGTERM, handle_signal);

    if (argc > 1 && !strcmp(argv[1], "-d")) 
    { 
        deamon = 1;
    };
    
    //find_and_kill_process_using_port(9000);

    // Opens a stream socket bound to port 9000, failing and returning -1 if any of the socket connection steps fail.
    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_INET; // IPv4
    hints.ai_socktype = SOCK_STREAM;
    //hints.ai_flags = AI_PASSIVE;     // fill in my IP for me
    //Get information about a host name and/or service and load up a struct sockaddr with the result.
    getaddrinfo("127.0.0.1", "9000", &hints, &res);
    
    // Allocate a socket descriptor
    sockfd = socket(res->ai_family, res->ai_socktype, res->ai_protocol); 
    if (sockfd == -1) 
    {
        freeaddrinfo(res);
        printf("Failed create socket: %s\n", strerror(errno));
        return -1;
    }
    
    inet_ntop(AF_INET, &(res->ai_addr), server_ip, INET_ADDRSTRLEN);
    
    // Set various options for a socket
    if (setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) 
    {
        syslog(LOG_ERR, "Failed to set SO_REUSEADDR: %s", strerror(errno));
        freeaddrinfo(res);
        close(sockfd);
        return -1;
    }

    // bind socket to port 9000
    if(bind(sockfd, res->ai_addr, res->ai_addrlen)) 
    {
        syslog(LOG_ERR, "Failed to bind socket to port 9000: %s", strerror(errno));
        printf("Failed to bind socket to port 9000: %s\n", strerror(errno));
        freeaddrinfo(res);
        close(sockfd);
        return -1;
    }

    //free up the memory
    freeaddrinfo(res);

    if(deamon) 
    {
        printf("running as deamon...\n");
        if(fork()) exit(0);
    }

    while (run) 
    { 
        //Tell a socket to listen for incoming connections
        if (listen(sockfd, 10)) 
        {
            syslog(LOG_ERR, "Failed to listen on %s:9000 : %s", server_ip, strerror(errno));
            return -1;
        }
        
        struct sockaddr client;
        socklen_t size = sizeof(client);
        working=0;
        //Accept an incoming connection on a listening socket
        fd = accept(sockfd, (struct sockaddr *)&client, &size);
        working=1;

        if (fd == -1) 
        {
            syslog(LOG_ERR, "Failed to connect to client: %s", strerror(errno));
            return -1;
        } 
        else 
        {
            //Convert IP addresses to human-readable form and back.
            inet_ntop(AF_INET, &client, client_ip, INET_ADDRSTRLEN);
            syslog(LOG_DEBUG, "Accepted connection from %s", client_ip);
        }
        
        // read from client untill a new line is received
        FILE *outf = fopen(OUT_FILE, "ab");
        while (1)
        {
            //Receive data on a socket
            ssize_t bytes = recv(fd, buffer, BUFSIZ, 0);
            if( bytes <= 0) 
            { 
                exit(-1);
            } 
            else 
            {
                int nl_found = 0;
                for (int i = 0; i< bytes; i++) 
                {
                    if (buffer[i] == '\n') 
                    { 
                    	bytes = i+1; 
                    	nl_found = 1; 
                    	break; 
                    }
                }
                
                // write all received data or untill the new line character.
                fwrite(buffer, 1, bytes, outf);
                fflush(outf);
                if(nl_found) break;
            }
            
        }
        fclose(outf);

        // write OUT_FILE contents back to client
        outf = fopen(OUT_FILE, "rb");
        while (!feof(outf))
        {
            size_t bytes = fread(buffer, 1, BUFSIZ, outf);
            //Send data out over a socket 
            //memcpy(&a[0], buffer, bytes);
            //syslog(LOG_DEBUG, "Data: %s \n", a);
            ssize_t s = send(fd, (void*) buffer, bytes, 0);
            if (s == -1)
            {
            	syslog(LOG_ERR, "Failed to send: %ld", s);            
            }
        }
        fclose(outf);
        close(fd); // close connection
        syslog(LOG_DEBUG, "Closed connection from %s", client_ip);
    }

    close(sockfd);
    remove(OUT_FILE);
    closelog();
    return 0;
}
