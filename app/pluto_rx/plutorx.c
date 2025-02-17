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
    char type[30]="";

    bool perf=false;
    while (1)
    {
        a = getopt(argc, argv, "hn:o:b:pt:");

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
        case 'p':
            perf = true;
            break;
        case 't':
            strcpy(type,optarg);
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

    struct iio_context *ctx = NULL;
    //IIO Scan 
    struct iio_scan_context *scan_ctx = iio_create_scan_context("local:usb:ip", 0);
    struct iio_context_info **info;
    ssize_t ret = iio_scan_context_get_info_list(scan_ctx, &info);
    
    if (ret > 0)
    {
        for(int contextidx=0;contextidx<ret;contextidx++)
        {
            // Get dev info
            const char *dev_id = iio_context_info_get_uri(info[contextidx]);
            fprintf(stderr,"Discovered : %s\n",dev_id);
            if(strlen(type)>0)
            {
                //fprintf(stderr,"Comp %s %s\n",type,dev_id);
                if (strncmp(type,dev_id,strlen(type))==0)
                {
                    ctx=iio_create_context_from_uri(dev_id);
                    //ctx=iio_create_context_from_uri("ip:10.0.0.52");
                    fprintf(stderr,"Using %s \n",dev_id);
                }    
            }   
            else
            if(ctx==NULL)
            {
                fprintf(stderr,"Using %s \n",dev_id);
                ctx=iio_create_context_from_uri(dev_id);
            }    

        }
    }
        
    iio_scan_context_destroy(scan_ctx);
    if(ctx==NULL)
    {
        fprintf(stderr,"Error : No plutosdr found ! Exiting");
        exit(1);
    }    
    // IIO Init
    
    /*
	ctx = iio_create_local_context();
    if(ctx==NULL)
        ctx=iio_create_network_context("10.0.0.52");
    */    
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
    //#define SAMPLE_MINI 25000000/12
    #define SAMPLE_MINI 18000000
    #define SAMPLE_MAX 64000000
    #define MAX_BUFF_SIZE 8000000LL/2LL
    #define MAX_TOTAL_SIZE 64000000LL
    #define MAX_CNT 64
   
    iio_channel_attr_read_longlong(iio_device_find_channel(dev,"voltage0",false),"sampling_frequency",&SampleRate);
   
    // Get a 100 ms buffer    
   
    if(buf_request>0)
        blockSize=buf_request;
    else
    blockSize = SampleRate/8LL; 
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
    
   
    fprintf(stderr, "Start receiving\n");
    uint32_t val;
    char *RxBuffer = NULL;
    iio_device_reg_read(rx, 0x80000088, &val);
    iio_device_reg_write(rx, 0x80000088, val);
    size_t BlockRead=0;
    size_t ErrorRead=0;

    if(!perf)
    {
        

        while (want_quit == 0)
        {
            size_t Size = iio_buffer_refill(rxbuf);
            
            iio_device_reg_read(rx, 0x80000088, &val);
            if (val & 4)
            {
                //fprintf(stderr,"PlutoSDR underflow!\n");
                fprintf(stderr,"!");fflush(stderr);
                iio_device_reg_write(rx, 0x80000088, val);
            }
            
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
    }
    else
    {
         size_t ErrorRead=0;
         size_t step=250000;
        
        for(SampleRate=out_cs8?SAMPLE_MINI*2:SAMPLE_MINI;(SampleRate<SAMPLE_MAX)&&(ErrorRead==0);SampleRate+=step)
        {
            iio_channel_attr_write_longlong(iio_device_find_channel(dev,"voltage0",false),"sampling_frequency",SampleRate); 
            if(rxbuf!=NULL) iio_buffer_destroy(rxbuf);
            blockSize = SampleRate/8LL; 
            blockSize=(blockSize>>2)<<2;
            if(blockSize>MAX_BUFF_SIZE) blockSize=MAX_BUFF_SIZE;
            kernel_buffer_cnt=MAX_TOTAL_SIZE/(blockSize);
            if(kernel_buffer_cnt>MAX_CNT) kernel_buffer_cnt=MAX_CNT;
            iio_device_set_kernel_buffers_count(dev, kernel_buffer_cnt);
            struct iio_buffer *rxbuf = iio_device_create_buffer(rx, blockSize, false); //2 because driver think it is 16bit 
            fprintf(stderr,"Using %d buffers of %ld bytes\n",kernel_buffer_cnt,blockSize);

           fprintf(stderr,"Trying sampleRate %lld\n",SampleRate);
            iio_device_reg_read(rx, 0x80000088, &val);
            iio_device_reg_write(rx, 0x80000088, val);
            for(size_t Read=0;(Read<20)&&(ErrorRead==0);Read++)
            {
                if (want_quit == 1) return 0;
                size_t Size = iio_buffer_refill(rxbuf);
                
                iio_device_reg_read(rx, 0x80000088, &val);
                if (val & 4)
                {
                    //fprintf(stderr,"PlutoSDR underflow!\n");
                    ErrorRead++;
                    
                    iio_device_reg_write(rx, 0x80000088, val);
                    break;
                }
                
            }
            if(ErrorRead)
            {
                 fprintf(stderr,"Underrun detected: exting...\n");
                  break;
            }
        }
        fprintf(stderr,"Sample Max=%lld\n",SampleRate-step);
    }
    return 0;
}
