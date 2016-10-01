#include <stdio.h>
#include <stdlib.h>
#include "unpifiplus.h"
#include "unp.h"
extern struct ifi_info *Get_ifi_info_plus(int family, int doaliases);
extern void free_ifi_info_plus(struct ifi_info *ifihead);

#define	MAX_SIZE		1000
#define	DATAGRAM_SIZE	512
#define	HEADER_SIZE		30
#define	PAYLOAD_SIZE	DATAGRAM_SIZE - HEADER_SIZE
#define	MAX_PACKETS		2000
#define	DUP_ACK_THRESH	3

#define RTT_RXTMIN      1000    /* min retransmit timeout value, in Milliseconds */
#define RTT_RXTMAX      3000    /* max retransmit timeout value, in Milliseconds */
#define RTT_MAXNREXMT   12    	/* max # times to retransmit */
#define RTT_RTOCALC(ptr) ((ptr)->rtt_srtt + (4*(ptr->rtt_rttvar)))

/* Structure to hold server operating parameters */
typedef struct server_input_ {
	int		port_no;
	int		sl_window_size;
} server_input;

/* Structure Defined to store details of Socket */
typedef struct socket_data{
    int 				soc_fd;
	struct sockaddr_in  ip_addr;
	struct sockaddr_in  network_mask;
	struct sockaddr_in  subnet_addr;
}sockdata;

/* Enum for packet status */
typedef enum packet_status_ {
	PACKET_NOT_SENT,
	PACK_SENT_NO_ACK,
	PACKET_SENT_ACK_RECV
}packet_status;

/* Enum for client location */
typedef enum client_location_ {
	CLIENT_NONE,
	CLIENT_SAME_SUBNET,
	CLIENT_SAME_HOST,
	CLIENT_DIFF_SUBNET
}client_location;

/* Structure Defined to Calculate RTT details of Socket */
struct rtt_info {
    long   	rtt_rtt;            /* most recent measured RTT, in Milliseconds */
    long   	rtt_srtt;           /* smoothed RTT estimator, in Milliseconds */
    long   	rtt_rttvar;         /* smoothed mean deviation, in Milliseconds */
    long   	rtt_rto;            /* current RTO to use, in Milliseconds*/
    long   	rtt_nrexmt;         /* # Retransmission Counter ...... */
    uint32_t 	rtt_base;          /* # Millisec since 1/1/1970 at start */
};

/* Limit value of RTT to Max 3000 milliseconds and Min  1000 milliseconds */
static long
rtt_minmaxcalc(long rto) {	
	if (rto < RTT_RXTMIN)
		rto = RTT_RXTMIN;
	else if (rto > RTT_RXTMAX)
		rto = RTT_RXTMAX;
	return (rto);
}

/* Intialize the timer */
void
rtt_initialcalc(struct rtt_info *ptr) {
	struct timeval tv;

    Gettimeofday(&tv, NULL);
    ptr->rtt_base = tv.tv_sec*1000;   /* # sec since 1/1/1970 at start */
	
    ptr->rtt_rtt = 0;
    ptr->rtt_srtt = 0;
    ptr->rtt_rttvar = 750;
    ptr->rtt_rto = rtt_minmaxcalc(RTT_RTOCALC(ptr));
    //printf("\n Initial RTO value:%i", ptr->rtt_rto );  
}

/* Convert time from seconds to millisconds */
uint32_t 
rtt_timecalc(struct rtt_info *ptr)	{
	uint32_t ts;
	struct timeval tv;
	Gettimeofday(&tv, NULL);
	ts = (tv.tv_sec*1000 - ptr->rtt_base) + (tv. tv_usec / 1000);
	return (ts);
}


/* Time out after 12 retransmissions*/
int
rtt_timeoutcalc(struct rtt_info *ptr){
	ptr->rtt_rto *= 2;          /* next RTO */
	ptr->rtt_rto = rtt_minmaxcalc(ptr->rtt_rto);
	//printf("\nRTO value:%i", ptr->rtt_rto );
	if (++ptr->rtt_nrexmt > RTT_MAXNREXMT)
		return (-1);            /* time to give up for this packet */
	return (0);
}

