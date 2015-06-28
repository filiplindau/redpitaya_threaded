// network.c
// Robert Lindvall 2015-05-28

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <pthread.h>
#include "rp.h"

  
int listenfd = 0, connfd = 0;
struct sockaddr_in serv_addr; 
char sendBuff[1000];
char client_message[2000];
char *sString;

extern int triggered;
extern int free_counter;
extern int count_table[10];
extern float charge[10];
extern pthread_mutex_t mutex1;
extern int new_data;
extern float trig_level;
extern int32_t trig_delay;
extern int record_length;
extern float fpga_temp;
extern int16_t* buff_ch1_raw;
extern float* buff_ch1;
extern float* buff_ch2;

extern float max_adc_v_ch1;
extern int dc_offset_ch1;
extern float max_adc_v_ch2;
extern int dc_offset_ch2;

extern rp_acq_trig_src_t trig_source;


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

int SetupSocket_Server()
{
    // Setup socket server
    listenfd = socket(AF_INET, SOCK_STREAM, 0);
    if (listenfd == -1)
    {
	printf("Could not create socket");
	return -1;
    }
    memset(&serv_addr, '0', sizeof(serv_addr));
    memset(sendBuff, '0', sizeof(sendBuff)); 

    serv_addr.sin_family = AF_INET;
    serv_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    serv_addr.sin_port = htons(8888); 

    if( bind(listenfd,(struct sockaddr *)&serv_addr , sizeof(serv_addr)) < 0)
    {
	//print the error message
	perror("bind failed. Error\n");
	return -1;
    }
    listen(listenfd, 10); 
    //Accept and incoming connection
    printf("Waiting for incoming connections...\n");
    return 1;
}

int CloseSocket_Server()
{
    printf("Closing socket");
    close(listenfd);
    return 1;
}

int Handle_Incoming_Connections()
{
    connfd = accept(listenfd, (struct sockaddr*)NULL, NULL); 
    if (connfd < 0)
    {
	perror("accept failed\n");
	return -1;
    }
    printf("Connection accepted\n");
    return 1;
}

