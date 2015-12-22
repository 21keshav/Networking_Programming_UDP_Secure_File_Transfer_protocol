#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "unpifiplus.h"
#include "unp.h"


#define	MAX_SIZE		1000
#define	DATAGRAM_SIZE	512
#define	HEADER_SIZE		30
#define	PAYLOAD_SIZE	DATAGRAM_SIZE - HEADER_SIZE
#define	MAX_PACKETS		2000
#define	TIMED_WAIT		1
#define	MAX_SLEEP_TIME	1000

int						expected_ack;
int						client_soc_copy;
int						client_window_locked = 0;
int                     adver_window;
int						received_seq[MAX_PACKETS];
int						window_start = 0;
char					msgprint[MAX_PACKETS][MAX_SIZE];
pthread_mutex_t 		buffer_mutex = 
						PTHREAD_MUTEX_INITIALIZER;

/* Structure to hold client operation parameters */ 
typedef struct client_input_ {
	char	*server_ip;
	int		server_port;
	char	*file_name;
	int		sl_window_size;
	int		seed_value;
	double	prob;
	int		mean_time;
} client_input;

/* Enum for server location */
typedef enum server_location_ {
	SERVER_NONE,
	SERVER_SAME_HOST,
	SERVER_SAME_SUBNET,
	SERVER_DIFF_SUBNET
}server_location;

/* Enum for probability result */
typedef enum probability_result_ {
	PROBABILITY_PACKET_NONE,
	PROBABILITY_PACKET_SUCCESS,
	PROBABILITY_PACKET_FAILURE
}probability_result;

/* ARQ Mechanism Function */
int arq_sendrecv(int connfd,
                 char  *buffer){   
	int    status;
	struct timeval 		timeout;
	struct sockaddr_in 	IP;
	socklen_t           IP_len;
	timeout.tv_sec = 3;
	timeout.tv_usec = 0;
	fd_set 		read_fd;
	FD_ZERO(&read_fd);
	FD_SET(connfd, &read_fd);
	status = select(connfd + 1, &read_fd, NULL, NULL, &timeout);
		if (status < 0) {
			printf("\nStatus = %d, Unable to monitor sockets !!! Exiting ...",status);
			return 0;
		}
    if (FD_ISSET(connfd, &read_fd)) {	
		IP_len = sizeof(struct sockaddr_in);
		status = recvfrom(connfd,buffer , MAX_SIZE, 0,
					  (struct sockaddr *)&IP, &IP_len);
		if (status < 0) {
			printf("\nStatus = %d, Socket read error in server!!! Exiting ...\n");
			return 0;
		}
		buffer[status] = '\0';
		return(1);
	}else{
		return(0);
	}
	
}

