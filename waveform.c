#include <sys/mman.h>
#include <fcntl.h>

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <time.h>
#include <math.h>
#include "rp.h"
#include <pthread.h>
#include <signal.h>

#define RP_BUF_SIZE	16384

extern int SetupSocket_Server();
extern int CloseSocket_Server();
extern void *Process_Incoming_Commands(void *arg);
extern int Handle_Incoming_Connections();
void *Read_CT_Data(void *arg);
void *read_waveform_data(void *arg);


pthread_mutex_t mutex1=PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t mutex_settings=PTHREAD_MUTEX_INITIALIZER;
pthread_t network_thread,read_thread;

static bool keepRunning = true;
static bool fpga_running = true;

float charge[10];
int triggered;
int free_counter;
int new_data;
int count_table[10];	//Table for keeping number of trigs
int num_data_points_before_trig=10;
int num_data_points_after_trig=100;
int16_t record_length=8192;
int32_t trig_delay=1000;
float trig_level=0.2;
float fpga_temp;

float max_adc_v_ch1;
int dc_offset_ch1;
float max_adc_v_ch2;
int dc_offset_ch2;

int16_t* buff_ch1_raw;
float* buff_ch1;
float* buff_ch2;

rp_acq_trig_src_t trig_source=RP_TRIG_SRC_CHA_NE;

void intHandler() 
{
    keepRunning = false;
    fpga_running = false;
}

float integrated_val(float *signal, int start, int sig_len)
{
	float sum;
	sum=0;
	float retval;
	int samples;
	samples=0;

	for (int i=start;i<sig_len;i++)
	{
	    if (signal[i]<-0.002)
	    {
			sum=sum+fabsf(signal[i]);
			//printf("No: %d, Val: %f\n",i,signal[i]);
			samples++;
	    }
			
	}
	//retval=sum*8;				// 8 ns/sample
        retval=sum*4;				// 4 ns/sample when using two channels
	return retval;
}


// Returns integrated value
float integrated_charge(float *signal, int start, int sig_len)
{
	float sum;
	sum=0;
	int samples;
	samples=0;

	for (int i=start;i<sig_len;i++)
	{
	    if (signal[i]<-0.002)
	    {
		sum=sum+(fabsf(signal[i])*4);			// 4 ns/sample when using two channels
		//printf("No: %d, Val: %f\n",i,signal[i]);
		samples++;
	    }
	}
	return sum;
}


void setup_output()
{
    /* Generating frequency */
    rp_GenFreq(RP_CH_2, 0.0);

    /* Generating amplitude */
    rp_GenAmp(RP_CH_2, 0.0);

    /* Generating wave form */
    rp_GenWaveform(RP_CH_2, RP_WAVEFORM_DC );
    
    rp_GenOutEnable(RP_CH_2);
}


int main(int argc, char **argv)
{
    int iret1,iret2;
    struct sigaction act;
    act.sa_handler = intHandler;
    sigaction(SIGINT, &act, NULL);

	iret2 = pthread_create( &read_thread, NULL, read_waveform_data, NULL);
	if(iret2)
	{
		    fprintf(stderr,"Error - pthread_create() return code: %d\n",iret2);
		    //exit(EXIT_FAILURE);
	}
	    
    if (SetupSocket_Server()==1)
    {
	Handle_Incoming_Connections();
	

	usleep(100000);
	iret1 = pthread_create( &network_thread, NULL, Process_Incoming_Commands, NULL);
	if(iret1)
	{
		    fprintf(stderr,"Error - pthread_create() return code: %d\n",iret1);
		    exit(EXIT_FAILURE);
	}



    }
    fflush(stdout);
    while (keepRunning==true)
    {
		//pthread_join(read_thread,NULL);
		pthread_join(network_thread,NULL);
		usleep(100000);
		printf("Network thread exit");
		Handle_Incoming_Connections();
		iret1 = pthread_create( &network_thread, NULL, Process_Incoming_Commands, NULL);
		if(iret1)
		{
				fprintf(stderr,"Error - pthread_create() return code: %d\n",iret1);
				exit(EXIT_FAILURE);
		}
		
    }
    CloseSocket_Server();
    printf("Exiting..");


}