/* Calculations of new rtt when acknowledment arrives before timeout */
void
rtt_newcalc(struct rtt_info *ptr, uint32_t ms)	{
	long  delta;
	ptr->rtt_rtt = ms; /* measured RTT in seconds */
	delta = ptr->rtt_rtt - ptr->rtt_srtt;
	ptr->rtt_srtt += delta / 8; /* g = 1/8 */
	if (delta < 0)
		delta = -delta;         /* |delta| */
	ptr->rtt_rttvar += (delta - ptr->rtt_rttvar) / 4; /* h = 1/4 */
	ptr->rtt_rto = rtt_minmaxcalc(RTT_RTOCALC(ptr));
	//printf("\nRTO value:-%i", ptr->rtt_rto );
}

/* Reset retransmit counter */
void
rtt_transmitcounter(struct rtt_info *ptr)	{
	ptr->rtt_nrexmt = 0;
}

/*Minimum Calculation of Reciver window, Congestion Window, Window Size*/
int 
minf( int rwnd,
	  int cwnd,	
	  int window_size)	{
	if(rwnd <= cwnd && rwnd <= window_size)	{
		return(rwnd);
	} else if(cwnd <= rwnd && cwnd <= window_size)	{
		return(cwnd);
	} else {
		return(window_size);
	}
} 
/* ARQ Mechanism Function */
int arq_sendrecvmechanism(int connfd,
						 char  *buffer
						 ){  
   	int    status;
	struct timeval 		timeout;
	struct sockaddr_in 	IP;
	socklen_t           IP_len;
	timeout.tv_sec = 2;
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
/* Read details from client.in */
int
get_server_data(struct server_input_	*server_data) {
	char	file_line[MAX_SIZE], *ptr_ret = NULL;
	FILE	*fp = NULL;
	int	no_chars = 0, status = 0;
	
	fp = fopen("server.in", "r");
	if (fp == NULL) {
		printf("\nFile not present in code directory !!!\nExiting ...\n");
		return -1;
	}
	/* read well-known Port of Server from File server.in*/
	ptr_ret = fgets(file_line, MAX_SIZE, fp);
	if (ptr_ret == NULL) {
		printf("\nUnexpected end of file !!! Check server.in ...\n");
		return -1;
	}
	status = atoi(file_line);
	if (status < 1 || status > 65535) {
		printf("\nPort number = %d is out of range !!!\nExiting ...\n",status);
		return -1;
	}
	server_data->port_no = status;
	
	/* Read server Window Size from File server.in */
	ptr_ret = fgets(file_line, MAX_SIZE, fp);
	if (ptr_ret == NULL) {
		printf("\nUnexpected end of file !!!\nCheck server.in ...\n");
		return -1;
	}
	status = atoi(file_line);
	if (status < 0) {
		printf("\nSliding window size should be more than 0 !!!\nExiting ...\n");
		return -1;
	}
	server_data->sl_window_size = status;
	
	ptr_ret = fgets(file_line, MAX_SIZE, fp);
	if (ptr_ret != NULL) {
		printf("\nExtra data present in server.in file !!!\nExiting ...\n");
		return -1;
	}
	return 0;
}

/* Function to count the digits in a number */
int
count_digits(int	number) {
	int		count = 0;
	while(number != 0) {
		number = number/10;
		count++;
	}
	return count;
}

/* Utility to Fill data in Packet Header */
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
/* Utility Function to retrieve Details from Acknowledment Header */
int
get_packet_data(char	*packet,
		int data_size,
		int *ret) {
	char	number[30];
	int		iter, ans = 0;
	
	for (iter = 0; iter < data_size; iter++) {
		number[iter] = packet[iter];
	}
	number[iter] = '\0';
	*ret = atoi(number);
	return 0;
}

/* Handling of File Transfer from server child */
int
initiate_file_transfer(	int conn_soc,
			char	*file_name,
			int window_size ) {
	char	packet[MAX_PACKETS][520], temp;
	FILE	*fp = NULL;
	int	no_chars = 0, file_end = 0;
	int	sequence_no = 0, status, iter;
	socklen_t len;
	char 	msg[MAX_SIZE];
	struct sockaddr_in  recv_struct;
	int ack_no, send_ptr, send_size, cwnd;
	int window_start, window_end;
	int received_seq[MAX_PACKETS];
	int fin, dup_ack_count, last_ack_no;
	struct rtt_info		rtt_data;
	struct timeval 		timeout;
	uint32_t		time_start, time_end;
	int			ssthresh, rwnd = 1;
	
	fp = fopen(file_name, "r");
	
	if (fp == NULL) {
		status = send(conn_soc, '\0', 0, 0);
		if (status < 0) {
			printf("\nStatus = %d, Data sent incompletely ...", status);
			return -1;
		}
		printf("\nFile not present on server !!!\nExiting ...\n");
		return -1;
	}

	/* Creation of packets Header for data in File */
	sequence_no = 0;
	while(file_end == 0) {
		/* Fill Packet Header with Sequence number- 
		   First five locations 0-4 are stored for Seq_no */
		status = fill_packet_data(packet[sequence_no], sequence_no, 5);
		if(status != 0) {
			printf("\nError in filling sequence number !!!");
		}
		
		/* Fill Packet Header with Acknowlegment Details- 
		   locations 5 to 9 are reserved for Acknowlegment */	
		status = fill_packet_data(packet[sequence_no] + 5, 0, 5);
		if(status != 0) {
			printf("\nError in filling ACK number !!!");
		}
		
		/* Fill Packet Header with Window Size- 
		   locations 10 to 14 are reserved for Window Size  */	
		status = fill_packet_data(packet[sequence_no] + 10, window_size, 5);
		if(status != 0) {
			printf("\nError in filling window size !!!");
		}
		
		/* Fill Packet Header with Fin- 
		   locations 15 is reserved for Fin statement  */	
		status = fill_packet_data(packet[sequence_no] + 15, 0, 1);
		if(status != 0) {
			printf("\nError in fin statement size !!!");
		}
		
		/* Fill Packet Header with data- 
		   locations 16- 30  is Space  reserved   */		
		status = fill_packet_data(packet[sequence_no] + 16, 0, 14);
		if(status != 0) {
			printf("\nError in filling window size !!!");
		}
		
		/* Fill Packet Header with data from File - 
	       locations 30- 512  is Space  reserved for filling data from File  */		
		for (no_chars = HEADER_SIZE; no_chars < DATAGRAM_SIZE; no_chars++) {
			temp = fgetc(fp);
			if (temp == EOF) {
				packet[sequence_no][no_chars] = '\0';		
				status = fill_packet_data(packet[sequence_no] + 15, 1, 1);
				if(status != 0) {
					printf("\nError in fin statement size !!!");
				}
				file_end = 1;
				break;
			} else {
				packet[sequence_no][no_chars] = temp;
			}
		}
		packet[sequence_no][no_chars] = '\0';
		sequence_no++;
	}
	printf("\nNo of packets to be transferred = %d", sequence_no);
	
	
	/* Implementation of sliding window */
	send_ptr = 0;
	window_start = 0;
	window_end = window_size;
	send_size = 1;
	cwnd=1;							//set Congestion Window =1 initially
	ssthresh = window_size/2;		//set SSThreshold initially
	
	/* Initially calculate RTT */
	rtt_initialcalc(&rtt_data);
	
	/*set Transmission counter To zero*/
	rtt_transmitcounter(&rtt_data);
	
	for (iter = 0; iter < MAX_PACKETS; iter++) {
		received_seq[iter] = PACKET_NOT_SENT;
	}
	
	printf("\n-----------------------------------");
	for(;;) {
		fd_set 		read_fd;	
		printf("\n-----------------------------------");
		printf("\nCurrent send size = %d\tCurrent window_start = %d", send_size, window_start);
		/* Initialization of timeout structure when packet is sent */
		time_start = rtt_timecalc(&rtt_data);
		for(; send_ptr < window_start + send_size;) {
			
			if (send_ptr >= sequence_no)
				break;
			
			if (received_seq[send_ptr] == PACKET_NOT_SENT) {
			
				/* Send Packet to Client */
				status = send(conn_soc, packet[send_ptr], strlen(packet[send_ptr]), 0);
				if (status < 0) {
					printf("\nStatus = %d, Data packet %d sent incompletely ...", send_ptr, status);
					return -1;
				}
				received_seq[send_ptr] = PACK_SENT_NO_ACK;
				printf("\nSent packet = %d", send_ptr);
			}		
			send_ptr++;
		}
		
		/* Set Time out feilds for select */
		timeout.tv_sec = rtt_data.rtt_rto/1000;
		timeout.tv_usec = (rtt_data.rtt_rto - timeout.tv_sec*1000)*1000;
		
		FD_ZERO(&read_fd);
		FD_SET(conn_soc, &read_fd);
		
		/* This is re-transmissions timer */
		status = select(conn_soc + 1, &read_fd, NULL, NULL, &timeout);
		if (status < 0) {
			printf("\nStatus = %d, Unable to monitor sockets !!! Exiting ...",status);
			return 0;
		}
		/* Acknowledgement arrived on socket */
		if (FD_ISSET(conn_soc, &read_fd)) {
			/* Calculate new timeout after packet reception */
			time_end = rtt_timecalc(&rtt_data);
			rtt_newcalc(&rtt_data, time_end - time_start);
			rtt_transmitcounter(&rtt_data);
			
			len = sizeof(struct sockaddr_in);
			status = recvfrom(conn_soc, msg, MAX_SIZE, 0,
							(struct sockaddr *)&recv_struct, &len);
			if (status < 0) {
				printf("\nStatus = %d, Socket read error in server!!! Exiting ...\n", status);
				return 0;
			}		
			msg[status] = '\0';
			printf("\nACK received is %s", msg);
			
			/* Retrieve Sequence number from From Acknowlegement Header */	
			status = get_packet_data(msg + 5, 5, &ack_no);
			if (status != 0) {
				printf("\nUnable to get ack from received packet ...");
			}
			
		    /* Retrieve Fin from From Acknowlegement Header */	
			status = get_packet_data(msg + 15, 1, &fin);
		    if (status != 0) {
				printf("\nFailed to read packet from server !!!");
				return -1;
		    }
            
 			if(fin == 1)	{
			 /* Terminate server Child if finish obtained from client */
				printf("\nTotal no of Packets Transfered: %d",sequence_no);
				printf("\nCompleted File transfered to the Client.....\n");
				break;
			}
			
			/* Retrieve Reciver Window from From Acknowlegement Header */
			status = get_packet_data(msg + 10, 5, &rwnd);
			if (status != 0) {
				printf("\nUnable to get rwnd from received packet ...");
			}
			
			/* Checks if Packet data is Acknowleged */
			if (window_start < ack_no) {
				/* TCP slow start and congestion avoidance */
				/* Congestion window increases exponentially before ssthresh
				   and linearly afer that */
				if (cwnd > ssthresh)
					cwnd = cwnd + 1;
				else
					cwnd = cwnd*2;
					
				while(window_start < ack_no && received_seq[window_start] == PACK_SENT_NO_ACK) {
					received_seq[window_start] = PACKET_SENT_ACK_RECV;
					/* Shift Sliding Window if Packet Data is Acknowleged */
					window_start++;
					window_end++;
					dup_ack_count = 1;
					last_ack_no = ack_no;
				}
				
			} else if (window_start == ack_no) {	
			
				/* Implementation of Fast Retransmition */
				if (ack_no == last_ack_no)
					dup_ack_count++;
				printf("\nSending duplicate ACK %d\nNumber of duplicate ACK's = %d", last_ack_no, dup_ack_count);
				
				if (dup_ack_count >= DUP_ACK_THRESH) {
					/* Checks if Duplicate ACK count exceeds Duplicate Threshold */
				    /* and resend it if threshold is breached */
					status = send(conn_soc, packet[window_start], strlen(packet[window_start]), 0);
					if (status < 0) {
						printf("\nStatus = %d, Data packet %d sent incompletely ...", send_ptr, status);
						return -1;
					}
					printf("\nRE transmitted packet %d", window_start);
					dup_ack_count = 0;
				}
			}
			
			if (rwnd == 0) {
			   /* Reciver Window from client is Full so we stip sending more data */
				printf("\nReceiver window full !!! Not sending any more data ...");
			}
		} else {
			
			/* Time out Occurs only when Acknowlegment not sent by client */
			printf("\nTCP re-transmission timer timed out !!!");
			
			/* Calculate new timeout for time out situation */
			status = rtt_timeoutcalc(&rtt_data);
			if (status == -1) {
				printf("\nMaximum number of re-transmissions exceeded !!! Aborting !!!");
				return -1;
			}
			
			/* Mark packet for re-transmission */
			send_ptr = window_start;
			received_seq[window_start] = PACKET_NOT_SENT;
			
			/* set SSthreshold to Congestion window/2  as time out has occured */
			/* set Congestion window to 1 as time out has occured */
			ssthresh = cwnd/2;
			cwnd = 1;
			
		}
		printf("\nReceived Window Size = %d\tCongestion Window Size = %d",rwnd, cwnd);
		printf("\tServer Window Size = %d", window_size);
		
		/* Combine the effect of congestion avoidance, TCP slow start and FRR
		   to decide on the send size for server */
		send_size = minf(rwnd, cwnd, window_size);
	}
	printf("\n-----------------------------------");
	printf("\n-----------------------------------");
	return 0;
}

/* Handling Of server Child */
int
handle_server_child( int listen_soc,
		     sockdata 	*server_data,
		     struct sockaddr_in	*client_IP,
		     char *file_name,
		     int window_size) {
	int conn_soc, status;
	struct sockaddr_in	server_addr;
	struct sockaddr_in 	server_soc;
	struct sockaddr_in	ephemeral_cli_ack;
	socklen_		server_len;
	char 			temp[INET_ADDRSTRLEN];
	char	                tempack[MAX_SIZE];
	int			ephemeral_port;
	conn_soc = socket(AF_INET,SOCK_DGRAM, 0);		
	if (conn_soc < 0) {
		printf("\nStatus = %d, Unable to create socket !!!",conn_soc);
		return 0;
	}
	
	/*Bind IP of Server To Connection Socket*/
	server_addr.sin_port = htons(0);
	server_addr.sin_family = AF_INET;
	server_addr.sin_addr.s_addr = server_data->ip_addr.sin_addr.s_addr;
	status = bind(conn_soc, (struct sockaddr *)&server_addr, sizeof(server_addr));
	if (status < 0) {
		printf("\nStatus = %d, Unable to bind socket on client!!! Exiting ...\n",status);
		return 0;
	}
	
	/* Get Server details its Ip Adderess and its Ephemeral Port 
	   it has bind to from getsockname*/
	server_len = sizeof(struct sockaddr_in);
	status = getsockname(conn_soc, (struct sockaddr *)&server_soc, &server_len);
	if (status < 0) {
		printf("\nStatus = %d, Unable to get socket name on client!!! Exiting ...\n",status);
		return 0;
	}
	inet_ntop(AF_INET, &(server_soc.sin_addr), temp, INET_ADDRSTRLEN);
	printf("\nIP address of server from getsockname is: %s", temp);
	
	ephemeral_port = ntohs(server_soc.sin_port);
    printf("\nEphemeral port on server from getsockname is: %d", ephemeral_port);
	
    /* Server Connects to the Client */
	status = connect(conn_soc, (struct sockaddr *)client_IP, sizeof(*client_IP));
	if (status < 0) {
		printf("\nStatus = %d, Unable to connect ephemeral port to server !!! Exiting ...\n");
		return 0;
	}
	
	snprintf(temp, 10, "%d", ephemeral_port);
	/* ARQ mechanism for resending server port if no ACK is recived */	
	while(1){
	    /* Send Emphemeral port details of server to client on Listening Socket */     
		status = sendto(listen_soc, temp, strlen(temp),0,
					(struct sockaddr *)client_IP, sizeof(*client_IP));
		if (status < 0){
			printf("\nStatus = %d, Data sent incompletely ...", status);
		}
		
		/* Check if  Acknowledment has been sent by the client
		   other wise resend port details as Time out has occured */ 
		if(arq_sendrecvmechanism(listen_soc,tempack) == 0)	{
			printf("\nTime Out:Unable to send port details to Client");
			printf("\n Sending Port details to the Client Again"); 
			continue;
		} else {
			printf("\nClosing LISTEN socket after receiveing acknowledgement %s", tempack);
			break;
		}	
	
	}
	
	/* Close Listening Socket */
	status = close(listen_soc);
	if (status != 0) {
		printf("\nStatus = %d, Unable to close LISTEN socket !!!",status);
	}
	
	/* Initiate File Transfer */
	status = initiate_file_transfer(conn_soc, file_name, window_size);
	if (status != 0) {
		printf("\nFile transfer failed !!! Exiting ...");
	}
	
	/* Close connection socket after file transfer */
	status = close(conn_soc);
	if (status != 0) {
		printf("\nStatus = %d, Unable to close socket on server side !!!",status);
	}
	return 0;
}
int main() {
	int					status = 0;
	server_input		server_data;
    	struct ifi_info 	*ifi, *ifihead;
    	struct sockaddr_in 	*temp_addr, server_addr, client_addr;
    	sockdata		sockdetails[MAX_SIZE];
	const int		rstsock = 1;
	int 			soc_fd, max_fd, iter, sock_flag;
	char 			temp_addr1[INET_ADDRSTRLEN];
	int 			len = 0;
	char			msg[MAX_SIZE],ipaddr[MAX_SIZE];
	int 			client_len;
	char			file_name[MAX_SIZE];
	pid_t          		 pid;
	int 			listen_soc;
	struct sockaddr_in	subnetaddr1, subnetaddr2;
	char 			temp_addr2[INET_ADDRSTRLEN];
	char 			temp_addr3[INET_ADDRSTRLEN];
	client_location		client_loc = CLIENT_NONE;
	
	/* Read the server.in file to get operating parameters */
	status = get_server_data(&server_data);
	if (status != 0) {
		printf("\nInconsistent data in server.in file !!!Exiting ...\n");
		return 0;
	}
	printf("\nPort number in integer format is %d", server_data.port_no);
	printf("\nSliding window size in integer format is %d\n", server_data.sl_window_size);
	
	/* Function used to get Network mask of tht server and its Associated IP addresses */
	ifihead = ifi = Get_ifi_info_plus(AF_INET,1); 	
	
	while(ifi != NULL){
	    soc_fd = socket(AF_INET,SOCK_DGRAM, 0);		
		if (soc_fd < 0) {
			printf("\nStatus = %d, Unable to create socket !!!",soc_fd);
			return 0;
		}				 
		
		status = setsockopt(soc_fd, SOL_SOCKET, SO_REUSEADDR, &rstsock, sizeof(rstsock));
        if (status < 0) {
			printf("Status = %d, Unable to set  port to REUSE_ADDR mode !!! ",status);
	        return 0;   
		}
				
		printf("\nInterface name: %s ", ifi->ifi_name);
	
		bzero(&server_addr, sizeof(server_addr));
  		server_addr.sin_family = AF_INET;
        server_addr.sin_port = htons(server_data.port_no);
		
		temp_addr = (struct sockaddr_in *)ifi->ifi_addr;
        /* Store IP Address in Stucture of Server Socket details
		   of Associated Ip Address */
		sockdetails[len].ip_addr.sin_addr.s_addr =
			temp_addr->sin_addr.s_addr;
		printf("\nIP address of interface: %s",inet_ntoa(sockdetails[len].ip_addr.sin_addr));
		server_addr.sin_addr.s_addr = sockdetails[len].ip_addr.sin_addr.s_addr;
		
		/* bind Associated IPaddress of server to socket */
		status = bind(soc_fd, (struct sockaddr *)&server_addr, sizeof(server_addr));
		if (status < 0) {
			printf("\nStatus = %d, Unable to bind socket on server !!!",status);
			return 0;			
	    }
		
		/* Store Details of socket in Stucture of Server Socket
		   details of Associated Ip Address */
		sockdetails[len].soc_fd = soc_fd;	
		temp_addr = (struct sockaddr_in *)ifi->ifi_ntmaddr;
		
		/* Store Network Address in 
		Stucture of Server Socket details of Associated Ip Address */
		sockdetails[len].network_mask.sin_addr.s_addr =
			temp_addr->sin_addr.s_addr;
		printf("\nNetwork mask of interface: %s",inet_ntoa(sockdetails[len].network_mask.sin_addr));
		
        /* Subnet calculation of Server Interface */ 
		/* Store Subnet in Stucture of Server Socket details of
		   Associated Ip Address */
		(sockdetails[len].subnet_addr.sin_addr.s_addr) =
			((sockdetails[len].ip_addr.sin_addr.s_addr)&(sockdetails[len].network_mask.sin_addr.s_addr));
		printf("\nSubnet address of interface: %s\n",inet_ntoa((sockdetails[len].subnet_addr).sin_addr));
						
		ifi = ifi->ifi_next;					
		len++;
	}
	free_ifi_info_plus(ifihead);
	
	while(1) {
		fd_set 		read_fd;
		FD_ZERO(&read_fd);
		max_fd = -1;
		for(iter = 0; iter < len; iter++) {
			FD_SET(sockdetails[iter].soc_fd, &read_fd);
			if (sockdetails[iter].soc_fd > max_fd)
				max_fd = sockdetails[iter].soc_fd;
		}
		
		status = select(max_fd + 1, &read_fd, NULL, NULL, NULL);
		if (status < 0) {
			printf("\nStatus = %d, Unable to monitor sockets !!! Exiting ...\n",status);
			return 0;
		}
		
		for(iter = 0; iter < len; iter++) {
			if (FD_ISSET(sockdetails[iter].soc_fd, &read_fd)) {
				printf("\nReceived message on interface %s", 
						inet_ntoa(sockdetails[iter].ip_addr.sin_addr));
				
				client_len = sizeof(struct sockaddr_in);
				/* Receive filename to be transfered to client */
				status = recvfrom(sockdetails[iter].soc_fd, file_name, MAX_SIZE, 0,
								  (struct sockaddr *)&client_addr, &client_len);
				if (status < 0) {
					printf("\nStatus = %d, Socket read error in server!!! Exiting ...\n");
					return 0;
				}
				
				inet_ntop(AF_INET, &(client_addr.sin_addr), msg, INET_ADDRSTRLEN);
				printf("\nServer got request on %s on port %d", msg, ntohs(client_addr.sin_port));
				
				file_name[status] = '\0';
				printf("\nThe file to be transferred is %s", file_name);
				sock_flag = iter;
				
				/* Send Filename Acknowledgement that File name has been received from client */
				status = sendto(sockdetails[iter].soc_fd, "File_Name_Recived",20,0,
							(struct sockaddr *)&client_addr, sizeof(client_addr));
				if (status < 0) {
					printf("\nStatus = %d, Data sent incompletely ...", status);
					return -1;
				}
				break;
			}
		}
		
		/* Create server child to handle file transfer for this client */
		pid = fork();
		if (pid == 0) {
			/* === Child process === */		
			for(iter = 0;iter < len;iter++){ 
				if(iter == sock_flag) {
	                /* Store Listening Socket on which File name is Recived*/			
					listen_soc = sockdetails[iter].soc_fd; 	
				} else {
                    /* Close Sockets of other Associated IP Address */				
					close(sockdetails[iter].soc_fd);
				}
			}
			
			status = strcmp(msg,"127.0.0.1");
			if(status == 0)	{
	            
				/* Checks if Client  is on Same Host i.e 127.0.0.1 */
				client_loc = CLIENT_SAME_HOST;
                printf("\nClient is on Same Host...");
				
				/* Set Socket Option to SO_DONTROUTE if Client is on same host */
				status = setsockopt(listen_soc, SOL_SOCKET, SO_DONTROUTE, &rstsock, sizeof(rstsock));
				if (status < 0) {
					printf("Status = %d, Unable to set socket to DONOTROUTE mode !!! Continuing ...",status);
					return 0;   
				} else {
					printf("\nSocket set to DONOTROUTE mode");
				}
			}
			
			inet_ntop(AF_INET, &(sockdetails[sock_flag].ip_addr.sin_addr), ipaddr, INET_ADDRSTRLEN);
			//printf("\nServer address:%s ",ipaddr );
			
			status = strcmp(ipaddr,"127.0.0.1");
			if(status != 0){
			/* Checks if Client is on not Same Host i.e 127.0.0.1 */
				for(iter = 0; iter < len; iter++){ 
				    /* Subnet calculation of client IP Address */
					subnetaddr1.sin_addr.s_addr = 
							(client_addr.sin_addr.s_addr) & 
							(sockdetails[iter].network_mask.sin_addr.s_addr);
							
					/* Subnet calculation of Server IP Address */
					subnetaddr2.sin_addr.s_addr = 
							(sockdetails[iter].ip_addr.sin_addr.s_addr) &
							(sockdetails[iter].network_mask.sin_addr.s_addr);
							
				    inet_ntop(AF_INET, &(subnetaddr1.sin_addr), temp_addr2, INET_ADDRSTRLEN);
					inet_ntop(AF_INET, &(subnetaddr2.sin_addr), temp_addr3, INET_ADDRSTRLEN);
					/*
					printf("\nNetwork part of interface: %s",temp_addr3);
					printf("\nNetwork part of client address: %s",temp_addr2);
					*/
					
					status = strcmp(temp_addr2, temp_addr3);
					if (status == 0) {
						/*Checks if Client  is on  Same Subnet*/
						printf("\nClient is on same subnet i.e it is Local ...");
						client_loc = CLIENT_SAME_SUBNET;
						
						/* Set Socket Option to SO_DONTROUTE if Client is on Same Subnet */
						status = setsockopt(listen_soc, SOL_SOCKET, SO_DONTROUTE, &rstsock, sizeof(rstsock));
						if (status < 0) {
							printf("Status = %d, Unable to set socket to DONOTROUTE mode !!! Continuing ...",status);
							return 0;   
						} else {
							printf("\nSocket set to DONOTROUTE mode");
						}
						break;
					} else {
						client_loc = CLIENT_DIFF_SUBNET;
					}
				}
			}

            if(client_loc == CLIENT_DIFF_SUBNET){
				/* Checks if Client  is not on Same Subnet */ 
				printf("\nClient is on different subnet i.e it is not Local  ...");
			}
			/* Handle Server Child for File Transfer */
			status = handle_server_child(listen_soc, &sockdetails[sock_flag],
										&client_addr, file_name,
										server_data.sl_window_size);
			return 0;
		} else if (pid > 0) {
			/* === Parent process === */
			/* Return to select */
		} else {
			printf("\nStatus = %d, fork() failed !!!",status);
		}
	}
	return 0;	
}

