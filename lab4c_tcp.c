/* 
NAME: Lila Rungcharoenporn
EMAIL: yrung16@gmail.com
ID: 904994468
*/

//edit

#include <stdio.h>
#include <stdlib.h>
#include <getopt.h>
#include <signal.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <time.h>
#include <sys/time.h>
#include <poll.h>
#include <math.h>
#include <mraa.h>
#include <mraa/aio.h>
#include <ctype.h>
#include "fcntl.h"
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>

int log_fd;
int period = 1;
int log_opt = 0;
char scale_type = 'F';
mraa_aio_context sensor;
mraa_gpio_context button;
int stop = 0;
struct tm *read_time;
struct timeval timer;
// time_t next_read = 0;
time_t afterpoll;
time_t diff;
time_t begin, end;

char* id;
char* host_name;
int sockfd;
int port;


char buf[150];
char command[150];
int i;
int j = 0;

#define B 4275
#define R0 100000.0

//when pushed, outputs and logs a final sample with:
	// time and string SHUTDOWN (instead of temp)
	// exits
void do_when_pushed()
{
	read_time = localtime(&timer.tv_sec);
	dprintf(sockfd, "%.2d:%.2d:%.2d SHUTDOWN\n", read_time->tm_hour, read_time->tm_min, read_time->tm_sec);
	if(log_opt != 0) 
		dprintf(log_fd, "%.2d:%.2d:%.2d SHUTDOWN\n", read_time->tm_hour, read_time->tm_min, read_time->tm_sec);
	exit(0);
}

void log_it(char *tolog) {
	if(log_opt != 0) {
		dprintf(log_fd, "%s\n", tolog);
	}
}

void input_comm(char* comm) {
	if(strcmp(comm, "SCALE=F") == 0) {
		scale_type = 'F';
		log_it(comm);
	}
	else if(strcmp(comm, "SCALE=C") == 0) {
		scale_type = 'C';
		log_it(comm);
	}	
//https://stackoverflow.com/questions/4770985/how-to-check-if-a-string-starts-with-another-string-in-c
	else if(strncmp(comm, "PERIOD=", 7) == 0) {
		period = (int)atoi(comm+7);
		log_it(comm);
	}
	//stop report but process input commands...log command
	else if(strcmp(comm, "STOP") == 0) {
		stop = 1;
		log_it(comm);
	}
	//if currently stopped, resume generating reports
	//if not stopped, log command
	else if(strcmp(comm, "START") == 0) {
		stop = 0;
		log_it(comm);
	}
	//log command
	else if(strncmp(comm, "LOG", 3) == 0) {
		log_it(comm);
	}
	//like pressing button
	else if(strcmp(comm, "OFF") == 0) {
		log_it(comm);
		do_when_pushed();
	}
	//invalid command...log it anyway
	else
		log_it(comm);
}
//http://wiki.seeedstudio.com/Grove-Temperature_Sensor_V1.2/
float convert_temp_reading(double reading)
{
	float R = 1023.0/((float)reading) - 1.0;
	R = R0*R;
	//C is the temperature in Calcius
	float C = 1.0/(log(R/R0)/B + 1/298.15) - 273.15;
	//F is the temperature in Fahrenheit
	float F = (C*9)/5 + 32;
	if(scale_type == 'C')
		return C;
	else
		return F;
}

