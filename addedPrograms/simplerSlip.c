
#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>

#include "../tools/tools-utils.h"

#ifndef BAUDRATE
#define BAUDRATE B115200
#endif

#define BUFSIZE 2000
#define DEBUG_LINE_MARKER '\r'
#define MIN_DEVMTU 1500
#define WIRESHARK_IMPORT_FORMAT 1

#define SLIP_END      0300
#define SLIP_ESC      0333
#define SLIP_ESC_END  0334
#define SLIP_ESC_ESC  0335

speed_t b_rate = BAUDRATE;

unsigned char slip_buf[BUFSIZE];
int slip_end=0;
int slip_begin=0;
int timestamp=0;
int verbose=0;

/* prints the current time*/
void
stamptime(void)
{
	static long startsecs=0,startmsecs=0;
	long secs,msecs;
	struct timeval tv;
	time_t t;
	struct tm *tmp;
	char timec[20];

	gettimeofday(&tv, NULL) ;
	msecs=tv.tv_usec/1000;
	secs=tv.tv_sec;
	if (startsecs) {
		secs -=startsecs;
		msecs-=startmsecs;
		if (msecs<0) {secs--;msecs+=1000;}
		fprintf(stderr,"%04lu.%03lu ", secs, msecs);
	} else {
		startsecs=secs;
		startmsecs=msecs;
		t=time(NULL);
		tmp=localtime(&t);
		strftime(timec,sizeof(timec),"%T",tmp);
		fprintf(stderr,"\n%s ",timec);
	}
}

/* opens a device */
int
devopen(const char *dev, int flags)
{
  char t[1024];
  strcpy(t, "/dev/");
  strncat(t, dev, sizeof(t) - 5);
  return open(t, flags);
}

/* checks if the string only contains alfanumeric characters, '0', '\r', '\n', or '\t' */
int
is_sensible_string(const unsigned char *s, int len)
{
  int i;
  for(i = 1; i < len; i++) {
    if(s[i] == 0 || s[i] == '\r' || s[i] == '\n' || s[i] == '\t') {
      continue;
    } else if(s[i] < ' ' || '~' < s[i]) {
      return 0;
    }
  }
  return 1;
}

/* get sockaddr, IPv4 or IPv6 */
void *
get_in_addr(struct sockaddr *sa)
{
	if(sa->sa_family == AF_INET) {
		return &(((struct sockaddr_in*)sa)->sin_addr);
	}
	return &(((struct sockaddr_in6*)sa)->sin6_addr);
}

/* Configuration of SLIP communications (a.o. baudrate) */
void
stty_telos(int fd)
{
	struct termios tty;
	speed_t speed = b_rate;

	if(tcflush(fd, TCIOFLUSH) == -1) err(1, "tcflush"); /* flush data received but not read, and data written but not send */

	if(tcgetattr(fd, &tty) == -1) err(1, "tcgetattr"); /* store properties of fd in tty */

	cfmakeraw(&tty); /* set attributes in tty to raw mode */

	/* Nonblocking read without flow control */
	tty.c_cc[VTIME] = 0;
	tty.c_cc[VMIN] = 0;
	tty.c_cflag &= ~CRTSCTS;
	tty.c_iflag &= ~IXON;
	tty.c_iflag &= ~IXOFF & ~IXANY;
	tty.c_cflag &= ~HUPCL;
	tty.c_cflag &= ~CLOCAL;

	cfsetispeed(&tty, speed);
	cfsetospeed(&tty, speed);

	tty.c_cflag |= CLOCAL;
	if(tcsetattr(fd, TCSAFLUSH, &tty) == -1) err(1, "tcsetattr");

	int i = TIOCM_DTR;
	if(ioctl(fd, TIOCMBIS, &i) == -1) err(1, "ioctl"); /* flush data received but not read, and data written but not send */

	usleep(10*1000);		/* Wait for hardware 10ms. */

	/* Flush input and output buffers. */
	if(tcflush(fd, TCIOFLUSH) == -1) err(1, "tcflush");
}

/* Add character to SLIP-buffer */
void
slip_send(int fd, unsigned char c)
{
	if(slip_end >= sizeof(slip_buf)) {
		err(1, "slip_send overflow");
	}
	slip_buf[slip_end] = c;
	slip_end++;
}

int
slip_empty()
{
	return slip_end == 0;
}