//int Process_Incoming_Commands(float *charge, int triggered, int counter, int *no_trigs)
void *Process_Incoming_Commands(void *arg)
{
    char str[30];
    int size;
    char* command;
    char* cmdData;
    int channel;
    int arg1;
    float arg_f;
    int errCode;
    rp_acq_decimation_t decimation;
    
    //Receive a message from client
    while ((size=recv(connfd , client_message , 2000 , 0))>0)
    {
	client_message[size] = '\0';
	cmdData = strtok(client_message, ":");
	if (cmdData != NULL)
	{
	    command = cmdData;
	    cmdData = strtok(NULL, ":");
	    //printf("%s %s",command,cmdData);
	    //fflush(stdout);
	}
	
	if (strcmp(command,"getCharge")==0)
	{

	    arg1 = atoi(cmdData);
	    if ((arg1>=0) && (arg1<=9))
	    {	
		if (triggered==1)
		{
			size=sprintf(str,"%d;%d;%f",free_counter,count_table[arg1],charge[arg1]);
			write(connfd,str,size);
		}
		else
		{
		    write(connfd, "not triggered" , 13);
		    fflush(stdout);
		}
	    }
		
	}
	else if (strcmp(command,"getTriggerLevel")==0)
	{
	    size=sprintf(str,"%f",trig_level);
	    write(connfd,str,size);
	}  
	else if (strcmp(command,"setTriggerLevel")==0)
	{
	    arg_f = atof(cmdData);
	    rp_AcqSetTriggerLevel(arg_f);
	    size=sprintf(str,"OK");
	    write(connfd,str,size);
	    printf("New trigger level: %.01f V\n", arg_f);
	}
	else if (strcmp(command,"setRecordLength")==0)
	{
	    arg1 = atoi(cmdData);
	    pthread_mutex_lock( &mutex1 );
	    record_length = arg1;
	    size=sprintf(str,"OK");
	    pthread_mutex_unlock( &mutex1 );
	    write(connfd,str,size);
	    printf("New record length: %7d\n", arg1);
	}
	else if (strcmp(command,"setDecimation")==0)
	{
	    arg1 = atoi(cmdData);
	    if (arg1 == 0) 
	    {
		decimation = RP_DEC_1;
	    }
	    else if (arg1 == 1) 
	    {
		decimation = RP_DEC_8;
	    }
	    else if (arg1 == 2) 
	    {
		decimation = RP_DEC_64;
	    }
	    else if (arg1 == 3) 
	    {
		decimation = RP_DEC_1024;
	    }
	    else if (arg1 == 4) 
	    {
		decimation = RP_DEC_8192;
	    }
	    else if (arg1 == 5) 
	    {
		decimation = RP_DEC_65536;
	    }
	    else
	    {
		decimation = RP_DEC_1;
	    }
	    pthread_mutex_lock( &mutex1 );
	    rp_AcqSetDecimation(decimation);
	    pthread_mutex_unlock( &mutex1 );
	    size=sprintf(str,"OK");
	    write(connfd,str,size);
	    printf("New decimation: %7d\n", arg1);
	}
	else if (strcmp(command,"setTriggerDelaySamples")==0)
	{
	    arg1 = atoi(cmdData);
	    pthread_mutex_lock( &mutex1 );
	    trig_delay = arg1;
//	    errCode = rp_AcqSetTriggerDelay(arg1);
	    pthread_mutex_unlock( &mutex1 );
	    size=sprintf(str,"OK");
	    write(connfd,str,size);
	    printf("New trigger delay: %7d samples, return code %7d\n", arg1, errCode);
	}
	else if (strcmp(command,"getFPGATemp")==0)
	{
	    size=sprintf(str,"%.01f",fpga_temp);
	    write(connfd,str,size);
	}
	else if (strcmp(command,"setTriggerSource")==0)
	{
	    if (strcmp(cmdData,"RP_TRIG_SRC_CHA_PE")==0)
		{
		    size=sprintf(str,"OK");
		    write(connfd,str,size);
		    trig_source=RP_TRIG_SRC_CHA_PE;
		    printf("TrigSrc %s\n",cmdData);
		}
		else if (strcmp(cmdData,"RP_TRIG_SRC_CHA_NE")==0)
		{
		    size=sprintf(str,"OK");
		    write(connfd,str,size);
		    trig_source=RP_TRIG_SRC_CHA_NE;
		    printf("TrigSrc %s\n",cmdData);
		}
		else if (strcmp(cmdData,"RP_TRIG_SRC_CHB_PE")==0)
		{
		    size=sprintf(str,"OK");
		    write(connfd,str,size);
		    trig_source=RP_TRIG_SRC_CHB_PE;
		    printf("TrigSrc %s\n",cmdData);
		}
		else if (strcmp(cmdData,"RP_TRIG_SRC_CHB_NE")==0)
		{
		    size=sprintf(str,"OK");
		    write(connfd,str,size);
		    trig_source=RP_TRIG_SRC_CHB_NE;
		    printf("TrigSrc %s\n",cmdData);
		}		
		else if (strcmp(cmdData,"RP_TRIG_SRC_EXT_PE")==0)
		{
		    size=sprintf(str,"OK");
		    write(connfd,str,size);
		    trig_source=RP_TRIG_SRC_EXT_PE;
		    printf("TrigSrc %s\n",cmdData);
		}
		else if (strcmp(cmdData,"RP_TRIG_SRC_EXT_NE")==0)
		{
		    size=sprintf(str,"OK");
		    write(connfd,str,size);
		    trig_source=RP_TRIG_SRC_EXT_NE;
		    printf("TrigSrc %s\n",cmdData);
		}
				
		else 
		{
		    size=sprintf(str,"Argument Syntax Error");
		    write(connfd,str,size);
		}

	}
	else if (strcmp(command,"getCalibrationMaxADC")==0)
	{
		if (strcmp(cmdData, "0") == 0)
		{
			// Channel 1:
			printf("Calibration max ADC %7f\n", max_adc_v_ch1);
			sString = (char *) &max_adc_v_ch1;
			write(connfd , sString , sizeof(float));
		}
		else
		{		
			// Channel 2:
			printf("Calibration max ADC %7f\n", max_adc_v_ch2);
			sString = (char *) &max_adc_v_ch2;
			write(connfd , sString , sizeof(float));
		}

	}
	else if (strcmp(command,"getCalibrationOffset")==0)
	{
		if (strcmp(cmdData, "0") == 0)
		{
			// Channel 1:
				printf("Calibration offset %7d\n", dc_offset_ch1);
				sString = (char *) &dc_offset_ch1;
				write(connfd , sString , sizeof(int));
		}
		else
		{		
			// Channel 2:
				printf("Calibration offset %7d\n", dc_offset_ch2);
				sString = (char *) &dc_offset_ch2;
				write(connfd , sString , sizeof(int));
		}

	}

	else if (strcmp(command,"getWaveform")==0)
	{
		channel = atoi(cmdData);
//		printf("getWaveform %7d\n", channel);
		switch(channel){
			case 0:
				if (new_data == 1) {
					new_data = 0;
					pthread_mutex_lock( &mutex1 );
					sString = (char *) buff_ch1_raw;
//					printf("Writing channel 1, size %7d", size);
					// Need to send record_length+1 words because the first word is the header
					// containing the number of words
					write(connfd, sString , sizeof(int16_t)*(record_length+1));
					pthread_mutex_unlock( &mutex1 );
//					printf("...done");
					break;
				}
				else {
					write(connfd, "not triggered" , 13);
					break;
				}
			case 1:
				write(connfd, "not triggered" , 13);
				break;
		}

	    // Todo, stop ct, return measurements
	    // size=sprintf(str,"%.01f",fpga_temp);
	    // write(connfd,str,size);
	}
	else if (strcmp(command,"getWaveformFloat")==0)
	{
		channel = atoi(cmdData);
//		printf("getWaveform %7d\n", channel);
		switch(channel){
			case 0:
				if (new_data == 1) {
					new_data = 0;
					pthread_mutex_lock( &mutex1 );
					size = record_length + 2;
					sString = (char *) buff_ch1;
//					printf("Writing channel 1, size %7d", size);
					// Need to send record_length+1 words because the first word is the header
					// containing the number of words
					write(connfd, sString , sizeof(float)*(record_length+2));
					pthread_mutex_unlock( &mutex1 );
//					printf("...done\n");
					break;
				}
				else {
					write(connfd, "not triggered" , 13);
					break;
				}
			case 1:
				pthread_mutex_lock( &mutex1 );
				sString = (char *) buff_ch2;
				size = record_length + 2;
//				printf("Writing channel 2, size %7d", size);
				// Need to send record_length+1 words because the first word is the header
				// containing the number of words
				write(connfd, sString , sizeof(float)*(record_length+2));
				pthread_mutex_unlock( &mutex1 );
//				printf("...done\n");
				break;
		}

	    // Todo, stop ct, return measurements
	    // size=sprintf(str,"%.01f",fpga_temp);
	    // write(connfd,str,size);
	}
	else if (strcmp(command,"genOutEnable")==0)
	{
		channel = atoi(cmdData);
		if (channel == 1)
		{
			errCode = rp_GenOutEnable(RP_CH_1);
		}
		else
		{
			errCode = rp_GenOutEnable(RP_CH_2);
		}
		if (errCode != RP_OK)
		{
			printf("Error genOutEnable: %7d\n", errCode);
			size=sprintf(str,"Error %d", errCode);
		    write(connfd,str,size);
		}
		else
		{
			printf("genOutEnable");
			size=sprintf(str,"OK");
		    write(connfd,str,size);
		}
	}	
	else if (strcmp(command,"genOutDisable")==0)
	{
		channel = atoi(cmdData);
		if (channel == 1)
		{
			errCode = rp_GenOutDisable(RP_CH_1);
		}
		else
		{
			errCode = rp_GenOutDisable(RP_CH_2);
		}
		if (errCode != RP_OK)
		{
			printf("Error genOutDisable: %7d\n", errCode);
			size=sprintf(str,"Error %d", errCode);
		    write(connfd,str,size);
		}
		else
		{
			printf("genOutDisable");
			size=sprintf(str,"OK");
		    write(connfd,str,size);
		}
	}	
	else if (strcmp(command,"genFreqCh1")==0)
	{
		arg_f = atof(cmdData);
		errCode = rp_GenFreq(RP_CH_1, arg_f);
		if (errCode != RP_OK)
		{
			printf("Error genFreq channel 1: %7d\n", errCode);
			size=sprintf(str,"Error %d", errCode);
		    write(connfd,str,size);
		}
		else
		{
			printf("genFreqCh1");
			size=sprintf(str,"OK");
		    write(connfd,str,size);
		}
	}	
	else if (strcmp(command,"genFreqCh2")==0)
	{
		arg_f = atof(cmdData);
		errCode = rp_GenFreq(RP_CH_2, arg_f);
		if (errCode != RP_OK)
		{
			printf("Error genFreq channel 2: %7d\n", errCode);
			size=sprintf(str,"Error %d", errCode);
		    write(connfd,str,size);
		}
		else
		{
			printf("genFreqCh2");
			size=sprintf(str,"OK");
		    write(connfd,str,size);
		}
	}
	else if (strcmp(command,"genModeCh1")==0)
	{
		if (strcmp(cmdData,"RP_GEN_MODE_BURST")==0)
		{
			errCode = rp_GenMode(RP_CH_1, RP_GEN_MODE_BURST);
		}
		else if (strcmp(cmdData,"RP_GEN_MODE_STREAM")==0)
		{
			errCode = rp_GenMode(RP_CH_1, RP_GEN_MODE_STREAM);
		}
		else 
		{
			errCode = rp_GenMode(RP_CH_1, RP_GEN_MODE_CONTINUOUS);
		}
		if (errCode != RP_OK)
		{
			printf("Error genMode channel 1: %7d\n", errCode);
			size=sprintf(str,"Error %d", errCode);
		    write(connfd,str,size);
		}
		else
		{
			printf("genModeCh1");
			size=sprintf(str,"OK");
		    write(connfd,str,size);
		}
	}
	else if (strcmp(command,"genModeCh2")==0)
	{
		if (strcmp(cmdData,"RP_GEN_MODE_BURST")==0)
		{
			errCode = rp_GenMode(RP_CH_2, RP_GEN_MODE_BURST);
		}
		else if (strcmp(cmdData,"RP_GEN_MODE_STREAM")==0)
		{
			errCode = rp_GenMode(RP_CH_2, RP_GEN_MODE_STREAM);
		}
		else 
		{
			errCode = rp_GenMode(RP_CH_2, RP_GEN_MODE_CONTINUOUS);
		}
		if (errCode != RP_OK)
		{
			printf("Error genMode channel 2: %7d\n", errCode);
			size=sprintf(str,"Error %d", errCode);
		    write(connfd,str,size);
		}
		else
		{
			printf("genModeCh2");
			size=sprintf(str,"OK");
		    write(connfd,str,size);
		}
	}
	else if (strcmp(command,"genWaveformCh1")==0)
	{
		if (strcmp(cmdData,"RP_WAVEFORM_SINE")==0)
		{
			errCode = rp_GenWaveform(RP_CH_1, RP_WAVEFORM_SINE);
		}
		else if (strcmp(cmdData,"RP_WAVEFORM_SQUARE")==0)
		{
			errCode = rp_GenWaveform(RP_CH_1, RP_WAVEFORM_SQUARE);
		}
		else if (strcmp(cmdData,"RP_WAVEFORM_TRIANGLE")==0)
		{
			errCode = rp_GenWaveform(RP_CH_1, RP_WAVEFORM_TRIANGLE);
		}
		else if (strcmp(cmdData,"RP_WAVEFORM_RAMP_UP")==0)
		{
			errCode = rp_GenWaveform(RP_CH_1, RP_WAVEFORM_RAMP_UP);
		}
		else if (strcmp(cmdData,"RP_WAVEFORM_RAMP_DOWN")==0)
		{
			errCode = rp_GenWaveform(RP_CH_1, RP_WAVEFORM_RAMP_DOWN);
		}
		else if (strcmp(cmdData,"RP_WAVEFORM_DC")==0)
		{
			errCode = rp_GenWaveform(RP_CH_1, RP_WAVEFORM_DC);
		}
		else if (strcmp(cmdData,"RP_WAVEFORM_PWM")==0)
		{
			errCode = rp_GenWaveform(RP_CH_1, RP_WAVEFORM_PWM);
		}
		else if (strcmp(cmdData,"RP_WAVEFORM_ARBITRARY")==0)
		{
			errCode = rp_GenWaveform(RP_CH_1, RP_WAVEFORM_ARBITRARY);
		}
		if (errCode != RP_OK)
		{
			printf("Error genWaveformCh1 channel 1: %7d\n", errCode);
			size=sprintf(str,"Error %d", errCode);
		    write(connfd,str,size);
		}
		else
		{
			printf("genWaveformCh1");
			size=sprintf(str,"OK");
		    write(connfd,str,size);
		}
	}
	else if (strcmp(command,"genWaveformCh2")==0)
	{
		if (strcmp(cmdData,"RP_WAVEFORM_SINE")==0)
		{
			errCode = rp_GenWaveform(RP_CH_2, RP_WAVEFORM_SINE);
		}
		else if (strcmp(cmdData,"RP_WAVEFORM_SQUARE")==0)
		{
			errCode = rp_GenWaveform(RP_CH_2, RP_WAVEFORM_SQUARE);
		}
		else if (strcmp(cmdData,"RP_WAVEFORM_TRIANGLE")==0)
		{
			errCode = rp_GenWaveform(RP_CH_2, RP_WAVEFORM_TRIANGLE);
		}
		else if (strcmp(cmdData,"RP_WAVEFORM_RAMP_UP")==0)
		{
			errCode = rp_GenWaveform(RP_CH_2, RP_WAVEFORM_RAMP_UP);
		}
		else if (strcmp(cmdData,"RP_WAVEFORM_RAMP_DOWN")==0)
		{
			errCode = rp_GenWaveform(RP_CH_2, RP_WAVEFORM_RAMP_DOWN);
		}
		else if (strcmp(cmdData,"RP_WAVEFORM_DC")==0)
		{
			errCode = rp_GenWaveform(RP_CH_2, RP_WAVEFORM_DC);
		}
		else if (strcmp(cmdData,"RP_WAVEFORM_PWM")==0)
		{
			errCode = rp_GenWaveform(RP_CH_2, RP_WAVEFORM_PWM);
		}
		else if (strcmp(cmdData,"RP_WAVEFORM_ARBITRARY")==0)
		{
			errCode = rp_GenWaveform(RP_CH_2, RP_WAVEFORM_ARBITRARY);
		}
		if (errCode != RP_OK)
		{
			printf("Error genWaveformCh1 channel 2: %7d\n", errCode);
			size=sprintf(str,"Error %d", errCode);
		    write(connfd,str,size);
		}
		else
		{
			printf("genWaveformCh2");
			size=sprintf(str,"OK");
		    write(connfd,str,size);
		}
	}
	else if (strcmp(command,"genAmpCh1")==0)
	{
		arg_f = atof(cmdData);
		errCode = rp_GenAmp(RP_CH_1, arg_f);
		if (errCode != RP_OK)
		{
			printf("Error genAmp channel 1: %7d\n", errCode);
			size=sprintf(str,"Error %d", errCode);
		    write(connfd,str,size);
		}
		else
		{
			printf("genAmpCh1");
			size=sprintf(str,"OK");
		    write(connfd,str,size);
		}
	}	
	else if (strcmp(command,"genAmpCh2")==0)
	{
		arg_f = atof(cmdData);
		errCode = rp_GenAmp(RP_CH_2, arg_f);
		if (errCode != RP_OK)
		{
			printf("Error genAmp channel 2: %7d\n", errCode);
			size=sprintf(str,"Error %d", errCode);
		    write(connfd,str,size);
		}
		else
		{
			printf("genAmpCh2");
			size=sprintf(str,"OK");
		    write(connfd,str,size);
		}
	}	
	else if (strcmp(command,"genTrigger")==0)
	{
		if (strcmp(cmdData,"RP_CH_1")==0)
		{
			errCode = rp_GenTrigger(1);
		}
		else if (strcmp(cmdData,"RP_CH_2")==0)
		{
			errCode = rp_GenTrigger(2);
		}
		else
		{
			errCode = rp_GenTrigger(3);
		}
		if (errCode != RP_OK)
		{
			printf("Error genTrigger: %7d\n", errCode);
			size=sprintf(str,"Error %d", errCode);
		    write(connfd,str,size);
		}
		else
		{
			printf("genTrigger");
			size=sprintf(str,"OK");
		    write(connfd,str,size);
		}
	}	
	else if (strcmp(command,"genTriggerSourceCh1")==0)
	{
		if (strcmp(cmdData,"RP_GEN_TRIG_SRC_INTERNAL")==0)
		{
			errCode = rp_GenTriggerSource(RP_CH_1, RP_GEN_TRIG_SRC_INTERNAL);
		}
		else if (strcmp(cmdData,"RP_GEN_TRIG_SRC_EXT_PE")==0)
		{
			errCode = rp_GenTriggerSource(RP_CH_1, RP_GEN_TRIG_SRC_EXT_PE);
		}
		else if (strcmp(cmdData,"RP_GEN_TRIG_SRC_EXT_NE")==0)
		{
			errCode = rp_GenTriggerSource(RP_CH_1, RP_GEN_TRIG_SRC_EXT_NE);
		}
		else
		{
			errCode = rp_GenTriggerSource(RP_CH_1, RP_GEN_TRIG_GATED_BURST);
		}
		if (errCode != RP_OK)
		{
			printf("Error genTriggerSourceCh1: %7d\n", errCode);
			size=sprintf(str,"Error %d", errCode);
		    write(connfd,str,size);
		}
		else
		{
			printf("genTriggerSourceCh1");
			size=sprintf(str,"OK");
		    write(connfd,str,size);
		}
	}	
	else if (strcmp(command,"genTriggerSourceCh2")==0)
	{
		if (strcmp(cmdData,"RP_GEN_TRIG_SRC_INTERNAL")==0)
		{
			errCode = rp_GenTriggerSource(RP_CH_2, RP_GEN_TRIG_SRC_INTERNAL);
		}
		else if (strcmp(cmdData,"RP_GEN_TRIG_SRC_EXT_PE")==0)
		{
			errCode = rp_GenTriggerSource(RP_CH_2, RP_GEN_TRIG_SRC_EXT_PE);
		}
		else if (strcmp(cmdData,"RP_GEN_TRIG_SRC_EXT_NE")==0)
		{
			errCode = rp_GenTriggerSource(RP_CH_2, RP_GEN_TRIG_SRC_EXT_NE);
		}
		else
		{
			errCode = rp_GenTriggerSource(RP_CH_2, RP_GEN_TRIG_GATED_BURST);
		}
		if (errCode != RP_OK)
		{
			printf("Error genTriggerSourceCh2: %7d\n", errCode);
			size=sprintf(str,"Error %d", errCode);
		    write(connfd,str,size);
		}
		else
		{
			printf("genTrigerSourceCh2");
			size=sprintf(str,"OK");
		    write(connfd,str,size);
		}
	}	
	else if (strcmp(command,"stopCT")==0)
	{
	    // Todo, stop ct
	    // size=sprintf(str,"%.01f",fpga_temp);
	    // write(connfd,str,size);
	}
	else if (strcmp(command,"startCT")==0)
	{
	    // Todo, start ct
	    // size=sprintf(str,"%.01f",fpga_temp);
	    // write(connfd,str,size);
	}
	else
	{
	    
	    write(connfd, "syntax error" , 13);
	}
	
	
    }
    printf("Client disconnected\n");
    fflush(stdout);
    close(connfd);
    return NULL;
    
}

  
