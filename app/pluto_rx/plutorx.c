/*
  ===========================================================================

  Copyright (C) 2024 Evariste F5OEO
 

  PLUTO_RX is free software: you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 3 of the License, or
  (at your option) any later version.

  PLUTO_RX is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License LIMfor more details.

  You should have received a copy of the GNU General Public License
  along with RX2TX.  If not, see <http://www.gnu.org/licenses/>.

  ===========================================================================
*/

// https://adaptivesupport.amd.com/s/article/75195?language=en_US


//
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <math.h>
#include <errno.h>

#include <getopt.h>
#include <ctype.h>
#include <termios.h>
#include <iio.h>

#define PROGRAM_VERSION "0.0.1"

#include <sys/types.h>
#include <sys/socket.h>
#include <unistd.h>
#include <stdio.h>
#include <netinet/in.h> /* IPPROTO_IP, sockaddr_in, htons(), 
htonl() */
#include <arpa/inet.h>  /* inet_addr() */
#include <netdb.h>
#include <time.h>
#include <sys/ioctl.h>


// Global variable used by the signal handler and capture/encoding loop
static int want_quit = 0;
int m_sock;
struct sockaddr_in m_client;
char UdpOutput[255];

// Global signal handler for trapping SIGINT, SIGTERM, and SIGQUIT
static void signal_handler(int signal)
{
    want_quit = 1;
    //m_running = false;
}

void udp_set_ip(const char *ip)
{
    char text[40];
    char *add[2];
    u_int16_t sock;

    strcpy(text, ip);
    add[0] = strtok(text, ":");
    add[1] = strtok(NULL, ":");
    if (strlen(add[1]) == 0)
        sock = 1314;
    else
        sock = atoi(add[1]);
    // Construct the client sockaddr_in structure
    memset(&m_client, 0, sizeof(m_client));       // Clear struct
    m_client.sin_family = AF_INET;                // Internet/IP
    m_client.sin_addr.s_addr = inet_addr(add[0]); // IP address
    m_client.sin_port = htons(sock);              // server socket
}

void udp_send(char *b, size_t len)
{



    //#define UDP_MTU (1454)
    #define UDP_MTU 65507 
    int index = 0;
    for (index = 0; index < len; index += UDP_MTU)
    {

        sendto(m_sock, b+index, (index+UDP_MTU<len)?UDP_MTU:len-index , 0, (struct sockaddr *)&m_client, sizeof(m_client));
    }
    
}

void udp_init(void)
{
    // Create a socket for transmitting UDP TS packets
    if ((m_sock = socket(PF_INET, SOCK_DGRAM, IPPROTO_UDP)) < 0)
    {
        printf("Failed to create socket\n");
        return;
    }
    int one;

    //if (setsockopt(m_sock, SOL_SOCKET, SO_ZEROCOPY, &one, sizeof(one)))
    //    error(1, errno, "setsockopt zerocopy");

    udp_set_ip(UdpOutput);
}

void print_usage()
{

    fprintf(stderr,
            "plutorx -%s\n\
Usage:\nplutorx -n ip:port\n\
-h            help (print this help).\n\
Example : ./plutorx -n 10.0.0.100:10000 -o 8\n\
\n",
            PROGRAM_VERSION);

} /* end function print_usage */

int main(int argc, char **argv)
{

    int a;
    int anyargs = 0;
    int upsample = 1;
    int buffer_size = 0;
    bool outnetwork = false;
    bool out_cs8=false;
    size_t buf_request=0;
    while (1)
    {
        a = getopt(argc, argv, "hn:o:b:");

        if (a == -1)
        {
            if (anyargs)
                break;
            else
                a = 'h'; //print usage and exit
        }
        anyargs = 1;

        switch (a)
        {

        case 'h': // help
            print_usage();
            exit(0);
            break;

        case 'n':
            strcpy(UdpOutput, optarg);
            outnetwork = true;
            break;
        case 'o':
            out_cs8 = (atoi(optarg)==8);
            break;
        case 'b':
            buf_request = atoi(optarg);
            break;
        case -1:
            break;
        case '?':
            if (isprint(optopt))
            {
                fprintf(stderr, "plutorx `-%c'.\n", optopt);
            }
            else
            {
                fprintf(stderr, "plutorx: unknown option character `\\x%x'.\n", optopt);
            }
            print_usage();

            exit(1);
            break;
        default:
            print_usage();
            exit(1);
            break;
        } /* end switch a */
    }     /* end while getopt() */

    
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
    signal(SIGQUIT, signal_handler);
    signal(SIGPIPE, signal_handler);

    // IIO Init
    struct iio_context *ctx = NULL;
    
	ctx = iio_create_local_context();
    if(ctx==NULL)
        ctx=iio_create_network_context("10.0.0.52");
    struct iio_device *dev =  iio_context_find_device(ctx, "ad9361-phy");
    struct iio_device *rx = iio_context_find_device(ctx, "cf-ad9361-lpc");
    struct iio_channel *chnI = iio_device_get_channel(rx, 0);
    struct iio_channel *chnQ = iio_device_get_channel(rx, 1);
    iio_channel_enable(chnI);
    if(out_cs8)
         iio_channel_disable(chnQ);
     else    
         iio_channel_enable(chnQ);

    size_t blockSize=1024;

    
    unsigned int kernel_buffer_cnt=1;
    long long SampleRate;
    iio_channel_attr_read_longlong(iio_device_find_channel(dev,"voltage0",false),"sampling_frequency",&SampleRate);
    // Get a 100 ms buffer    
    #define MAX_BUFF_SIZE 8000000LL
    #define MAX_TOTAL_SIZE 64000000LL
    #define MAX_CNT 64
    if(buf_request>0)
        blockSize=buf_request;
    else
        blockSize = SampleRate/4LL; 
    blockSize=(blockSize>>2)<<2;
    if(blockSize>MAX_BUFF_SIZE) blockSize=MAX_BUFF_SIZE;
    kernel_buffer_cnt=MAX_TOTAL_SIZE/(blockSize*2);
    if(kernel_buffer_cnt>MAX_CNT) kernel_buffer_cnt=MAX_CNT;


    iio_device_set_kernel_buffers_count(dev, kernel_buffer_cnt);
    struct iio_buffer *rxbuf = iio_device_create_buffer(rx, blockSize/2, false); //2 because driver think it is 16bit 
    fprintf(stderr,"Using %d buffers of %ld bytes\n",kernel_buffer_cnt,blockSize);
    if (outnetwork)
    {
        udp_init();
        udp_set_ip(UdpOutput);
    }
    
   
    
    if (out_cs8)
    {
        
    }
    else
    ;
    

    fprintf(stderr, "Start receiving\n");
    uint32_t val;
    char *RxBuffer = NULL;

        while (want_quit == 0)
        {
            size_t Size = iio_buffer_refill(rxbuf);
            /*
            iio_device_reg_read(dev, 0x80000088, &val);
            if (val & 4)
            {
                fprintf(stderr,"PlutoSDR underflow!\n");
                iio_device_reg_write(dev, 0x80000088, val);
            }
            */
            if (outnetwork)
            {

                //fprintf(stderr, "-> %04x %04x %04x  \n", RxBuffer[0] & 0xFFFF, RxBuffer[1] & 0xFFFF, RxBuffer[2] & 0xFFFF);
                //fprintf(stderr, "Rx %ld  \n",Size);
                udp_send((char*)iio_buffer_first(rxbuf, chnI), Size );
            }
            else
            {
                fwrite((char*)iio_buffer_first(rxbuf, chnI),1, Size, stdout);
            }
    
        }

    return 0;
}