void *read_waveform_data(void *arg)   
{
    int error_code;
    rp_pinState_t heart_beat_state; 
    uint32_t buff_size = RP_BUF_SIZE + 2;
    uint32_t buff_size2 = RP_BUF_SIZE;
    uint32_t trig_pos;

	// CT calculations varaibles
    clock_t start_time, diff;
    
    // Raw waveform variables
    int32_t decimated_data_num;
    
    buff_ch1 = (float*)malloc(buff_size * sizeof(float));
    buff_ch2 = (float*)malloc(buff_size * sizeof(float));
    // Pointer to first data sample (first position is header with number of samples):
    float* buff_ch1_offset;
	float* buff_ch2_offset;
    buff_ch1_offset = buff_ch1 + 2*sizeof(float);
	buff_ch2_offset = buff_ch2 + 2*sizeof(float);
	// First two numbers are header: read length and trigger counter
	uint32_t start_pos;
	uint32_t end_pos;
	uint32_t buff_filled_size;
    
    new_data=0;
    
    /* Print error, if rp_Init() function failed */
    if(rp_Init() != RP_OK)
    {
        fprintf(stderr, "Rp api init failed!n");
    }
    
    /* Retrieving calibration settings*/
	rp_calib_params_t rp_main_calib_params;
	rp_main_calib_params = rp_GetCalibrationSettings();

	printf("Calibration fe_ch1_fs_g_hi: %7d\n", rp_main_calib_params.fe_ch1_fs_g_hi);
	printf("Calibration fe_ch1_dc_offs: %7d\n", rp_main_calib_params.fe_ch1_dc_offs);
	printf("Calibration be_ch1_dc_offs: %7d\n", rp_main_calib_params.be_ch1_dc_offs);
	printf("Calibration be_ch1_fs: %7d\n", rp_main_calib_params.be_ch1_fs);
	printf("Calibration fe_ch2_fs_g_hi: %7d\n", rp_main_calib_params.fe_ch2_fs_g_hi);
	printf("Calibration fe_ch2_dc_offs: %7d\n", rp_main_calib_params.fe_ch2_dc_offs);
	printf("Calibration be_ch2_dc_offs: %7d\n", rp_main_calib_params.be_ch2_dc_offs);
	printf("Calibration be_ch2_fs: %7d\n", rp_main_calib_params.be_ch2_fs);
	max_adc_v_ch1 = rp_main_calib_params.fe_ch1_fs_g_hi/(float)((uint64_t)1<<32) * 100;
	dc_offset_ch1 = rp_main_calib_params.be_ch1_dc_offs;
	max_adc_v_ch2 = rp_main_calib_params.fe_ch2_fs_g_hi/(float)((uint64_t)1<<32) * 100;
	dc_offset_ch2 = rp_main_calib_params.be_ch2_dc_offs;


	if (rp_AcqReset() != RP_OK) {
		printf("Acquire reset failed!\n");
		//write(client_sock , "Acquire reset error" , 19);
	}
    //rp_AcqSetDecimation(RP_DEC_8 );
    rp_AcqSetSamplingRate(RP_SMP_125M);
    //rp_AcqSetSamplingRate(RP_SMP_15_625M);
    rp_AcqSetTriggerLevel(trig_level); //Trig level is set in Volts while in SCPI is set in mV
//    rp_AcqSetTriggerDelayNs(0);
    error_code = rp_AcqSetTriggerDelay(trig_delay); 	
    printf("Setting trigger delay %7d, returned %7d\n", trig_delay, error_code);
    //rp_AcqSetTriggerDelay(0);
    //rp_AcqSetAveraging(true);
	
    rp_AcqStart();

    //rp_AcqSetTriggerSrc(RP_TRIG_SRC_EXT_PE);
    //rp_AcqSetTriggerSrc(RP_TRIG_SRC_CHA_NE);			// Trig on signal
    rp_AcqSetTriggerSrc(trig_source);			
    rp_acq_trig_state_t state = RP_TRIG_STATE_TRIGGERED;

    // Get clock
    start_time = clock();

    rp_DpinSetDirection ( RP_LED4, RP_OUT);
    rp_DpinSetDirection ( RP_LED3, RP_OUT);
    rp_DpinSetDirection ( RP_DIO1_P, RP_OUT);
    rp_AcqStart();    
    //Clear count_table    
    free_counter=0;
    heart_beat_state=RP_LOW;

	while(fpga_running == true)
	{
	    // Check for trig
	    
		rp_AcqGetTriggerState(&state);
		if(state == RP_TRIG_STATE_TRIGGERED)
		{
			usleep(10);
			
			pthread_mutex_lock( &mutex1 );
			
			triggered=1;
			// Get data
			rp_DpinSetState( RP_LED4, RP_HIGH);
			
			// Read trigger parameters
			rp_AcqGetWritePointerAtTrig(&trig_pos);						
			rp_AcqGetTriggerDelay(&decimated_data_num);
//			printf("Trigger position: %7d\n", trig_pos);
			printf("Read trigger delay: %7d\n", decimated_data_num);

//			rp_AcqGetDataPosV(RP_CH_1,0,(RP_BUF_SIZE-1),buff_ch1,&buff_size2);			// Get entire buffer 

			
			// Calculate end point
			end_pos = trig_pos + decimated_data_num;
			
			if (end_pos > (RP_BUF_SIZE - 1)) 
			// The end point goes past the end of the ring buffer:
			{
				start_pos = end_pos - record_length;
				end_pos = RP_BUF_SIZE - 1;
				buff_filled_size = RP_BUF_SIZE;
//				error_code=rp_AcqGetDataPosRaw(RP_CH_1, start_pos, end_pos, buff_ch1_raw_offset, &buff_filled_size);
				error_code=rp_AcqGetDataPosV(RP_CH_1,start_pos, end_pos, buff_ch1_offset, &buff_filled_size);
				buff_filled_size = RP_BUF_SIZE;
//				error_code=rp_AcqGetDataPosV(RP_CH_2,start_pos, end_pos, buff_ch2_offset, &buff_filled_size);
				
				end_pos = end_pos - start_pos - 1;
				start_pos = 0;
				buff_filled_size = RP_BUF_SIZE;
				error_code=rp_AcqGetDataPosV(RP_CH_1, start_pos, end_pos, buff_ch1_offset + buff_filled_size, &buff_filled_size);
				buff_filled_size = RP_BUF_SIZE;
//				error_code=rp_AcqGetDataPosV(RP_CH_2, start_pos, end_pos, buff_ch2_offset + buff_filled_size, &buff_filled_size);
			}
			else
			// The end point is within the end of the ring buffer:
			{			
				if (end_pos < record_length)
				// The start point is before the beginning of the ring buffer:
				{
					start_pos = RP_BUF_SIZE + end_pos - record_length;
					end_pos = RP_BUF_SIZE - 1;
					buff_filled_size = RP_BUF_SIZE;
					error_code=rp_AcqGetDataPosV(RP_CH_1, start_pos, end_pos, buff_ch1_offset, &buff_filled_size);
					buff_filled_size = RP_BUF_SIZE;
//					error_code=rp_AcqGetDataPosV(RP_CH_2, start_pos, end_pos, buff_ch2_offset, &buff_filled_size);
					
					end_pos = record_length - end_pos + start_pos - 1;
					start_pos = 0;
					buff_filled_size = RP_BUF_SIZE;
//					error_code=rp_AcqGetDataPosV(RP_CH_1, start_pos, end_pos, buff_ch1_offset + buff_filled_size*sizeof(int16_t), &buff_filled_size);
					error_code=rp_AcqGetDataPosV(RP_CH_1, start_pos, end_pos, buff_ch1_offset + buff_filled_size, &buff_filled_size);
					buff_filled_size = RP_BUF_SIZE;
//					error_code=rp_AcqGetDataPosV(RP_CH_2, start_pos, end_pos, buff_ch2_offset + buff_filled_size, &buff_filled_size);
				}
				else
				// The waveform is completely within the ring buffer:
				{
					start_pos = end_pos - record_length;
					buff_filled_size = RP_BUF_SIZE;
					error_code=rp_AcqGetDataPosV(RP_CH_1, start_pos, end_pos, buff_ch1_offset, &buff_filled_size);
					buff_filled_size = RP_BUF_SIZE;
					error_code=rp_AcqGetDataPosV(RP_CH_2, start_pos, end_pos, buff_ch2_offset, &buff_filled_size);
				}				
			}

			
			buff_ch1[0] = (float)record_length;
			buff_ch1[1] = (float)free_counter;
			buff_ch2[0] = (float)record_length;
			buff_ch2[1] = (float)free_counter;
			new_data = 1;
			free_counter++;
//			printf("%f %f %f %f %f\n", buff_ch1[1], buff_ch1[2], buff_ch1[3], buff_ch1[4], buff_ch1[5]);
//			printf("Error code: %7d, buff filled size: %7d\n", error_code, buff_filled_size);
			pthread_mutex_unlock( &mutex1 );
			error_code = rp_AcqSetTriggerDelay(trig_delay); 	
			rp_AcqStart();
			rp_AcqSetTriggerSrc(trig_source);
			rp_DpinSetState( RP_LED4, RP_LOW);

//			printf("Setting trigger delay %7d, returned %7d\n", trig_delay, error_code);

			//Debug
			//printf("High Ring Buffer overflow\n");
			//printf("ch1: %f ch2: %f\n",val_ch1,val_ch2);
			//printf("Trig pos: %d end %d start %d\n", trig_pos, endpoints, startpoints);
			
		}
			
		// Get FPGA temp
		rp_HealthGetValue(RP_TEMP_FPGA, &fpga_temp);
		//printf("FPGA temp: %.01f C\n",fpga_temp);	    
	    
	}
	/* Releasing resources */
	printf("Exiting read_waveform");	    


	free(buff_ch1);
	free(buff_ch2);
	printf("free");
	rp_Release();
	printf("release");
	fflush(stdout);
	
	return NULL;


}