/* empties slip_buf to the given fd */
void
slip_flushbuf(int fd)
{
	int n;

	if(slip_empty()) { /* nothing to flush */
		return;
	}

	n = write(fd, slip_buf + slip_begin, (slip_end - slip_begin));

	if(n == -1 && errno != EAGAIN) { /* Write would block while in non-blocking mode */
		err(1, "slip_flushbuf write failed");
	} else {
		slip_begin += n;
		if(slip_begin == slip_end) { /* all data is written, reset begin and end */
			slip_begin = slip_end = 0;
		}
	}
}

/* cleanup on exit */
void
cleanup(void)
{
	printf("exiting program\n");
}

/* cleanup on exit by signal */
void
sigcleanup(int signal)
{
	printf("got signal, ");
	exit(0);
}

/* Read from serial, write to tunnel.  */
void
serial_to_tun(FILE *inslip, int outfd)
{
	static union {
		unsigned char inbuf[BUFSIZE];
	} uip;

	static int inbufptr = 0;
	int ret,i;
	unsigned char c;

	/* read 1 character from serial connection */
	ret = fread(&c, 1, 1, inslip);
	if(ret == -1 || ret == 0) err(1, "serial_to_tun: read"); /* problem reading or no data*/
	goto after_fread;

	read_more:
	if(inbufptr >= sizeof(uip.inbuf)) {
		if(timestamp) stamptime();
		fprintf(stderr, "*** dropping large %d byte packet\n",inbufptr);
		inbufptr = 0;
	}
	ret = fread(&c, 1, 1, inslip);

	after_fread:
	if(ret == -1) {
		err(1, "serial_to_tun: read");
	}
	if(ret == 0) { /* end of packet*/
		clearerr(inslip); /* reset input state */
		return; /* */
	}
	switch(c) { /* process byte */
		case SLIP_END: /* end of slip message, process tasks about message here */
		if(inbufptr > 0) { /* buffer is not empty */
			if(uip.inbuf[0] == '!') { /* SLIP command response */
				printf("command response received: %s\n",uip.inbuf);
			} else if(uip.inbuf[0] == '?') { /* SLIP command request */
				printf("command request received: %s\n",uip.inbuf);
			} else if(uip.inbuf[0] == DEBUG_LINE_MARKER) { /* SLIP debug line, print buffer to stdout */
				fwrite(uip.inbuf + 1, inbufptr - 1, 1, stdout);
			} else { /* normal packet */
				if(verbose>2) { /* write some info about packet */
					if (timestamp) stamptime();
					printf("Packet from SLIP of length %d.\n", inbufptr);
					if (verbose>4) { /* print whole packet */
						#if WIRESHARK_IMPORT_FORMAT
							printf("0000");
							for(i = 0; i < inbufptr; i++) printf(" %02x",uip.inbuf[i]);
						#else
							printf("         ");
							for(i = 0; i < inbufptr; i++) {
								printf("%02x", uip.inbuf[i]);
								if((i & 3) == 3) printf(" ");
								if((i & 15) == 15) printf("\n         ");
							}
						#endif
						printf("\n");
					}
				}
				if(write(outfd, uip.inbuf, inbufptr) != inbufptr) { /* write packet to output file descriptor */
					err(1, "serial_to_tun: write");
				}
			}
			inbufptr = 0;
		}
		break;

		case SLIP_ESC: /* escape char found, next char needs to be taken litteraly*/

		/* try reading the escaped character 
		 * if reading fails, the method returns
		 */
		if(fread(&c, 1, 1, inslip) != 1) { 
			clearerr(inslip);
			/* Put ESC back and give up! */
			ungetc(SLIP_ESC, inslip);
			return;
		}
		switch(c) { /* translate escape sequence to escaped character */
			case SLIP_ESC_END:
			c = SLIP_END;
			break;
			case SLIP_ESC_ESC:
			c = SLIP_ESC;
			break;
		}

		/* FALLTHROUGH */
		default:
		uip.inbuf[inbufptr++] = c;

		/* Echo lines as they are received for verbose=2,3,5+ */
		/* Echo all printable characters for verbose==4 */
		if((verbose==2) || (verbose==3) || (verbose>4)) {
			if(c=='\n') {
				if(is_sensible_string(uip.inbuf, inbufptr)) {
					if (timestamp) stamptime();
					fwrite(uip.inbuf, inbufptr, 1, stdout);
					inbufptr=0;
				}
			}
		} else if(verbose==4) {
			if(c == 0 || c == '\r' || c == '\n' || c == '\t' || (c >= ' ' && c <= '~')) {
				fwrite(&c, 1, 1, stdout);
				if(c=='\n') if(timestamp) stamptime();
			}
		}
		break;
	}
	goto read_more;
}

