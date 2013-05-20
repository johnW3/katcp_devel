/* (c) 2010,2011 SKA SA */
/* Released under the GNU GPLv3 - see COPYING */

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>
#include <unistd.h>
#include <fcntl.h>
#include <sysexits.h>

#include <sys/select.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>

#include <arpa/inet.h>

#include <katcp.h>
#include <katpriv.h>

#define DMON_POLL_INTERVAL        1000    /* default poll interval */
#define DMON_POLL_MIN              100    /* minimum (in ms) */

#define DMON_MODULE_NAME         "dmon"

#define UDP_MAX_PACKET 		9728

#define UDP_MAGIC                   0xDEADBEEF
/************************************************************/


int rw_flag = 0;

struct udp_state
{
	unsigned int u_magic;
	unsigned int u_fd;
};

struct udp_message
{
	uint32_t u_id;
	uint32_t u_address;
	uint32_t u_length;
	uint32_t u_data[UDP_MAX_PACKET];
}__attribute__ ((packed));

/*****************************************************************************/
void destroy_udp(struct katcp_dispatch *d, struct udp_state *ud)
{
	if(ud == NULL){
		return;
	}

	if(ud->u_magic != UDP_MAGIC){
		log_message_katcp(d, KATCP_LEVEL_FATAL, DMON_MODULE_NAME, "bad magic on udp state");
	}

	if(ud->u_fd >= 0){
		close(ud->u_fd);
		ud->u_fd = (-1);
	}

	free(ud);
}

struct udp_state *create_udp(struct katcp_dispatch *d)
{
	struct udp_state *ud;

	ud = malloc(sizeof(struct udp_state));
	if(ud == NULL){
		return NULL;
	}

	ud->u_magic = UDP_MAGIC;
	ud->u_fd = (-1);

	return ud;
}

/*****************************************************************************/
#if 0
int connect_udp(struct katcp_dispatch *d, struct udp_state *ud, int port)
{

	int fd;
	struct sockaddr_in sa;

	/* Prepare UDP socket */
	memset(&sa, 0, sizeof sa);
	sa.sin_family = AF_INET;
	sa.sin_addr.s_addr = htonl(INADDR_ANY);
	sa.sin_port = htons(port);

	fd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
	if(fd < 0){
		fprintf(stderr, "unable to create udp socket:\n ");
		log_message_katcp(d, KATCP_LEVEL_ERROR, DMON_MODULE_NAME, "unable to create udp socket: %s", strerror(errno));
		return -1;
	}

	if(bind(fd, (struct sockaddr *)&sa, sizeof(struct sockaddr_in)) < 0){
		fprintf(stderr, "unable to bind udp:\n ");
		log_message_katcp(d, KATCP_LEVEL_ERROR, DMON_MODULE_NAME, "unable to connect to udp: %s", strerror(errno));
		close(fd);
		fd = (-1);
		return -1;
	}
	ud->u_fd = fd;
	log_message_katcp(d, KATCP_LEVEL_INFO, DMON_MODULE_NAME, "digitiser destination,%s:%d", ip_addr, port);
	fprintf(stderr, "digitiser destination,%s:%d\n", ip_addr, port);

	return 0;
}
#endif
/*****************************************************************************/
int send_udp(struct katcp_dispatch *d, struct udp_state *ud, char *ip_addr, int port, uint32_t address, uint32_t length)
{
	struct udp_message buffer, *uv;
	int wr;
	struct sockaddr_in sa;

	/* Prepare UDP socket */
	memset(&sa, 0, sizeof sa);
	sa.sin_family = AF_INET;
	sa.sin_addr.s_addr = inet_addr(ip_addr);
	sa.sin_port = htons(port);

	uv = &buffer;

	uv->u_id 	= htons(0);
	uv->u_address = htons((address & 0x7FFFFFFF) | (rw_flag << 31));

	uv->u_length 	= htons(length);

  //wr = send(ud->u_fd, uv, sizeof(uv), MSG_NOSIGNAL);
  wr = sendto(ud->u_fd, uv, sizeof(uv), 0,(struct sockaddr *)&sa, sizeof(struct sockaddr_in));
  if(wr < 0){
    switch(errno){
      case EAGAIN :
      case EINTR  :
        fprintf(stderr, "unable to send request, ERR:\n");
        return 0;
      default :
        log_message_katcp(d, KATCP_LEVEL_ERROR, DMON_MODULE_NAME, "unable to send request: %s", strerror(errno));
        fprintf(stderr, "unable to send request: \n");
        break;
    }
  }

	return 0;
}

