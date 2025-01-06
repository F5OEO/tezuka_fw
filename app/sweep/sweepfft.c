#include <time.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h> /* for sleep() */
#include <math.h>
#include <stdbool.h>
#include "civetweb.h"


/* Global options for this example. */
static const char WS_URL[] = "/waterfall";
static const char *SERVER_OPTIONS[] =
    {"listening_ports", "7681", "num_threads", "10", NULL, NULL};

/* Define websocket sub-protocols. */
/* This must be static data, available between mg_start and mg_stop. */
static const char subprotocol_bin[] = "Company.ProtoName.bin";
static const char subprotocol_json[] = "Company.ProtoName.json";
static const char *subprotocols[] = {subprotocol_bin, subprotocol_json, NULL};
static struct mg_websocket_subprotocols wsprot = {2, subprotocols};


/* Exit flag for the server */
volatile int g_exit = 0;

/* User defined client data structure */
struct tclient_data {

	time_t started;
	time_t closed;
	struct tmsg_list_elem *msgs;
};

struct tmsg_list_elem {
	time_t timestamp;
	void *data;
	size_t len;
	struct tmsg_list_elem *next;
};


/* Helper function to get a printable name for websocket opcodes */
static const char *
msgtypename(int flags)
{
	unsigned f = (unsigned)flags & 0xFu;
	switch (f) {
	case MG_WEBSOCKET_OPCODE_CONTINUATION:
		return "continuation";
	case MG_WEBSOCKET_OPCODE_TEXT:
		return "text";
	case MG_WEBSOCKET_OPCODE_BINARY:
		return "binary";
	case MG_WEBSOCKET_OPCODE_CONNECTION_CLOSE:
		return "connection close";
	case MG_WEBSOCKET_OPCODE_PING:
		return "PING";
	case MG_WEBSOCKET_OPCODE_PONG:
		return "PONG";
	}
	return "unknown";
}

struct mg_connection *gconn=NULL;

void jsonize_param(const char *name, float param, char *jsonresult)
{
    char paramjson[255];

    if (strlen(jsonresult) == 0)
    {
        sprintf(paramjson, "{\"%s\":%.0f", name, param);
    }
    else
    {
        sprintf(paramjson, ",\"%s\":%.0f", name, param);
    }

    strcat(jsonresult, paramjson);
}


void update_sweep_param()
{
	char webmessage[255];
	float freq1=110e6;
	float span=60e6;
	float freqmini=freq1-span/2.0;
	float freqrx=freq1+(span*8)/2.0;
    strcpy(webmessage, "");

    jsonize_param("center", freqrx, webmessage);
    
    jsonize_param("span", span*8, webmessage);
    strcat(webmessage, "}");
    fprintf(stderr, "Webmessage = %s\n", webmessage);
	mg_websocket_write(gconn,MG_WEBSOCKET_OPCODE_TEXT,(const char *)webmessage,strlen(webmessage));
    
}