void *Read_CT_Data(void *arg)
{
    rp_pinState_t heart_beat_state; 
    uint32_t buff_size = RP_BUF_SIZE;
    uint32_t trig_pos;
    float *buff_ch1 = (float *)malloc(buff_size * sizeof(float));
    float *buff_ch2 = (float *)malloc(buff_size * sizeof(float));
    float val_sum_ch1,val_sum_ch2,val_ch1,val_ch2;
    float tmp_val_ch1, tmp_val_ch2;
    float charge_ch1;
    float charge_ch2;  
    int counter;
    clock_t start_time, diff;
    int msec;
    int endpoints;
    int startpoints;
    
    val_sum_ch1=0;
    counter=0;
    msec=0;
    new_data=0;
    
    /* Print error, if rp_Init() function failed */
    if(rp_Init() != RP_OK)
    {
        fprintf(stderr, "Rp api init failed!n");
    }
    
    setup_output();

    //rp_AcqSetDecimation(RP_DEC_8 );
    rp_AcqSetSamplingRate(RP_SMP_125M);
    //rp_AcqSetSamplingRate(RP_SMP_15_625M);
    rp_AcqSetTriggerLevel(trig_level); //Trig level is set in Volts while in SCPI is set in mV
    rp_AcqSetTriggerDelayNs(0);
    //rp_AcqSetTriggerDelay(0);
    rp_AcqSetAveraging(true);
    rp_AcqStart();

    //rp_AcqSetTriggerSrc(RP_TRIG_SRC_EXT_PE);
    //rp_AcqSetTriggerSrc(RP_TRIG_SRC_CHA_NE);			// Trig on signal
    rp_AcqSetTriggerSrc(trig_source);			
    rp_acq_trig_state_t state = RP_TRIG_STATE_TRIGGERED;

    // Get clock
    start_time = clock();

    rp_DpinSetDirection ( RP_LED4, RP_OUT);
    rp_DpinSetDirection ( RP_LED3, RP_OUT);
    rp_DpinSetDirection ( RP_DIO1_P, RP_OUT);
    rp_AcqStart();    
    //Clear count_table
    count_table[0]=0;
    free_counter=0;
    heart_beat_state=RP_LOW;

	while(1)
	{
	    // Wait for trig
	    
	    while(1)
	    {
		diff=clock()-start_time;
		msec=diff*1000/CLOCKS_PER_SEC;
		rp_AcqGetTriggerState(&state);
		if(state == RP_TRIG_STATE_TRIGGERED)
		{
		    usleep(10);
		    triggered=1;
		    break;
		}
		if (msec>2000)
		{
		    rp_AcqStart();
		    rp_AcqSetTriggerSrc(trig_source);
		    rp_GenAmp(RP_CH_2, 0);
		    triggered=0;
		    printf("No trig for 2 s... Set output analog output to zero");
		}

	    }
	    // Get data
	    rp_DpinSetState( RP_LED4, RP_HIGH);
	    rp_AcqGetWritePointerAtTrig(&trig_pos);
	    //printf("Trig pos: %d", trig_pos);
	    // Check if trig_pos is too close to buffer start/end
	    if ((trig_pos+num_data_points_after_trig)>(RP_BUF_SIZE-1))
	    {
		endpoints=(RP_BUF_SIZE-1)-(trig_pos-num_data_points_before_trig);
		startpoints=num_data_points_after_trig-((RP_BUF_SIZE-1)-trig_pos);
		rp_AcqGetDataPosV(RP_CH_1,0,(RP_BUF_SIZE-1),buff_ch1,&buff_size);			// Get entire buffer 
		rp_AcqGetDataPosV(RP_CH_2,0,(RP_BUF_SIZE-1),buff_ch2,&buff_size);			// Get entire buffer

		val_ch1=integrated_charge(buff_ch1,((RP_BUF_SIZE-1)-endpoints),(RP_BUF_SIZE-1));
		tmp_val_ch1=integrated_charge(buff_ch1,0,startpoints);
		val_ch2=integrated_charge(buff_ch2,((RP_BUF_SIZE-1)-endpoints),(RP_BUF_SIZE-1));
		tmp_val_ch2=integrated_charge(buff_ch2,0,startpoints);
		val_ch1=val_ch1+tmp_val_ch1;
		val_ch2=val_ch2+tmp_val_ch2;
	
		//Debug
		//printf("High Ring Buffer overflow\n");
		//printf("ch1: %f ch2: %f\n",val_ch1,val_ch2);
		//printf("Trig pos: %d end %d start %d\n", trig_pos, endpoints, startpoints);
		
	    }
	    else if ((trig_pos-num_data_points_before_trig)<0)
	    {	
		
		endpoints=(trig_pos-num_data_points_before_trig);
		startpoints=num_data_points_after_trig-endpoints;
		rp_AcqGetDataPosV(RP_CH_1,0,(RP_BUF_SIZE-1),buff_ch1,&buff_size);			// Get entire buffer
		rp_AcqGetDataPosV(RP_CH_2,0,(RP_BUF_SIZE-1),buff_ch2,&buff_size);			// Get entire buffer

		val_ch1=integrated_charge(buff_ch1,((RP_BUF_SIZE-1)-endpoints),(RP_BUF_SIZE-1));
		tmp_val_ch1=integrated_charge(buff_ch1,0,startpoints);
		val_ch2=integrated_charge(buff_ch2,((RP_BUF_SIZE-1)-endpoints),(RP_BUF_SIZE-1));
		tmp_val_ch2=integrated_charge(buff_ch2,0,startpoints);
		val_ch1=val_ch1+tmp_val_ch1;
		val_ch2=val_ch2+tmp_val_ch2;
	
		//Debug
		printf("Low Ring Buffer underrun\n");
		printf("ch1: %f ch2: %f\n",val_ch1,val_ch2);
		printf("Trig pos: %d end %d start %d\n", trig_pos, endpoints, startpoints);
	    }
	    else
	    {
		rp_AcqGetDataPosV(RP_CH_1,(trig_pos-num_data_points_before_trig),(trig_pos+num_data_points_after_trig),buff_ch1,&buff_size);
		rp_AcqGetDataPosV(RP_CH_2,(trig_pos-num_data_points_before_trig),(trig_pos+num_data_points_after_trig),buff_ch2,&buff_size);
		val_ch1=integrated_charge(buff_ch1,0,(num_data_points_after_trig+num_data_points_before_trig));
		val_ch2=integrated_charge(buff_ch2,0,(num_data_points_after_trig+num_data_points_before_trig));
	    }
	        
	    val_sum_ch1=val_sum_ch1+val_ch1;
	    val_sum_ch2=val_sum_ch2+val_ch2;
	    counter++;
	    charge[0]=val_ch1+val_ch2;

	    //rp_DpinSetState( RP_DIO1_P, RP_HIGH);
	    //usleep(20);
	    //rp_DpinSetState( RP_DIO1_P, RP_LOW);
	    
	    // Update data each s, 1050 ms to make sure the buffers update even @ 1 Hz trigger rate
	    if (msec>1050)
	    {
		start_time = clock();
		charge_ch1=(val_sum_ch1/(counter-1));
		charge_ch2=(val_sum_ch2/(counter-1));
		pthread_mutex_lock( &mutex1 );
		charge[1]=charge_ch1+charge_ch2;
		// Update table of volt & counters for sec 1-9
		for (int i=9;i>1;i--)
		{
		   count_table[i]=count_table[i-1]+counter;
		   charge[i]=charge[i-1]+charge[1];
		}
		count_table[1]=counter;
		charge[1]=charge[1];
		printf("\nNo of Triggers: %d, Time betweem (msec): %d\n", count_table[1], msec);

		/* Generating amplitude */
		//Check if overflow for the DAC
		if (charge[1]<1)
		{
		    rp_GenAmp(RP_CH_2, charge[1]);
		}
		else
		{
		    rp_GenAmp(RP_CH_2, 1);
		}

		printf("Charge: %f nC, Charge Ch1: %f nC, Charge Ch2: %f nC\n",charge[1], charge_ch1,charge_ch2);
		fflush(stdout);
		val_sum_ch1=0;
		val_sum_ch2=0;
		counter=0;
		free_counter++;
		// Digital output heartbeat
		rp_DpinGetState(RP_DIO1_P, &heart_beat_state);
		if (heart_beat_state==RP_LOW)
		{
		    rp_DpinSetState( RP_DIO1_P, RP_HIGH);
		}
		else
		{
		    rp_DpinSetState( RP_DIO1_P, RP_LOW);  
		}
		// Get FPGA temp
		rp_HealthGetValue(RP_TEMP_FPGA, &fpga_temp);
		printf("FPGA temp: %.01f C\n",fpga_temp);
		
		
	    }
	    new_data=1;
	    pthread_mutex_unlock( &mutex1 );
	    rp_AcqStart();
	    rp_AcqSetTriggerSrc(trig_source);
	    rp_DpinSetState( RP_LED4, RP_LOW);
	    
	    
	}
	/* Releasing resources */

	fflush(stdout);
	free(buff_ch1);
	free(buff_ch2);
	rp_Release();
	
	return NULL;


}