/* escapes special characters and does the actual writing */
void
write_to_serial(int outfd, void *inbuf, int len)
{
	u_int8_t *p = inbuf;
	int i;

	if(verbose>2) { /* write some info about packet */
		if (timestamp) stamptime();
		printf("Packet from TUN of length %d - write SLIP\n", len);
		if (verbose>4) { /* print whole packet */
			#if WIRESHARK_IMPORT_FORMAT
				printf("0000");
				for(i = 0; i < len; i++) printf(" %02x", p[i]);
			#else
				printf("         ");
				for(i = 0; i < len; i++) {
					printf("%02x", p[i]);
					if((i & 3) == 3) printf(" ");
					if((i & 15) == 15) printf("\n         ");
				}
			#endif
			printf("\n");
		}
	}

	for(i = 0; i < len; i++) { /* add escape for special characters */
		switch(p[i]) {
			case SLIP_END:
			slip_send(outfd, SLIP_ESC);
			slip_send(outfd, SLIP_ESC_END);
			break;
			case SLIP_ESC:
			slip_send(outfd, SLIP_ESC);
			slip_send(outfd, SLIP_ESC_ESC);
			break;
			default:
			slip_send(outfd, p[i]);
			break;
		}
	}

	slip_send(outfd, SLIP_END); /* append real end of message */

}

/* Read from tunnel, write to serial. */
int
tun_to_serial(int infd, int outfd)
{
	struct {
		unsigned char inbuf[BUFSIZE];
	} uip;
	int size;

	if((size = read(infd, uip.inbuf, BUFSIZE)) == -1) err(1, "tun_to_serial: read");

	write_to_serial(outfd, uip.inbuf, size);
	return size;
}