/* Callback for handling data received from the server */
static int
websocket_client_data_handler(struct mg_connection *conn,
                              int flags,
                              char *data,
                              size_t data_len,
                              void *user_data)
{
	struct tclient_data *pclient_data = (struct tclient_data *)user_data;
	time_t now = time(NULL);
	
	/* We may get some different message types (websocket opcodes).
	 * We will handle these messages differently. */
	int is_text = ((flags & 0xf) == MG_WEBSOCKET_OPCODE_TEXT);
	int is_bin = ((flags & 0xf) == MG_WEBSOCKET_OPCODE_BINARY);
	int is_ping = ((flags & 0xf) == MG_WEBSOCKET_OPCODE_PING);
	int is_pong = ((flags & 0xf) == MG_WEBSOCKET_OPCODE_PONG);
	int is_close = ((flags & 0xf) == MG_WEBSOCKET_OPCODE_CONNECTION_CLOSE);

	/* Log output: We got some data */
	
	/*printf("%10.0f - Client received %lu bytes of %s data from server%s",
	       difftime(now, pclient_data->started),
	       (long unsigned)data_len,
	       msgtypename(flags),
	       (is_text ? ": " : ".\n"));
	*/
	/* Check if we got a websocket PING request */
	if (is_ping) {
		/* PING requests are to check if the connection is broken.
		 * They should be replied with a PONG with the same data.
		 */
		mg_websocket_client_write(conn,
		                          MG_WEBSOCKET_OPCODE_PONG,
		                          data,
		                          data_len);
		return 1;
	}

	/* Check if we got a websocket PONG message */
	if (is_pong) {
		/* A PONG message may be a response to our PING, but
		 * it is also allowed to send unsolicited PONG messages
		 * send by the server to check some lower level TCP
		 * connections. Just ignore all kinds of PONGs. */
		return 1;
	}

	/* It we got a websocket TEXT message, handle it ... */
	if (is_text) {
		struct tmsg_list_elem *p;
		struct tmsg_list_elem **where = &(pclient_data->msgs);

		/* ... by printing it to the log ... */
		fwrite(data, 1, data_len, stdout);
		printf("\n");

		/* ... and storing it (OOM ignored for simplicity). */
		p = (struct tmsg_list_elem *)malloc(sizeof(struct tmsg_list_elem));
		p->timestamp = now;
		p->data = malloc(data_len);
		memcpy(p->data, data, data_len);
		p->len = data_len;
		p->next = NULL;
		while (*where != NULL) {
			where = &((*where)->next);
		}
		*where = p;
	}
    

	#define clrscr() printf("\e[1;1H\e[2J")
	/* Another option would be BINARY data. */
	if (is_bin) {
		/* In this example, we just ignore binary data.
		 * According to some blogs, discriminating TEXT and
		 * BINARY may be some remains from earlier drafts
		 * of the WebSocket protocol.
		 * Anyway, a real application will usually use
		 * either TEXT or BINARY. */
		float *fdata=(float *)data;
		static float min=100;
		static float max=-100;
		static int oldPeak=0;
		int Peak=0;
		int inaslot=0;
		float dbfloar=10;
        static uint16_t *binarray=NULL;
		if(binarray==NULL)
			binarray=(uint16_t *)malloc(4096*sizeof(uint16_t)*8);
		static int count=0;	
		bool is_sweep=1;
		int OutputResolution=1920*2; //FixMe need to be retrieve
		int step=(4096*8)/(OutputResolution);
		static int displaybin=0;
		uint16_t maxoverstep=0;
		//fprintf(stderr,"Len %ld \n ",data_len);
		for(int i=0;i<data_len/sizeof(float);i++)
		{
			if(i==0)
			{
				 //fprintf(stderr,"%f \n ",fdata[0]);
				 count=(int)fdata[0];
				 if(count==0) displaybin=0;
				 maxoverstep=0;
			}	 
			else
			{
				//float logval=log(fdata[i]);
				float logval=100*log(fdata[i]);
				//binarray[displaybin++]=logval;
				
				if(logval>maxoverstep) maxoverstep=logval;
				if((i%step)==0)
				{
					//fprintf(stderr,"maxoverstep %u",maxoverstep);
					binarray[displaybin++]=maxoverstep;
					maxoverstep=0;
				}
				
			}	
			
		}
		if(is_sweep)
		{
			//count=(count+1)%8;
			//fprintf(stderr,"Prof %d displaybin %d\n",count,displaybin);
			if((gconn!=NULL)&&(count==7))
			{
				//fprintf(stderr,"display %u\n",displaybin);
				mg_websocket_write(gconn,MG_WEBSOCKET_OPCODE_BINARY,(const char *)binarray,displaybin*sizeof(uint16_t));
				//displaybin=0;
				
			}
		}
		else
			mg_websocket_write(gconn,MG_WEBSOCKET_OPCODE_BINARY,(const char *)binarray,4096*sizeof(uint16_t));		
		

	}

	/* It could be a CLOSE message as well. */
	if (is_close) {
		printf("%10.0f - Goodbye\n", difftime(now, pclient_data->started));
		return 0;
	}

	/* Return 1 to keep the connection open */
	return 1;
}