int rcv_udp(struct katcp_dispatch *d, struct udp_state *ud)
{
	struct udp_message buffer, *uv;
	int rr, i, total;
	uint32_t word;

	total = 0;

	uv = &buffer;

	//rr = recv(ud->u_fd, uv, UDP_MAX_PACKET, 0);
	rr = recvfrom(ud->u_fd, uv, UDP_MAX_PACKET, 0, NULL, NULL);
	if(rr < 0){
		switch(errno){
			case EAGAIN :
			case EINTR  :
				return 0;
			default :
				log_message_katcp(d, KATCP_LEVEL_ERROR, DMON_MODULE_NAME, "udp receive failed with %s", strerror(errno));
				close(ud->u_fd);
				ud->u_fd = (-1);
				return 0;
		}
	}

	for(i = 0; i < rr; i += 4){
		word = (uint32_t)(uv->u_data + i);
		log_message_katcp(d, KATCP_LEVEL_TRACE, DMON_MODULE_NAME, "udp data 0x%x", ntohs(word));
	}
	total += rr;                                                                        
	log_message_katcp(d, KATCP_LEVEL_INFO, DMON_MODULE_NAME,"Current received total: %d bytes\n", total);    
	return 0;
}

/*****************************************************************************/

int main(int argc, char **argv)
{
  struct udp_state *ud;
  struct katcp_dispatch *d;
  unsigned int result;
  struct timeval now;
  fd_set fsr;
  char *ip_addr = NULL;
  uint32_t address, length;
  int i, j, c, pos;
  int wait, nooftries;
  int port = 0;

  i = j = 1;
  pos = 0;
  wait = 0;
  nooftries = 10;

  while (i < argc) {
    if (argv[i][0] == '-') {
      c = argv[i][j];
      switch (c) {
        case '\0':
          j = 1;
          i++;
          break;
        case '-' :
          j++;
          break;
        case 'h' :
          fprintf(stderr, "usage: %s -R [-i ipaddress] [-p port] address length\n", argv[0]);
          return 0;
          break;
        case 'i' : 
          j++;
          if(argv[i][j] == '\0'){
            j = 0;
            i++;
          }
          if(i >= argc){
            fprintf(stderr, "%s: option -%c requires a parameter\n", argv[0], c);
          }
          ip_addr = argv[i] + j;	
          i++;
          j = 1;
          break;
        case 'p' : 
          j++;
          if(argv[i][j] == '\0'){
            j = 0;
            i++;
          }
          if(i >= argc){
            fprintf(stderr, "%s: option -%c requires a parameter\n", argv[0], c);
          }
          port = atoi(argv[i] + j);	
          fprintf(stderr, "port number is %d\n", port);
          i++;
          j = 1;
          break;
        case 'R' : 
          rw_flag = 1;	
          i++;
          break;
        default:
          fprintf(stderr, "%s: unknown option -%c\n", argv[0], argv[i][j]);
          return 2;
      }
    } else {
      pos = i;
      i = argc;
    }
  }
  d = setup_katcp(STDOUT_FILENO);
  if(d == NULL){
    fprintf(stderr, "setup katcp failed\n");

    return EX_OSERR;
  }
  ud = create_udp(d);
  if(ud == NULL){
    fprintf(stderr, "create udp failed\n");
    log_message_katcp(d, KATCP_LEVEL_ERROR, DMON_MODULE_NAME, "unable to allocate local udp state");
    write_katcp(d);
    return EX_OSERR;
  }
#if 0
  if(connect_udp(d, ud, port) < 0){
    fprintf(stderr, "connect udp failed\n");
    log_message_katcp(d, KATCP_LEVEL_ERROR, DMON_MODULE_NAME, "unable to bind udp");
    return EX_OSERR;
  }
#endif
  ud->u_fd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
  if(ud->u_fd < 0){
    fprintf(stderr, "unable to create udp socket:\n ");
    log_message_katcp(d, KATCP_LEVEL_ERROR, DMON_MODULE_NAME, "unable to create udp socket: %s", strerror(errno));
    return -1;
  }

  for(;;){

    FD_ZERO(&fsr);

    FD_SET(ud->u_fd, &fsr);

    address = atoi(argv[pos]); 
    length  = atoi(argv[pos + 1]);

    printf("address[%s] and length[%d]\n", ip_addr, length);

    send_udp(d, ud, ip_addr, port, address, length);

    now.tv_sec =  10;
    now.tv_usec = 0;
    result = select(ud->u_fd + 1, &fsr, NULL, NULL, &now);
    if(result == 0){
      /* Resend again after timeout */
      printf("Resending udp again\n");
      send_udp(d, ud, ip_addr, port, address, length);
    }

    for(wait = 0; wait < nooftries; wait++){

      if(FD_ISSET(ud->u_fd, &fsr)){
        result = rcv_udp(d, ud);
        if(!result){
          return EX_OK;
        }
      }
    }
    return EX_OSERR;

  }

  destroy_udp(d, ud);
  shutdown_katcp(d);

  return EX_OK;
}