/* Read details from Client.in */
int
get_client_data(struct client_input_	*client_data) {
	char	file_line[MAX_SIZE], *ptr_ret = NULL;
	FILE	*fp = NULL;
	int		no_chars = 0, status = 0;
	

	fp = fopen("client.in", "r");
	if (fp == NULL) {
		printf("\nClient.in not present in code directory !!! Exiting ...\n");
		return -1;
	}
	
	/* Read Server Ip from client.in */
	ptr_ret = fgets(file_line, MAX_SIZE, fp);
	size_t ln = strlen(file_line) - 2;
	if (file_line[ln] == '\r'){
		file_line[ln] = '\0';
	}
	if (ptr_ret == NULL) {
		printf("\nUnexpected end of file !!!\nCheck client.in ...\n");
		return -1;
	}
	client_data->server_ip = (char *)malloc(strlen(file_line)*sizeof(char));
	strncpy(client_data->server_ip, file_line, strlen(file_line));
	
	/* Read WellKnown Port of Server from client.in */
	ptr_ret = fgets(file_line, MAX_SIZE, fp);
	if (ptr_ret == NULL) {
		printf("\nUnexpected end of file !!!\nCheck client.in ...\n");
		return -1;
	}
	
	status = atoi(file_line);
	if (status < 1 || status > 65535) {
		printf("\nPort number of server is invalid !!!\nExiting ...\n");
		return -1;
	}
	client_data->server_port = status;
	
	/* Read File name to be Reterived from the Server from client.in */
	ptr_ret = fgets(file_line, MAX_SIZE, fp);
	size_t ln1 = strlen(file_line) - 2;
	if (file_line[ln1] == '\r'){
		file_line[ln1 ] = '\0';
	}
	if (ptr_ret == NULL) {
		printf("\nUnexpected end of file !!!\nCheck client.in ...\n");
		return -1;
	}
	client_data->file_name = (char *)malloc(strlen(file_line)*sizeof(char));
	strncpy(client_data->file_name, file_line, strlen(file_line));
	
	/* Read Client Window Size from client.in */
	ptr_ret = fgets(file_line, MAX_SIZE, fp);
	if (ptr_ret == NULL) {
		printf("\nUnexpected end of file !!!\nCheck client.in ...\n");
		return -1;
	}
	status = atoi(file_line);
	if (status == 0) {
		printf("\nSliding window size should be more than 0 !!!Exiting ...\n");
		return -1;
	}
	client_data->sl_window_size = status;

	ptr_ret = fgets(file_line, MAX_SIZE, fp);
	if (ptr_ret == NULL) {
		printf("\nUnexpected end of file !!!\nCheck client.in ...\n");
		return -1;
	}
	/* Read Seed value from client.in */
	status = atoi(file_line);
	if (status == 0) {
		printf("\nSeed value is invalid !!!\nExiting ...\n");
		return -1;
	}
	client_data->seed_value = status;
	
	/* Read Probability that Packet is Dropped from client.in */
	ptr_ret = fgets(file_line, MAX_SIZE, fp);
	if (ptr_ret == NULL) {
		printf("\nUnexpected end of file !!!\nCheck client.in ...\n");
		return -1;
	}
	client_data->prob = atof(file_line);
	if (client_data->prob < 0.0 || client_data->prob > 1.0) {
		printf("\nProbability value is invalid !!!\nExiting ...\n");
		return -1;
	}
	
    /* Read MeanU from client.in */
	ptr_ret = fgets(file_line, MAX_SIZE, fp);
	if (ptr_ret == NULL) {
		printf("\nUnexpected end of file !!!\nCheck client.in ...\n");
		return -1;
	}
	status = atoi(file_line);
	if (status == 0) {
		printf("\nMean time value is invalid !!!\nExiting ...\n");
		return -1;
	}
	client_data->mean_time = status;
	
	ptr_ret = fgets(file_line, MAX_SIZE, fp);
	if (ptr_ret != NULL) {
		printf("\nExtra data present in client.in file !!!\nExiting ...\n");
		return -1;
	}
	return 0;
}

/* Function to open Advertising Window when client window is locked */
int
send_window_unlock_ack() {
	int		status;
	char	ack[MAX_SIZE];
	
    /*Fill expected Acknowledged in Acknowlegment*/
	status = fill_packet_data(ack + 5, expected_ack, 5);
	if (status != 0) {
		printf("\nFailed to fill ACK in packet !!!");
		return -1;
	}
	
	/*Fill Opened Advertisment Window in Acknowlegment*/
	status = fill_packet_data(ack + 10, adver_window, 5);
	if (status != 0) {
		printf("\nFailed to fill window size in packet !!!");
		return -1;
	}
    
	/*Fill  data in Acknowlegment*/
	status = fill_packet_data(ack, 0, 5);
	if (status != 0) {
		printf("\nFailed to fill sequence number in packet !!!");
		return -1;
	}		
    
	/*Fill  data in Acknowlegment*/
	status = fill_packet_data(ack + 15, 0, 15);
	if (status != 0) {
		printf("\nFailed to zero fill packet !!!");
		return -1;
	}
	ack[HEADER_SIZE] = '\0';
	
	/*Send Acknowlegment to the Server that Advertisment Window has Opened*/
	status = send(client_soc_copy, ack, strlen(ack), 0);
	if (status < 0) {
		printf("\nStatus = %d, Data sent incompletely ...", status);
		return -1;
	}
	printf("\nSent client sliding window unlock ACK = %s", ack);
	
	return 0;
}