/* Callback for handling a close message received from the server */
static void
websocket_client_close_handler(const struct mg_connection *conn,
                               void *user_data)
{
	struct tclient_data *pclient_data = (struct tclient_data *)user_data;

	pclient_data->closed = time(NULL);
	printf("%10.0f - Client: Close handler\n",
	       difftime(pclient_data->closed, pclient_data->started));
}


/* Websocket client test function */
void
run_websocket_client(const char *host,
                     int port,
                     int secure,
                     const char *path,
                     const char *greetings)
{
	char err_buf[100] = {0};
	struct mg_connection *client_conn;
	struct tclient_data *pclient_data;
	int i;

	/* Allocate some memory for callback specific data.
	 * For simplicity, we ignore OOM handling in this example. */
	pclient_data = (struct tclient_data *)malloc(sizeof(struct tclient_data));

	/* Store start time in the private structure */
	pclient_data->started = time(NULL);
	pclient_data->closed = 0;
	pclient_data->msgs = NULL;

	/* Log first action (time = 0.0) */
	printf("%10.0f - Connecting to %s:%i\n", 0.0, host, port);

	/* Connect to the given WS or WSS (WS secure) server */
	client_conn = mg_connect_websocket_client(host,
	                                          port,
	                                          secure,
	                                          err_buf,
	                                          sizeof(err_buf),
	                                          path,
	                                          NULL,
	                                          websocket_client_data_handler,
	                                          websocket_client_close_handler,
	                                          pclient_data);

	/* Check if connection is possible */
	if (client_conn == NULL) {
		printf("mg_connect_websocket_client error: %s\n", err_buf);
		return;
	}

	/* Connection established */
	//printf("%10.0f - Connected\n", mg_websocket_writedifftime(time(NULL), pclient_data->started));

	/*
	mg_close_connection(client_conn);
	printf("%10.0f - End of test\n",
	       difftime(time(NULL), pclient_data->started));
	

	

	
	{
		struct tmsg_list_elem **where = &(pclient_data->msgs);
		void *p1 = 0;
		void *p2 = 0;
		while (*where != NULL) {
			free((*where)->data);
			free(p2);
			p2 = p1;
			p1 = *where;

			where = &((*where)->next);
		}
		free(p2);
		free(p1);
	}
	free(pclient_data);
	*/
}


/* User defined data structure for websocket client context. */
struct tClientContext {
	uint32_t connectionNumber;
	uint32_t demo_var;
};


/* Handler for new websocket connections. */
static int
ws_connect_handler(const struct mg_connection *conn, void *user_data)
{
	(void)user_data; /* unused */

	/* Allocate data for websocket client context, and initialize context. */
	struct tClientContext *wsCliCtx =
	    (struct tClientContext *)calloc(1, sizeof(struct tClientContext));
	if (!wsCliCtx) {
		/* reject client */
		return 1;
	}
	static uint32_t connectionCounter = 0; /* Example data: client number */
	wsCliCtx->connectionNumber = __sync_add_and_fetch(&connectionCounter, 1);
	mg_set_user_connection_data(
	    conn, wsCliCtx); /* client context assigned to connection */

	/* DEBUG: New client connected (but not ready to receive data yet). */
	const struct mg_request_info *ri = mg_get_request_info(conn);
	printf("Client %u connected with subprotocol: %s\n",
	       wsCliCtx->connectionNumber,
	       ri->acceptedWebSocketSubprotocol);

	return 0;
}


/* Handler indicating the client is ready to receive data. */
static void
ws_ready_handler(struct mg_connection *conn, void *user_data)
{
	(void)user_data; /* unused */

	/* Get websocket client context information. */
	struct tClientContext *wsCliCtx =
	    (struct tClientContext *)mg_get_user_connection_data(conn);
	const struct mg_request_info *ri = mg_get_request_info(conn);
	(void)ri; /* in this example, we do not need the request_info */

	/* Send "hello" message. */
	const char *hello = "{}";
	size_t hello_len = strlen(hello);
	mg_websocket_write(conn, MG_WEBSOCKET_OPCODE_TEXT, hello, hello_len);

	/* DEBUG: New client ready to receive data. */
	printf("Client %u ready to receive data\n", wsCliCtx->connectionNumber);
	gconn=conn;
}


