#include <sys/socket.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <netdb.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <arpa/inet.h> 


char **strsplit(const char* str, const char* delim, size_t* numtokens) 
{
    size_t tokens_alloc = 1;
    size_t tokens_used = 0;
    char **tokens = calloc(tokens_alloc, sizeof(char*));
    char *token, *strtok_ctx;
    char *s = strdup(str);

    for (token = strtok_r(s, delim, &strtok_ctx);
            token != NULL;
            token = strtok_r(NULL, delim, &strtok_ctx))
    {
        // check if we need to allocate more space for tokens
        if (tokens_used == tokens_alloc) 
	{
            tokens_alloc *= 2;
            tokens = realloc(tokens, tokens_alloc * sizeof(char*));
        }
        tokens[tokens_used++] = strdup(token);
    }
    // cleanup
    if (tokens_used == 0) 
    {
        free(tokens);
        tokens = NULL;
    } 
    else 
    {
        tokens = realloc(tokens, tokens_used * sizeof(char*));
    }
    *numtokens = tokens_used;
    free(s);
    return tokens;
}


int main(int argc, char *argv[])
{
    int sockfd = 0, n = 0;
    int resp;
    char recvBuff[1024];
    int size;
    char str[100];
    int i;
    char **tokens;
    size_t numtokens;
    size_t j;
    struct sockaddr_in serv_addr; 

    if(argc != 2)
    {
        printf("\n Usage: %s <ip of server> \n",argv[0]);
        return 1;
    } 

    memset(recvBuff, '0',sizeof(recvBuff));
    if((sockfd = socket(AF_INET, SOCK_STREAM, 0)) < 0)
    {
        printf("\n Error : Could not create socket \n");
        return 1;
    } 

    memset(&serv_addr, '0', sizeof(serv_addr)); 

    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(5000); 

    if(inet_pton(AF_INET, argv[1], &serv_addr.sin_addr)<=0)
    {
        printf("\n inet_pton error occured\n");
        return 1;
    } 

    if( connect(sockfd, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0)
    {
       printf("\n Error : Connect Failed \n");
       return 1;
    } 
    
    
    size=sprintf(str,"setTrigSrc:RP_TRIG_SRC_CHA_NE");
    write(sockfd , str , size);
    
    while ( (n = read(sockfd, recvBuff, sizeof(recvBuff)-1)) > 0)
    {
	
        recvBuff[n] = '\0';
        if(i==10)
	{
	   printf("FPGA temp: %s C\n",recvBuff); 
	}
	else
	{
	    tokens=strsplit(recvBuff,";",&numtokens);
	    printf("Counter: %s Trigs: %s Charge for %ds: %s\n",tokens[0],tokens[1],i,tokens[2]);
	    for (j=0; j < numtokens; j++) 
	    {
		free(tokens[j]);
	    }
	    if (tokens != NULL)
	    {
		free(tokens);
	    }
	
	}

	i++;

	if (i>10)
	{
	    printf("\n-----------------------\n");
	    i=0;
	    printf("\033[2J\033[1;1H"); // Clear screen");
	    usleep(10000);
	}
	if(i<10)
	{
	    size=sprintf(str,"getCharge:%d",i);
	    write(sockfd , str , size);
	}
	else
	{
	    size=sprintf(str,"getFPGATemp");
	    write(sockfd , str , size);
	}

    } 


    return 0;
}