int
main(int argc, char **argv)
{
	int baudrate = -2;
	int devmtu = MIN_DEVMTU;
	int slipfd = 0, tunfd=0;
	const char *siodev = NULL;
	const char *host = NULL;
	const char *port = NULL;
	const char *prog = argv[0];
	FILE *inslip;

	int c;
	int ret;

	setvbuf(stdout, NULL, _IOLBF, 0); /* Line buffered output. */

	/* process arguments */
	while((c = getopt(argc, argv, "B:M:Ls:a:p:v::h")) != -1) {
		switch(c) {
			case 'B':
			baudrate = atoi(optarg);
			break;

			case 'M':
			devmtu=atoi(optarg);
			if(devmtu < MIN_DEVMTU) {
				devmtu = MIN_DEVMTU;
			}

			case 'L':
			timestamp=1;
			break;

			case 's':
			if(strncmp("/dev/", optarg, 5) == 0) {
				siodev = optarg + 5;
			} else {
				siodev = optarg;
			}
			break;

			case 'a':
			host = optarg;
			break;

			case 'p':
			port = optarg;
			break;

			case 'v':
			verbose = 2;
			if (optarg) verbose = atoi(optarg);
			break;

			case '?':
			case 'h':
			default:
			fprintf(stderr,"usage:  %s [options]\n", prog);
			fprintf(stderr,"example: tunslip6 -L -v -s ttyUSB1\n");
			fprintf(stderr,"Options are:\n");
			fprintf(stderr," -B baudrate    9600,19200,38400,57600,115200 (default),230400,460800,921600\n");
			fprintf(stderr," -L             Log output format (adds time stamps)\n");
			fprintf(stderr," -s siodev      Serial device (default /dev/ttyUSB0)\n");
			fprintf(stderr," -M             Interface MTU (default and min: 1280)\n");
			fprintf(stderr," -v [level]     Be verbose, level can be between 0 and 5\n");
			fprintf(stderr,"                when no level is defined, defaults to level 2\n");
			fprintf(stderr," -a serveraddr  \n");
			fprintf(stderr," -p serverport  \n");
			exit(1);
			break;
		}
	}

	if(baudrate != -2) { /* -2: use default baudrate */
		b_rate = select_baudrate(baudrate);
		if(b_rate == 0) {
			err(1, "unknown baudrate %d", baudrate);
		}
	}

	/* connect to IP-address and port */
	if(host != NULL) {
		struct addrinfo hints, *servinfo, *p;
		char s[INET6_ADDRSTRLEN];

		if(port == NULL) {
			port = "60001";
		}

		memset(&hints, 0, sizeof hints);
		hints.ai_family = AF_UNSPEC;
		hints.ai_socktype = SOCK_STREAM;

		int rv;
		/* get all possible server info about host and port */
		if((rv = getaddrinfo(host, port, &hints, &servinfo)) != 0) {
			err(1, "getaddrinfo: %s", gai_strerror(rv));
		}
		/* loop through all the results and connect to the first we can */
		for(p = servinfo; p != NULL; p = p->ai_next) {
			if((slipfd = socket(p->ai_family, p->ai_socktype, p->ai_protocol)) == -1) {
				perror("client: socket");
				continue;
			}
			if(connect(slipfd, p->ai_addr, p->ai_addrlen) == -1) {
				close(slipfd);
				perror("client: connect");
				continue;
			}
			break;
		}
		/* tried all services returned by getaddrinfo but failed to connect */
		if(p == NULL) {
			err(1, "can't connect to ``%s:%s''", host, port);
		}

		fcntl(slipfd, F_SETFL, O_NONBLOCK); /* set nonblocking flag for slipfd */

		/* convert binary address to text form and store in char* s */
		inet_ntop(p->ai_family, get_in_addr((struct sockaddr *)p->ai_addr), s, sizeof(s));
		fprintf(stderr, "slip connected to \"%s:%s\"\n", s, port);

		/* all done with this structure */
		freeaddrinfo(servinfo);
	/* connect to IO-device*/
	} else {
		if(siodev != NULL) { /* IO-device specified */
			slipfd = devopen(siodev, O_RDWR | O_NONBLOCK);
			if(slipfd == -1) {
				err(1, "can't open siodev ``/dev/%s''", siodev);
			}
		} else { /* no IO-device specified, try some defaults */
			static const char *siodevs[] = {
				"ttyUSB0", "cuaU0", "ucom0" /* linux, fbsd6, fbsd5 */
			};
			int i;
			for(i = 0; i < 3; i++) {
				siodev = siodevs[i];
				slipfd = devopen(siodev, O_RDWR | O_NONBLOCK);
				if(slipfd != -1) {
					break;
				}
			}
			if(slipfd == -1) { /* none of the defaults worked */
				err(1, "can't open siodev");
			}
		}
		if (timestamp) stamptime();
		fprintf(stderr, "********SLIP started on ``/dev/%s''\n", siodev);
		stty_telos(slipfd);
	}

	/* start of communication */
	slip_send(slipfd, SLIP_END);
	/* stream for reading */
	inslip = fdopen(slipfd, "r");
	if(inslip == NULL) err(1, "main: fdopen");


	/* TODO: start real tunneling service, currently just echoes every message to stdout */
	tunfd = STDOUT_FILENO;

	/* set signaling functions */
	atexit(cleanup);
	signal(SIGHUP, sigcleanup);
	signal(SIGTERM, sigcleanup);
	signal(SIGINT, sigcleanup);

	/* Processing info to and from the SLIP connection */
	

	/* first check for signaling data of Contiki-device
	 * reply with acknowledgement if data received
	 */

	/* TODO: implement signalling system */

	/* after signalling, process traffic normally */

  	fd_set rset, wset;
	while(1) {
		int maxfd = 0;
		/* clear sets */
		FD_ZERO(&rset);
		FD_ZERO(&wset);

		
		if(!slip_empty()) {		/* Anything to flush? */
			FD_SET(slipfd, &wset); /* set writeset for slipfd */
		}

		FD_SET(slipfd, &rset);	/* Read from slip ASAP! */
		if(slipfd > maxfd) maxfd = slipfd; /* update maxfd if current fd is bigger */

		/* We only have one packet at a time queued for slip output. */
		if(slip_empty()) {
			FD_SET(tunfd, &rset);
			if(tunfd > maxfd) maxfd = tunfd;
		}

		ret = select(maxfd + 1, &rset, &wset, NULL, NULL);
		
		if(ret == -1 && errno != EINTR) {
			err(1, "select");
		} else if(ret > 0) {

			if(FD_ISSET(slipfd, &rset)) { /* read from SLIP */
				serial_to_tun(inslip, tunfd);
			}

			if(FD_ISSET(slipfd, &wset)) { /* write buffer to SLIP */
				slip_flushbuf(slipfd);
			}

			int size;
			if(slip_empty() && FD_ISSET(tunfd, &rset)) { /* write if buffer empty and read necessary from tunnel */
				size=tun_to_serial(tunfd, slipfd);
				slip_flushbuf(slipfd);
			}
		}
	}
}