/* Handler indicating the client sent data to the server. */
static int
ws_data_handler(struct mg_connection *conn,
                int opcode,
                char *data,
                size_t datasize,
                void *user_data)
{
	(void)user_data; /* unused */

	/* Get websocket client context information. */
	struct tClientContext *wsCliCtx =
	    (struct tClientContext *)mg_get_user_connection_data(conn);
	const struct mg_request_info *ri = mg_get_request_info(conn);
	(void)ri; /* in this example, we do not need the request_info */

	/* DEBUG: Print data received from client. */
	const char *messageType = "";
	switch (opcode & 0xf) {
	case MG_WEBSOCKET_OPCODE_TEXT:
		messageType = "text";
		break;
	case MG_WEBSOCKET_OPCODE_BINARY:
		messageType = "binary";
		break;
	case MG_WEBSOCKET_OPCODE_PING:
		messageType = "ping";
		break;
	case MG_WEBSOCKET_OPCODE_PONG:
		messageType = "pong";
		break;
	}
	printf("Websocket received %lu bytes of %s data from client %u\n",
	       (unsigned long)datasize,
	       messageType,
	       wsCliCtx->connectionNumber);
    
	return 1;
}


/* Handler indicating the connection to the client is closing. */
static void
ws_close_handler(const struct mg_connection *conn, void *user_data)
{
	(void)user_data; /* unused */

	/* Get websocket client context information. */
	struct tClientContext *wsCliCtx =
	    (struct tClientContext *)mg_get_user_connection_data(conn);

	/* DEBUG: Client has left. */
	printf("Client %u closing connection\n", wsCliCtx->connectionNumber);

	/* Free memory allocated for client context in ws_connect_handler() call. */
	free(wsCliCtx);
}


int
main(int argc, char *argv[])
{
	/* Initialize CivetWeb library without OpenSSL/TLS support. */
	mg_init_library(0);


    const char *greetings = "Hello World!";

	const char *host = "10.0.0.52";
	const char *path = "/waterfall";

#if defined(NO_SSL)
	const int port = 80;
	const int secure = 0;
	mg_init_library(0);
#else
	const int port = 443;
	const int secure = 1;
	mg_init_library(MG_FEATURES_SSL);
#endif

	run_websocket_client(host, port, secure, path, greetings);

	/* Start the server using the advanced API. */
	struct mg_callbacks callbacks = {0};
	void *user_data = NULL;

	struct mg_init_data mg_start_init_data = {0};
	mg_start_init_data.callbacks = &callbacks;
	mg_start_init_data.user_data = user_data;
	mg_start_init_data.configuration_options = SERVER_OPTIONS;

	struct mg_error_data mg_start_error_data = {0};
	char errtxtbuf[256] = {0};
	mg_start_error_data.text = errtxtbuf;
	mg_start_error_data.text_buffer_size = sizeof(errtxtbuf);

	struct mg_context *ctx =
	    mg_start2(&mg_start_init_data, &mg_start_error_data);
	if (!ctx) {
		fprintf(stderr, "Cannot start server: %s\n", errtxtbuf);
		mg_exit_library();
		return 1;
	}

	/* Register the websocket callback functions. */
	mg_set_websocket_handler_with_subprotocols(ctx,
	                                           WS_URL,
	                                           &wsprot,
	                                           ws_connect_handler,
	                                           ws_ready_handler,
	                                           ws_data_handler,
	                                           ws_close_handler,
	                                           user_data);

	/* Let the server run. */
	printf("Websocket server running\n");
	while (!g_exit) {
		sleep(1);
		update_sweep_param();
	}
	printf("Websocket server stopping\n");

	/* Stop server, disconnect all clients. Then deinitialize CivetWeb library.
	 */
	mg_stop(ctx);
	mg_exit_library();
}