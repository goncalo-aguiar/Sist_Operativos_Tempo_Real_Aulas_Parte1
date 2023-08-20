/* ************************************************************
* Xenomai - creates a periodic task
*	
* Paulo Pedreiras
* 	Out/2020: Upgraded from Xenomai V2.5 to V3.1    
* 
************************************************************** */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>
#include <math.h>



#include <sys/mman.h> // For mlockall

// Xenomai API (former Native API)
#include <alchemy/task.h>
#include <alchemy/timer.h>

#include <alchemy/queue.h>

#define MS_2_NS(ms)(ms*1000*1000) /* Convert ms to ns */

/* *****************************************************
 * Define task structure for setting input arguments
 * *****************************************************/
 struct taskArgsStruct {
	 RTIME taskPeriod_ns;
	 int some_other_arg;
 };

/* *******************
 * Task attributes 
 * *******************/ 
#define TASK_MODE 0  	// No flags
#define TASK_STKSZ 0 	// Default stack size

#define TASK_A_PRIO 25 	// RT priority [0..99]
#define ACK_PERIOD_MS MS_2_NS(1000)

#define TASK_B_PRIO 25 	// RT priority [0..99]

#define TASK_C_PRIO 25 	// RT priority [0..99]

RT_TASK task_SENSOR_desc; // Task decriptor
RT_TASK task_PROCESSING_desc; // Task decriptor
RT_TASK task_STORAGE_desc; // Task decriptor


/* *********************
* Function prototypes
* **********************/
void catch_signal(int sig); 	/* Catches CTRL + C to allow a controlled termination of the application */
void wait_for_ctrl_c(void);
void Heavy_Work_SENSOR(void);      	/* Load task */
void task_code_SENSOR(void *args); 	/* Task body */
void Heavy_Work_PROCESSING(void);      	/* Load task */
void task_code_PROCESSING(void *args); 	/* Task body */
void Heavy_Work_STORAGE(void);      	/* Load task */
void task_code_STORAGE(void *args); 	/* Task body */

int LINHA = 0;

RT_QUEUE queue_sensor;
RT_QUEUE queue_processing;

/* ******************
* Main function
* *******************/ 
int main(int argc, char *argv[]) {
	int err; 
	struct taskArgsStruct taskSENSORArgs;
	struct taskArgsStruct taskPROCESSINGArgs;
	struct taskArgsStruct taskSTORAGEArgs;

    
	FILE *file;
    fopen("sensordataFiltered.txt","w");
	/* Lock memory to prevent paging */
	mlockall(MCL_CURRENT|MCL_FUTURE); 

    rt_queue_create(&queue_sensor, "queue_sensor", 10000, Q_UNLIMITED, Q_FIFO);
    rt_queue_create(&queue_processing, "queue_processing", 10000, Q_UNLIMITED, Q_FIFO);
   

	/* Create RT task */
	/* Args: descriptor, name, stack size, priority [0..99] and mode (flags for CPU, FPU, joinable ...) */
	err=rt_task_create(&task_SENSOR_desc, "Task SENSOR", TASK_STKSZ, TASK_A_PRIO, TASK_MODE);
	if(err) {
		printf("Error creating task SENSOR (error code = %d)\n",err);
		return err;
	} else 
		printf("Task SENSOR created successfully\n");
	
	err=rt_task_create(&task_PROCESSING_desc, "Task PROCESSING", TASK_STKSZ, TASK_B_PRIO, TASK_MODE);
	if(err) {
		printf("Error creating task PROCESSING (error code = %d)\n",err);
		return err;
	} else 
		printf("Task PROCESSING created successfully\n");
	
	err=rt_task_create(&task_STORAGE_desc, "Task STORAGE", TASK_STKSZ, TASK_C_PRIO, TASK_MODE);
	if(err) {
		printf("Error creating task STORAGE (error code = %d)\n",err);
		return err;
	} else 
		printf("Task STORAGE created successfully\n");


    
	taskSENSORArgs.taskPeriod_ns = ACK_PERIOD_MS; 	
    rt_task_start(&task_SENSOR_desc, &task_code_SENSOR, (void *)&taskSENSORArgs);
    rt_task_start(&task_PROCESSING_desc, &task_code_PROCESSING, (void *)&taskPROCESSINGArgs);
    rt_task_start(&task_STORAGE_desc, &task_code_STORAGE, (void *)&taskSTORAGEArgs);

    
	/* wait for termination signal */	
	wait_for_ctrl_c();

	return 0;
		
}