/* Print routine to print packet data */
/* This thread acts a consumer of data in sliding window */
void *
print_routine(void 	*data)	{
	int 	sleep_time;
	int 	status, fin;
	int 	mean_time = *(int *)data;
	float 	randomnum;
	int		print_ptr = 0;

	while(1)	{
		pthread_mutex_lock(&buffer_mutex);	
		for(; received_seq[print_ptr] == 1; print_ptr++)	{
		    
			/* Printing of data avialable in the Print Buffer */
			printf("\nPacket %d consumed: %s", print_ptr, msgprint[print_ptr] + HEADER_SIZE);
			status = get_packet_data(msgprint[print_ptr]+15, 1, &fin);
			if (status != 0) {
				printf("\nFailed to read packet from server !!!");
				return NULL;
			}
			
			/* Update advertisment window as Receive buffer is made free */
			window_start++;
			adver_window++;
		}
        
		/* Open advertisment window after Receive Buffer is full */
		if (client_window_locked == 1 && adver_window > 0) {
			client_window_locked = 0;
			status = send_window_unlock_ack();
			if (status != 0) {
				printf("\nStatus = %d, Unable to send window unlock ack !!!");
				return NULL;
			}
		}
		pthread_mutex_unlock(&buffer_mutex);
       		
		if(fin == 1) {
		    /* Terminate Thread if Last Packet is Printed */
			printf("\nTotal Packets consumed = %d\n", (print_ptr));
			return(NULL);
		}
		
		/* Exponential Distibution Calculation using mean_time read from client.in */
		randomnum = (float)(rand())/(RAND_MAX);
		sleep_time = log((double)randomnum) *mean_time* -1;
		
		/* Thread sleeps after Printing for Milliseconds according to
		   Exponential Distibution Calculation */		
		if (sleep_time > MAX_SLEEP_TIME)
			sleep_time = MAX_SLEEP_TIME;
		usleep(sleep_time*1000);
	}
	return NULL;
}

/* Probability Function to decide whether Packet is Dropped or Accepted
   Depending uopn Randomno Generator */
int
LossProbability(float 					probabilityloss,
                probability_result      packetprobabilityn){
	float randomnum;
	randomnum=(float)(rand())/(RAND_MAX);
	
	if(randomnum < (float)probabilityloss) {
		packetprobabilityn=PROBABILITY_PACKET_FAILURE;
		return(packetprobabilityn);
	} else {
		packetprobabilityn=PROBABILITY_PACKET_SUCCESS;
		return(packetprobabilityn);
	}
}

/* Count digits in the number */
int
count_digits(int	number) {
	int		count = 0;
	while(number != 0) {
		number = number/10;
		count++;
	}
	return count;
}

/* Fill data in Acknowlegement Header to be sent to Server */
int
fill_packet_data(char	*packet,
				 int	data,
				 int	data_size) {
	int		no_digit = count_digits(data);
	int		iter;
	
	iter = data_size - 1;
	for(; iter >= 0; iter --) {
		packet[iter] = data%10 + 48;
		data = data/10;
	}
	
	for(iter = 0; iter < data_size - no_digit; iter ++) {
		packet[iter] = 48;
	}
	return 0;
}

/*Function Reterives Details from Packet Header Recieved*/
int
get_packet_data(char	*packet,
				int		data_size,
				int		*ret) {
	char	number[30];
	int		iter, ans = 0;
	
	for (iter = 0; iter < data_size; iter++) {
		number[iter] = packet[iter];
	}
	number[iter] = '\0';
	*ret = atoi(number);
	return 0;
}