// slide 15 - overall code architecture
int main(int argc, char *argv[]) 
{
    int a;
    struct option long_options[] = {
        {"period", required_argument, 0, 'p'},
        {"scale", required_argument, 0, 's'},
        {"log", required_argument, 0, 'l'},
        {"id", required_argument, 0, 'i'},
        {"host", required_argument, 0, 'h'},
        {0, 0, 0, 0}
    };

	while (1)
	{
        a = getopt_long(argc, argv, "", long_options, 0);
        if(a == -1)
            break;
		switch(a)
		{
			case 'p':      
				period = atoi(optarg);
				break;  
            case 's':    
                scale_type = atoi(optarg); 
                break;
			case 'l':
            	log_opt = 1;
            	log_fd = creat(optarg, 0666);
            	if(log_fd < 0) 
            		fprintf(stderr, "Cannot create log file\n");
                break;
            case 'i':
            	if(strlen(optarg) != 9) {
            		fprintf(stderr, "id must have 9 digits\n");
            		exit(1);
            	}
            	id = optarg;
            	break;
            case 'h':
            	host_name = optarg;
            	break;
			default:    
                fprintf(stderr, "Unrecognized option %s \n", argv[optind-1]);
                fprintf(stderr, "Available options are --period, --scale, and --log\n");
                exit(1);
		}
	}

	if(optind < argc) {
		port = atoi(argv[optind]);
		if(port <= 0) {
			fprintf(stderr, "Invalid port number\n");
			exit(1);
		}
	}

	// client connect
	// slide 16
	if((sockfd = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
		fprintf(stderr, "Cannot create socket\n");
		exit(2);
	} 
	struct hostent *server;
	// convert host_name to IP address
	if((server = gethostbyname(host_name)) == NULL) {
		fprintf(stderr, "Error, no such host\n");
		exit(1);
	}
	struct sockaddr_in serv_addr; 
	memset((void*)&serv_addr, 0, sizeof(struct sockaddr_in)); 
	serv_addr.sin_family = AF_INET; 
	// copy IP address from server to serv_addr
	memcpy((char*)&serv_addr.sin_addr.s_addr, (char*)server->h_addr, server->h_length);
	serv_addr.sin_port = htons(port);
	if(connect(sockfd, (struct sockaddr *) &serv_addr, sizeof(serv_addr)) < 0) {
		fprintf(stderr, "Cannot connect to server\n");
		exit(2);
	}

	// send and log an ID terminated with a newline
	dprintf(sockfd, "ID=%s\n", id);
	if(log_opt == 1)
		dprintf(log_fd, "ID=%s\n", id);

	// initialize the sensors
	sensor = mraa_aio_init(1);
	if(sensor == NULL) {
		fprintf(stderr, "Cannot initialize sensor\n");
		mraa_deinit();
		exit(2);
	}

    struct pollfd fds[1];
    fds[0].fd = sockfd;
    fds[0].events = POLLIN | POLLERR | POLLHUP;

    // first report
    gettimeofday(&timer, 0);
    // if(timer.tv_sec >= next_read) {
    // if not stopped, print report to sockfd...then check if user passed in log option
    if(stop == 0) {
		double read_temp = mraa_aio_read(sensor);
		float scaled_temp = convert_temp_reading(read_temp);
		read_time = localtime(&timer.tv_sec);
		// next_read = timer.tv_sec + period;
		dprintf(sockfd, "%.2d:%.2d:%.2d %.1f\n", read_time->tm_hour, read_time->tm_min, read_time->tm_sec, scaled_temp);
		if(log_opt == 1) 
			dprintf(log_fd, "%.2d:%.2d:%.2d %.1f\n", read_time->tm_hour, read_time->tm_min, read_time->tm_sec, scaled_temp);        	
    }
    
    // time the poll...poll until period is over, then print temp report
    while(1) {
    	time(&begin);
    	while(1) {
			if(poll(fds, 1, 0) < 0) {
				fprintf(stderr, "poll failed: %s \n", strerror(errno));
				exit(2);
			}
        	if(fds[0].revents && POLLIN){
            	int count = read(sockfd, buf, 150);
            	if(count < 0){
                	fprintf(stderr, "Cannot read input\n");
                	exit(2);
            	}
            	int i, j;
				for (i = 0; i < count; ++i) {
					if (buf[i] == '\n') {
						command[j] = '\0';
						j = 0; 	//reset counter for the next command
						input_comm(command); 
						memset(command, 0, 150); //clear
					}
					else {
						command[j] = buf[i]; //keep copying if not yet a complete command
						j++; 
					}
            	}      
        	}
        	time(&end);
        	// if time to report temp
        	if(difftime(end, begin) >= period) {
    			gettimeofday(&timer, 0);
    			// if not stopped, print report to sockfd...then check if user passed in log option
        		if(stop == 0){
					double read_temp = mraa_aio_read(sensor);
					float scaled_temp = convert_temp_reading(read_temp);
					read_time = localtime(&timer.tv_sec);
					// next_read = timer.tv_sec + period;
					dprintf(sockfd, "%.2d:%.2d:%.2d %.1f\n", read_time->tm_hour, read_time->tm_min, read_time->tm_sec, scaled_temp);
					if(log_opt == 1) 
						dprintf(log_fd, "%.2d:%.2d:%.2d %.1f\n", read_time->tm_hour, read_time->tm_min, read_time->tm_sec, scaled_temp);        	
       			}
       			// after reporting, need a new "begin" time
       			break;
        	}
    	}
	}

	mraa_aio_close(sensor);
	// mraa_gpio_close(button);
	close(log_fd);

	exit(0);
}