/* ***********************************
* Task body implementation
* *************************************/
void task_code_SENSOR(void *args) {
	RT_TASK *curtask;
	RT_TASK_INFO curtaskinfo;
	struct taskArgsStruct *taskArgs;

	RTIME ta=0;
	unsigned long overruns;
	int err;
	
	/* Get task information */
	curtask=rt_task_self();
	rt_task_inquire(curtask,&curtaskinfo);
	taskArgs=(struct taskArgsStruct *)args;
	printf("Task %s init, period:%llu\n", curtaskinfo.name, taskArgs->taskPeriod_ns);
	
   
	
	/* Set task as periodic */
	err=rt_task_set_periodic(NULL, TM_NOW, taskArgs->taskPeriod_ns);
	for(;;) {
		err=rt_task_wait_period(&overruns);
		ta=rt_timer_read();
		if(err) {
			printf("task %s overrun!!!\n", curtaskinfo.name);
			break;
		}
		printf("\nTask %s activation at time %llu\n", curtaskinfo.name,ta);
		
		/* Task "load" */
        FILE *fileStream; 
   
        fileStream = fopen ("sensordata.txt", "r"); 

        char line[18]; 
        void *msg;
        int i = 0; 
        int len;
        // rt_queue_bind(&queue_sensor,"queue_sensor",TM_INFINITE);
        while (fgets(line, sizeof(line), fileStream)) { 
            if(i == LINHA ) 
            { 
                len = strlen(line) + 1;
                msg = rt_queue_alloc(&queue_sensor,len);
                strcpy(msg,line);
                rt_queue_send(&queue_sensor,msg,len,Q_NORMAL); 
                
            } 
            i++;
        } 
        LINHA ++;
        fclose(fileStream); 

		
	}
	return;
}

void task_code_PROCESSING(void *args) {
	RT_TASK *curtask;
	RT_TASK_INFO curtaskinfo;
	struct taskArgsStruct *taskArgs;

	RTIME ta=0;
	unsigned long overruns;
	int err;
	
	/* Get task information */
	curtask=rt_task_self();
	rt_task_inquire(curtask,&curtaskinfo);
	taskArgs=(struct taskArgsStruct *)args;
    
    
	rt_queue_bind(&queue_sensor,"queue_sensor",TM_INFINITE);
    ssize_t len;
    void* msg;
    void* msg2;
    int numeros[5];
	int pos;
    int aux=0;
	int media = 0;
    while (( len = rt_queue_receive(&queue_sensor,&msg,TM_INFINITE)) > 0){
        printf("TASK PROCESSING");
        printf("\nreceived message> ptr=%p, s=%s",msg,(const char *)msg);
        rt_queue_free(&queue_sensor,msg);

        if (aux > 4){
			
            for(int i=0;i<4;i++){
                numeros[i] = numeros[i+1];
            }
            numeros[4] = atoi(msg);
        }
        else{
		
            numeros[pos] = atoi(msg);
            pos++;
        }
        
        if (aux >= 4 && pos >= 4){
			
            for (int i=0;i<=4;i++){
				
                media = media + numeros[i];
            }
			
            media = round((double)media/5);


			char line1[200];
			sprintf(line1,"%d",media);
			int len1 = strlen(line1) +1;
			msg2 = rt_queue_alloc(&queue_processing,len1);
			strcpy(msg2,line1);
			int x = rt_queue_send(&queue_processing,msg2,len1,Q_NORMAL); 
        }
		// else{
		// 	printf("pos : %d",pos);
		// 	for (int i=0;i<pos;i++){
				
		// 		printf("\n%d",numeros[i]);
        //         media = media + numeros[i];
        //     }
        //     media = round((double)media/pos);
		// 	printf("\n%d",media);


		// 	char line1[200];
		// 	sprintf(line1,"%d",media);
		// 	int len1 = strlen(line1) +1;
		// 	msg2 = rt_queue_alloc(&queue_processing,len1);
		// 	strcpy(msg2,line1);
		// 	int x = rt_queue_send(&queue_processing,msg2,len1,Q_NORMAL); 
		// }
        aux++;

	
		media = 0;
        
    }

   
    rt_queue_unbind(&queue_sensor);
	
		
	return;
}

void task_code_STORAGE(void *args) {
	RT_TASK *curtask;
	RT_TASK_INFO curtaskinfo;
	struct taskArgsStruct *taskArgs;

	RTIME ta=0;
	unsigned long overruns;
	int err;
	
	/* Get task information */
	curtask=rt_task_self();
	rt_task_inquire(curtask,&curtaskinfo);
	taskArgs=(struct taskArgsStruct *)args;
    
    
	rt_queue_bind(&queue_processing,"queue_processing",TM_INFINITE);
    ssize_t len;
    void* msg;
   
    FILE *file;
    file = fopen("sensordataFiltered.txt","a");
    while (( len = rt_queue_receive(&queue_processing,&msg,TM_INFINITE)) > 0){
        printf("\nTASK STORAGE");
        printf("\nreceived message> ptr=%p, s=%s\n",msg,(const char *)msg);
        rt_queue_free(&queue_processing,msg);
        fputs(msg,file);
		fputs("\n",file);
    }

    fclose(file);
    rt_queue_unbind(&queue_processing);
	
		
	return;
}


/* **************************************************************************
 *  Catch control+c to allow a controlled termination
 * **************************************************************************/
 void catch_signal(int sig)
 {
	 return;
 }

void wait_for_ctrl_c(void) {
	signal(SIGTERM, catch_signal); //catch_signal is called if SIGTERM received
	signal(SIGINT, catch_signal);  //catch_signal is called if SIGINT received

	// Wait for CTRL+C or sigterm
	pause();
	
	// Will terminate
	printf("Terminating ...\n");
}