/*Reterive File details from the server*/
int
receive_file(int	client_soc,
			 int	window_size,
			 float 	drop_prob,
			 int	mean_time) {
	int						count = 0, status;
	struct sockaddr_in 		server_addr;
	socklen_t				server_len;
	int						seq_no, window_end;
	char					ack[MAX_SIZE], temp[MAX_SIZE], *ptr;
	int                     fin = 0, last_packet_seq;
	int						lossprobability;
	pthread_t               printdata; 
	probability_result      packetprobability = PROBABILITY_PACKET_NONE;
	
	/* Create thread for printing the packet data */
	pthread_create(&printdata, NULL, &print_routine, (void *)&mean_time);
	
	pthread_mutex_lock(&buffer_mutex);
	for (count = 0; count < MAX_PACKETS; count++) {
		received_seq[count] = 0;
	}
	window_start = 0;
	adver_window = window_size;
	client_soc_copy = client_soc;
	pthread_mutex_unlock(&buffer_mutex);
	
	for(count = 0;;count++) {
		
		/* Receive Packet Sent from the Server */
		server_len = sizeof(struct sockaddr_in);
		status = recvfrom(client_soc, temp, MAX_SIZE, 0,
							(struct sockaddr *)&server_addr, &server_len);
		if (status < 0) {
			printf("\nStatus = %d, Socket read error in client !!! Exiting...\n", status);
			return -1;
		} else if (status == 0) {
			printf("\nFile specified not present on server !!! Exiting...\n");
			return -1;
		}	
		temp[status] = '\0';
		
		/* Retrieve Sequence Number from From Packet Recieved */		
		status = get_packet_data(temp, 5, &seq_no);
		if (status != 0) {
			printf("\nFailed to read packet from server !!!");
			return -1;
		}		
		/* Drop Packet or Accept Packet depending on Loss Probability */		
		packetprobability = LossProbability(drop_prob,packetprobability);
		if(packetprobability == PROBABILITY_PACKET_FAILURE ) {
			/* Packet is lost */
			printf("\nPacket %d is dropped ",seq_no);
			continue;
		} else {
			/* Packet not lost */
			printf("\nPacket %d is accepted ",seq_no);
		}
		
		/* Retrieve Fin From Packet details */
		status = get_packet_data(temp+15, 1, &fin);
		if (status != 0) {
			printf("\nFailed to read packet from server !!!");
			return -1;
		}
		if (fin == 1)
			last_packet_seq = seq_no + 1;
			
	    /* Fill data in Acknowlegment */	
		status = fill_packet_data(ack, 0, 5);
		if (status != 0) {
			printf("\nFailed to fill sequence in packet !!!");
			return -1;
		}
		
		/*Fill data in Acknowlegment-Reserved Space*/		
		status = fill_packet_data(ack + 16, 0, 14);
		if (status != 0) {
			printf("\nFailed to fill window size in packet !!!");
			return -1;
		}	
		
		/* Acquire lock for modifying sliding window parameters */
		pthread_mutex_lock(&buffer_mutex);
		
		/* Keeps count of Packet Recived by
		   setting Recived Packet Sequence no with 1 */
		received_seq[seq_no] = 1;
		ptr = strncpy(msgprint[seq_no], temp, sizeof(temp));
		if (ptr == NULL) {
			printf("\nError in strcpy !!! Exiting ...\n");
			return -1;
		}
		adver_window--;
		
		/* checks if Packet Arrived is accknowleged */
		if (seq_no == expected_ack) {
			
			/* Got expected acknowledgement */
			
            /* Update  Sequence Number 
			   Depending upon count of Packet Recived */
			/* send cumulative ack if possible */
			while(received_seq[seq_no] == 1) {
				seq_no++;
			}
			expected_ack = seq_no;
			
			/* Client Reciver Buffer Full-Advertisement window Full */
			if(adver_window == 0)	{
				client_window_locked = 1;
				printf("\nClient Reciver Window Buffer locked !!!");
			}
			
			/* Fill sequence no in Acknowlegment to be sent to server */	
			status = fill_packet_data(ack + 5, seq_no, 5);
			if (status != 0) {
				printf("\nFailed to fill ACK in packet !!!");
				return -1;
			}
		} else {
			/*if Packet Arrived is  not acknowleged  */ 
			/* Send duplicate ACK for Fast Retransmit and Recovery*/
			
			/*Fill Last Unacknowlegement in Acknowlegment to be sent to server*/
			status = fill_packet_data(ack + 5, expected_ack, 5);
			if (status != 0) {
				printf("\nFailed to fill ACK in packet !!!");
				return -1;
			}
			printf("\nProcessing duplicate ACK = %d", expected_ack);
		}
        
		/* Fill advertisement window  in Acknowlegment to be sent to reciver */
		status = fill_packet_data(ack + 10, adver_window, 5);
		if (status != 0) {
			printf("\nFailed to fill window size in packet !!!");
			return -1;
		}
			
		if (seq_no == last_packet_seq) {
			
			/* Fill Fin=1 in Acknowlegment to be sent to 
			Indicates Complete File has been Recived from the Server */
			status = fill_packet_data(ack + 15, 1, 1);
			if (status != 0) {
				printf("\nFailed to fill Fin statement in packet !!!");
				return -1;
			}
		} else {
		
			/* Fill Fin=0 in Acknowlegment to be sent to the Server */
			status = fill_packet_data(ack + 15, 0, 1);
			if (status != 0) {
				printf("\nFailed to fill Fin statement in packet !!!");
				return -1;
			}
		}
		
		ack[HEADER_SIZE] = '\0';
		
		/* Release lock so that printer thread resumes */
		pthread_mutex_unlock(&buffer_mutex);
		
		/* Send Acknowlegement that Client has Recieved packets from Server */
		status = send(client_soc, ack, strlen(ack), 0);
		if (status < 0) {
			printf("\nStatus = %d, Data sent incompletely ...", status);
			return -1;
		}
		printf("\nSent ACK = %s", ack);
		
		if(seq_no == last_packet_seq) {
		    
			/* Last Packet has been Recived From the Server */
			printf("\nTotal no of packets Received: %d\n", (seq_no));
			sleep(TIMED_WAIT);
			break;
		}
	}
	pthread_join(printdata, NULL);
	return 0;
}
int main() {
	int						status = 0;
	client_input			client_data;
	struct ifi_info 		*ifi,*ifihead;
    struct sockaddr_in 		*temp_addr, *temp_mask, IP_client, IP_server;
	struct sockaddr_in		subnetaddr1, subnetaddr2, IP_server_conn;
	struct sockaddr_in 		ephemeral_client, ephemeral_server;
	char 					temp_addr1[INET_ADDRSTRLEN];
	char 					temp_addr2[INET_ADDRSTRLEN];
    char 					*subnet1,*subnet2;
	int     				soc_fd, ephemeral_port, ephemeral_soc;
	server_location			server_loc = SERVER_NONE;
	struct sockaddr_in 		client_soc;
	socklen_t				client_soc_len, server_len;
	struct sockaddr_in 		peer_addr;
	int						soc_option, server_ephemeral;
	struct timeval 			timeout;
	char 					filenameack[MAX_SIZE];
	
	status = get_client_data(&client_data);
	if (status != 0) {
		printf("\nInconsistent data in client.in file !!!\nExiting ...\n");
		return 0;
	}
	printf("\nThe client details are:");
	printf("\n=========================================");
	printf("\nCLIENT: server IP address = %s", client_data.server_ip);
	printf("\nCLIENT: server port number = %d", client_data.server_port);
	printf("\nCLIENT: server file name = %s", client_data.file_name);
	printf("\nCLIENT: sliding window size = %d", client_data.sl_window_size);
	printf("\nCLIENT: seed value = %d", client_data.seed_value);
	printf("\nCLIENT: probability value = %f", client_data.prob);
	printf("\nCLIENT: mean time = %d", client_data.mean_time);
	printf("\n=========================================\n");
    
	srand(client_data.seed_value);
	
	status = inet_pton(AF_INET, client_data.server_ip, &(IP_server.sin_addr.s_addr));
	if(status <= 0) {
		printf("\nStatus = %d, Unable to convert presentation to network address!!!",status);
	}
	IP_server.sin_family = AF_INET;
	IP_server.sin_port = htons(client_data.server_port);
	
    /* Function used to get Network mask of tht Client
	   and its Associated IP addresses */
	ifihead = ifi = Get_ifi_info_plus(AF_INET,1); 	
	printf("\nThe interface list obtained on client is:");
	printf("\n=========================================");
	while(ifi != NULL){
		  
		printf("\nInterface name: %s ", ifi->ifi_name);

		temp_addr = (struct sockaddr_in *)ifi->ifi_addr;
		if (temp_addr !=  NULL){
			printf("\nIP address of interface: %s",inet_ntoa(temp_addr->sin_addr));
		}
		
		inet_ntop(AF_INET, &(temp_addr->sin_addr), temp_addr1, INET_ADDRSTRLEN);
		status = strcmp(temp_addr1, client_data.server_ip);	
		if(status == 0) {
		    /*If Server is on Same Host Client Ip address is set to LOOPBACK ADDRESS*/
			printf("\nServer is on same host ...");
			IP_client.sin_family = AF_INET;
			IP_client.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
			IP_server.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
			server_loc = SERVER_SAME_HOST;
			break;
		}
		
		temp_mask = (struct sockaddr_in *)ifi->ifi_ntmaddr;
		if (temp_mask != NULL){
			printf("\nNetwork mask of interface: %s",inet_ntoa(temp_mask->sin_addr));
		}		
		/* Subnet calculation of client Interface */
		subnetaddr1.sin_addr.s_addr = (temp_addr->sin_addr.s_addr) & (temp_mask->sin_addr.s_addr);
		
        /* Subnet calculation of server address */		
		subnetaddr2.sin_addr.s_addr = (IP_server.sin_addr.s_addr) & (temp_mask->sin_addr.s_addr);
		
		inet_ntop(AF_INET, &(subnetaddr1.sin_addr), temp_addr1, INET_ADDRSTRLEN);
		inet_ntop(AF_INET, &(subnetaddr2.sin_addr), temp_addr2, INET_ADDRSTRLEN);

		printf("\nNetwork part of interface: %s",temp_addr1);
		printf("\nNetwork part of server address: %s\n",temp_addr2);
			
		status = strcmp(temp_addr1, temp_addr2);
		/* Checks if Server is on Same Subnet */ 
		if (status == 0) {
		 	printf("\nServer is on same subnet ...");
			IP_client.sin_family = AF_INET;
			IP_client.sin_addr.s_addr = temp_addr->sin_addr.s_addr;
			server_loc = SERVER_SAME_SUBNET;
			break;
		}
		ifi = ifi->ifi_next;									
	}
	
	/* Checks if Server is on Different Subnet */ 
	if (server_loc == SERVER_NONE) {
	    
		printf("\nServer is on different subnet ...");
		IP_client.sin_family = AF_INET;
		IP_client.sin_addr.s_addr = temp_addr->sin_addr.s_addr;
		server_loc = SERVER_DIFF_SUBNET;	
	}
	free_ifi_info_plus(ifihead);
	
	inet_ntop(AF_INET, &(IP_client.sin_addr), temp_addr1, INET_ADDRSTRLEN);
	inet_ntop(AF_INET, &(IP_server.sin_addr), temp_addr2, INET_ADDRSTRLEN);

	printf("\nIP_Client = %s\nIP_Server = %s", temp_addr1, temp_addr2);
	printf("\n=========================================\n");
	
	
    IP_client.sin_port = htons(0);
	
	soc_fd = socket(AF_INET, SOCK_DGRAM, 0);
	if (soc_fd < 0) {
		printf("\nStatus = %d, Unable to create socket on client. Exiting ...\n", soc_fd);
		return 0;
	}
	
	if (server_loc == SERVER_SAME_HOST ||
		server_loc == SERVER_SAME_SUBNET) {
		/* Set Socket Option to SO_DONTROUTE if server is on same host or same Subnet */
		status = setsockopt(soc_fd, SOL_SOCKET, SO_DONTROUTE, &soc_option, sizeof(int));
		if (status < 0) {
			printf("Status = %d, Unable to set socket to DONOTROUTE mode !!! Continuing ...",status);
		}
	} else {
		
		status = setsockopt(soc_fd, SOL_SOCKET, SO_REUSEADDR, &soc_option, sizeof(int));
		if (status < 0) {
			printf("Status = %d, Unable to set socket to REUSE_ADDR mode !!! Continuing ...",status);
		}
	}
	
    /* Bind the client Ip to socket */
	status = bind(soc_fd, (struct sockaddr *)&IP_client, sizeof(IP_client));
	if (status < 0) {
		printf("\nStatus = %d, Unable to bind socket on client!!! Exiting ...\n",status);
		return 0;
	}
	
	/* Get Client details its Ip Adderess and its Ephemeral Port it has bind to from
	 getsockname */
	client_soc_len = sizeof(client_soc);
	status = getsockname(soc_fd, (struct sockaddr *)&client_soc, &client_soc_len);
	if (status < 0) {
		printf("\nStatus = %d, Unable to get socket name on client!!! Exiting ...\n",status);
		return 0;
	}
	inet_ntop(AF_INET, &(client_soc.sin_addr), temp_addr1, INET_ADDRSTRLEN);
	printf("\nIP address from getsockname is: %s", temp_addr1);
	
	ephemeral_port = ntohs(client_soc.sin_port);
    printf("\nEphemeral port from getsockname is: %d", ephemeral_port);
    
	
	ephemeral_soc = socket(AF_INET, SOCK_DGRAM, 0);
	if (ephemeral_soc < 0) {
		printf("\nStatus = %d, Unable to create socket on client. Exiting ...\n", ephemeral_soc);
		return 0;
	}
	
	soc_option = 1;
	status = setsockopt(ephemeral_soc, SOL_SOCKET, SO_REUSEADDR, &soc_option, sizeof(int));
	if (status < 0) {
		printf("Status = %d, Unable to set socket to REUSE_ADDR mode !!! Continuing ...",status);
	}
	
	ephemeral_client.sin_family = AF_INET;
	ephemeral_client.sin_addr.s_addr = IP_client.sin_addr.s_addr;
	ephemeral_client.sin_port = htons(ephemeral_port);
	status = bind(ephemeral_soc, (struct sockaddr *)&ephemeral_client, sizeof(ephemeral_client));
	if (status < 0) {
		printf("\nStatus = %d, Unable to bind socket on client!!! Exiting ...\n",status);
		return 0;
	}
	
	/* Connect Client to the Server */
	status = connect(ephemeral_soc, (struct sockaddr *)&IP_server, sizeof(IP_server));
	if (status < 0) {
		printf("\nStatus = %d, Unable to connect ephemeral port to server !!! Exiting ...\n");
		return 0;
	}

	/* Get Server details its Ip Adderess and its Ephemeral Port it has bind to from
	 getpeername */
	client_soc_len = sizeof(client_soc);
	status = getpeername(ephemeral_soc, (struct sockaddr *)&peer_addr, &client_soc_len);
	if (status != 0) {
		printf("\nStatus = %d, Unable to get peer name on server !!! Exiting ...\n");
		return 0;
	}
	inet_ntop(AF_INET, &(peer_addr.sin_addr), temp_addr1, INET_ADDRSTRLEN);
	printf("\nClient connected to %s on port %d", temp_addr1, ntohs(peer_addr.sin_port));
	
	/* ARQ mechanism for resending File name if no ACK recived */	
	while(1){
	    /* Send File Name to be Reterived From server */ 
		status = send(ephemeral_soc, client_data.file_name, strlen(client_data.file_name),0);
				 //(struct sockaddr *)&IP_server, sizeof(IP_server));
		if (status < 0) {
			printf("\nStatus = %d, Data sent incompletely ...", status);
		}
		/* Check if File Name Receive Acknowledment has been sent by the server
		   other wise resend File name as Time out has occured */ 
		if(arq_sendrecv(ephemeral_soc,filenameack)==1){
		
			server_len = sizeof(struct sockaddr_in);
			/* Receive Server Emphemeral Port and other details */ 
	        status = recvfrom( ephemeral_soc, temp_addr1, MAX_SIZE, 0,
								(struct sockaddr *)&ephemeral_server, &server_len);
	        if (status < 0) {
				printf("\nStatus = %d, Socket read error in server!!! Exiting ...\n");
				return 0;
	        }
			temp_addr1[status] = '\0'; 
        
			/* Client sends an Acknowledgement that it has received 
     		 Server Emphemeral Port and other details */     			
			status = send(ephemeral_soc, "ACK", 3,0);
			if (status < 0) {
				printf("\nStatus = %d, Data sent incompletely ...", status);
			}
			break;
		} else {
			printf("\nTime Out:Unable to send File name to Server");
            printf("\n Sending File name  again to the Server"); 			
			continue;
		}	
	}
	
	/* Reconnect to server's connection socket */
	server_ephemeral = atoi(temp_addr1);
	printf("\nClient reconnecting to server's connection socket %d", server_ephemeral);
	IP_server_conn.sin_addr.s_addr = IP_server.sin_addr.s_addr;
	IP_server_conn.sin_family = AF_INET;
	IP_server_conn.sin_port = htons(server_ephemeral);
	status = connect(ephemeral_soc, (struct sockaddr *)&IP_server_conn, sizeof(IP_server_conn));
	if (status < 0) {
		printf("\nStatus = %d, Unable to connect ephemeral port to server !!! Exiting ...\n");
		return 0;
	}
	
	/* Receive File From the server */
	status = receive_file(ephemeral_soc, client_data.sl_window_size,
							(float)client_data.prob, client_data.mean_time);
	if (status != 0) {
		printf("\nFile reception failed !!!\n");
	}
	return 0;
}

