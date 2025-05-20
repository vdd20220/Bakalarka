#include <stdio.h> // IO
#include <pthread.h> // multithred
#include <stdlib.h>
#include <string.h>
#include <unistd.h> // sleep
#include <stdbool.h>  // For boolean data type (bool, true, false)
#include <signal.h>
#include <stdint.h>
#include <time.h>//debug
#include <iio.h>


#define _USE_MATH_DEFINES
#include <math.h>
#include "my_morse.c"
#include "morse_decode.c"

#include <mqtt.h>
#include "templates/posix_sockets.h"


#define NUM_THREADS 2
#define MQTT_ADRESS "192.168.2.10"//default adress of computer conected thru ssh
#define MQTT_PORT "1883"//default port

//MQTT to subscribe
#define MQTT_SUBSCRIBE_CNT 7
#define MQTT_MSG_TOPIC "/message"
#define MQTT_FREQUENCY_TX_TOPIC "/frequency/tx"
#define MQTT_FREQUENCY_RX_TOPIC "/frequency/rx"
#define MQTT_REFRESH_SETTINGS "/refresh"
#define MQTT_PARIS_SPEED_SETTING "/paris_speed"
#define MQTT_TONE_SETTING "/sine_tone"
#define MQTT_SPACE_SETTING "/space_btw"
//MQTT to publish
#define MQTT_PUBLISH_TX_FREQ "/settings/tx"
#define MQTT_PUBLISH_RX_FREQ "/settings/rx"
#define MQTT_PUBLISH_PARIS_SPEED "/settings/paris_speed"
#define MQTT_PUBLISH_DATA_OUT "/data/out"
#define MQTT_PUBLISH_TONE "/settings/tone"
#define MQTT_PUBLISH_SPACE "/settings/space"
#define MQTT_PUBLISH_MSG_OUT "/data/msg_out"


#define DETECTION_TRESHOLD 1800
#define DETECTION_JUMP 64
#define MESSAGE_MAX_LEN 500
#define BUFFER_LEN 1024*1024
#define SAMPLE_SPEED 2500000
#define FILTER_LEVEL 1e8 //

pthread_mutex_t mtx_data; // for accesing quit_variable betwwen functions

#define KHZ(x) ((long long)(x*1000.0 + .5))
#define MHZ(x) ((long long)(x*1000000.0 + .5))
#define GHZ(x) ((long long)(x*1000000000.0 + .5))

#define DEFAULT_TX_FREQ KHZ(435.0)//is actual MHZ but code multiplies by 1000 because of int32 size
#define DEFAULT_RX_FREQ KHZ(435.0)//is actual MHZ but code multiplies by 1000 because of int32 size

#define DATA_OUT_SIZE 256
#define IIO_ENSURE(expr) { \
	if (!(expr)) { \
		(void) fprintf(stderr, "assertion failed (%s:%d)\n", __FILE__, __LINE__); \
		(void) abort(); \
	} \
}

/* RX is input, TX is output */
enum iodev { RX, TX };

/* common RX and TX streaming params */
struct stream_cfg {
	long long bw_hz; // Analog banwidth in Hz
	long long fs_hz; // Baseband sample rate in Hz
	long long lo_hz; // Local oscillator frequency in Hz
   //long long gain;
	const char* rfport; // Port name
};//1500000 for strings */
static char tmpstr[64];

/* IIO structs required for streaming */
static struct iio_context *ctx   = NULL;
static struct iio_channel *rx0_i = NULL;
static struct iio_channel *rx0_q = NULL;
static struct iio_channel *tx0_i = NULL;
static struct iio_channel *tx0_q = NULL;
static struct iio_buffer  *rxbuf = NULL;
static struct iio_buffer  *txbuf = NULL;

static bool stop;

/* cleanup and exit */
static void tx_shutdown(void)
{
	printf("* Destroying buffers\n");
	if (rxbuf) { iio_buffer_destroy(rxbuf); }
	if (txbuf) { iio_buffer_destroy(txbuf); }

	printf("* Disabling streaming channels\n");
	if (rx0_i) { iio_channel_disable(rx0_i); }
	if (rx0_q) { iio_channel_disable(rx0_q); }
	if (tx0_i) { iio_channel_disable(tx0_i); }
	if (tx0_q) { iio_channel_disable(tx0_q); }

	printf("* Destroying context\n");
	if (ctx) { iio_context_destroy(ctx); }
	exit(0);
}

static void handle_sig(int sig)
{
	printf("Waiting for process to finish... Got signal %d\n", sig);
	stop = true;
}

/* check return value of attr_write function */
static void errchk(int v, const char* what) {
	 if (v < 0) { fprintf(stderr, "Error %d writing to channel \"%s\"\nvalue may not be supported.\n", v, what); tx_shutdown(); }
}

/* write attribute: long long int */
static void wr_ch_lli(struct iio_channel *chn, const char* what, long long val)
{
	errchk(iio_channel_attr_write_longlong(chn, what, val), what);
}

/* write attribute: string */
static void wr_ch_str(struct iio_channel *chn, const char* what, const char* str)
{
	errchk(iio_channel_attr_write(chn, what, str), what);
}

/* helper function generating channel names */
static char* get_ch_name(const char* type, int id)
{
	snprintf(tmpstr, sizeof(tmpstr), "%s%d", type, id);
	return tmpstr;
}

/* returns ad9361 phy device */
static struct iio_device* get_ad9361_phy(void)
{
	struct iio_device *dev =  iio_context_find_device(ctx, "ad9361-phy");
	IIO_ENSURE(dev && "No ad9361-phy found");
	return dev;
}

/* finds AD9361 streaming IIO devices */
static bool get_ad9361_stream_dev(enum iodev d, struct iio_device **dev)
{
   switch (d) {
	case TX: *dev = iio_context_find_device(ctx, "cf-ad9361-dds-core-lpc"); return *dev != NULL;
	case RX: *dev = iio_context_find_device(ctx, "cf-ad9361-lpc");  return *dev != NULL;
	default: IIO_ENSURE(0); return false;
	}
}

/* finds AD9361 streaming IIO channels */
static bool get_ad9361_stream_ch(enum iodev d, struct iio_device *dev, int chid, struct iio_channel **chn)
{
	*chn = iio_device_find_channel(dev, get_ch_name("voltage", chid), d == TX);
	if (!*chn)
		*chn = iio_device_find_channel(dev, get_ch_name("altvoltage", chid), d == TX);
	return *chn != NULL;
}

/* finds AD9361 phy IIO configuration channel with id chid */
static bool get_phy_chan(enum iodev d, int chid, struct iio_channel **chn)
{
	switch (d) {
	case RX: *chn = iio_device_find_channel(get_ad9361_phy(), get_ch_name("voltage", chid), false); return *chn != NULL;
	case TX: *chn = iio_device_find_channel(get_ad9361_phy(), get_ch_name("voltage", chid), true);  return *chn != NULL;
	default: IIO_ENSURE(0); return false;
	}
}

/* finds AD9361 local oscillator IIO configuration channels */
static bool get_lo_chan(enum iodev d, struct iio_channel **chn)
{
	switch (d) {
	 // LO chan is always output, i.e. true
	case RX: *chn = iio_device_find_channel(get_ad9361_phy(), get_ch_name("altvoltage", 0), true); return *chn != NULL;
	case TX: *chn = iio_device_find_channel(get_ad9361_phy(), get_ch_name("altvoltage", 1), true); return *chn != NULL;
	default: IIO_ENSURE(0); return false;
	}
}

/* applies streaming configuration through IIO */
bool cfg_ad9361_streaming_ch(struct stream_cfg *cfg, enum iodev type, int chid)
{
	struct iio_channel *chn = NULL;

	// Configure phy and lo channels
	printf("* Acquiring AD9361 phy channel %d\n", chid);
	if (!get_phy_chan(type, chid, &chn)) {	return false; }
	wr_ch_str(chn, "rf_port_select",     cfg->rfport);
	wr_ch_lli(chn, "rf_bandwidth",       cfg->bw_hz);
	wr_ch_lli(chn, "sampling_frequency", cfg->fs_hz);
   
	// Configure LO channel
	printf("* Acquiring AD9361 %s lo channel\n", type == TX ? "TX" : "RX");
	if (!get_lo_chan(type, &chn)) { return false; }
	wr_ch_lli(chn, "frequency", cfg->lo_hz);
	return true;
}


/**
 * @brief The function will be called whenever a PUBLISH message is received.
 */
void publish_callback(void** unused, struct mqtt_response_publish *published);

/**
 * @brief The client's refresher. This function triggers back-end routines to
 *        handle ingress/egress traffic to the broker.
 *
 * @note All this function needs to do is call \ref __mqtt_recv and
 *       \ref __mqtt_send every so often. I've picked 100 ms meaning that
 *       client ingress/egress traffic will be handled every 100 ms.
 */
void* client_refresher(void* client);

/**
 * @brief Safelty closes the \p sockfd and cancels the \p client_daemon before \c exit.
 */
void exit_example(int status, int sockfd, pthread_t *client_daemon);


//void send_settings(void* client,void *shared_data_pointer);

// shared data structure
typedef struct {
   bool quit;
   int freqency;
   bool new_message;
   bool refresh;//used to signal settings publish to mqtt
   bool client_rdy;
   char message[MESSAGE_MAX_LEN+1];//MESSAGE_MAX_LEN symbols + '\0'
   int frequncy_rx;
   int frequncy_tx;
   int message_lenght;
   int transmission_speed; // paris speed in PARIS per min
   int32_t data_out[DATA_OUT_SIZE];
   bool new_data;
   int sin_freq;
   bool new_settings;
   int space_betwwen_symbols;
   char msg_out[1000];
   int msg_out_len;
   int msg_len;
   bool new_recived_msg;
} data_t;

void * shared_data_pointer;//used for mqtt callback

void reverse(char str[], int length)
{
    int start = 0;
    int end = length - 1;
    while (start < end) {
        char temp = str[start];
        str[start] = str[end];
        str[end] = temp;
        end--;
        start++;
    }
}
// Implementation of citoa()
char* itoa(int num, char* str, int base)
{
    int i = 0;
    bool isNegative = false;
 
    /* Handle 0 explicitly, otherwise empty string is
     * printed for 0 */
    if (num == 0) {
        str[i++] = '0';
        str[i] = '\0';
        return str;
    }
 
    // In standard itoa(), negative numbers are handled
    // only with base 10. Otherwise numbers are
    // considered unsigned.
    if (num < 0 && base == 10) {
        isNegative = true;
        num = -num;
    }
 
    // Process individual digits
    while (num != 0) {
        int rem = num % base;
        str[i++] = (rem > 9) ? (rem - 10) + 'a' : rem + '0';
        num = num / base;
    }
 
    // If number is negative, append '-'
    if (isNegative)
        str[i++] = '-';
 
    str[i] = '\0'; // Append string terminator
 
    // Reverse the string
    reverse(str, i);
 
    return str;
}

int paris_speed(int transmision_speed)//simple recalculation of paris speed to Sample count
{
   return SAMPLE_SPEED*60/50/transmision_speed;
}

int square(int x)
{
   return x*x;
}

long long calculate_det_treshold(int treshold,int cnt)//treshold for detection;
{
   return 2*(long long)treshold *(long long)treshold * (long long)cnt;
}

int* get_AM(int freq,int sample_speed,int* ret_len)//returns 0x3fff*(1+e^(ikt)) and lenght of arr
{  
   int len = sample_speed/freq;
   int nos_freq= 16;
   //printf("len %i\n",len);
   *ret_len = len;
   int* arr = (int*)malloc(2 * len * sizeof(int));

   for(int i=0;i<len;i++)
   {
      arr[i]    =(int)((double)0x3fff*(1.0+sin(2*M_PI*((double)i/(double)len)))*sin(2*M_PI*((double)i/(double)len)));
      arr[i+len]=(int)((double)0x3fff*(1.0+cos(2*M_PI*((double)i/(double)len)))*sin(2*M_PI*((double)i/(double)len)));
      //printf("%i %i\n",arr[i],arr[i+len]);
   }
   return arr;


}

int* get_sinus(int freq,int sample_speed,int* ret_len)//returns 0x7fff*(e^(ikt)) and lenght of arr
{  
   int len = sample_speed/freq;
   
   //printf("len %i\n",len);
   *ret_len = len;
   int* arr = (int*)malloc(2 * len * sizeof(int));

   for(int i=0;i<len;i++)
   {
      arr[i]    =(int)((double)0x7ff0*(sin(2*M_PI*((double)i/(double)len))));
      arr[i+len]=(int)((double)0x7ff0*(cos(2*M_PI*((double)i/(double)len))));

      //printf("%i %i\n",arr[i],arr[i+len]);
   }
   return arr;


}
#define RAMP_LEN 2000 // ramps are 2KSmp long ~ 5ms at 2.5MSmp/s
#define RAMP_SIZE 0x400 //ramp peak later need to be shifted down by ten to the right

int* get_on_ramp()//returns 2^10*on ramp to be shifted later
{
   int* arr = (int*)malloc(RAMP_LEN * sizeof(int));
   for(int i=0;i<RAMP_LEN;i++)
   {
      arr[i] =(int)((double)(RAMP_SIZE<<1)*(1-cos(M_PI*(double)i/(double)RAMP_LEN))); 
   }
   return arr;
}


int* get_off_ramp()//returns 2^10*on ramp to be shifted later
{
   int* arr = (int*)malloc(RAMP_LEN * sizeof(int));
   for(int i=0;i<RAMP_LEN;i++)
   {
      arr[i] =(int)((double)(RAMP_SIZE<<1)*(1+cos(M_PI*(double)i/(double)RAMP_LEN))); 
   }
   return arr;
}


void* keybord_input_thread(void* d)
{data_t *data = (data_t*)d;
   while(1)
   {
      pthread_mutex_lock(&mtx_data);
      bool quit = data->quit;
      pthread_mutex_unlock(&mtx_data);
      
      if(quit) 
         break;  
      sleep(1);

   }
   return 0;
}
void* mqtt_thread(void* d)
{  
   data_t *data = (data_t*)d;

   const char* addr = MQTT_ADRESS;
   const char* port = MQTT_PORT;
   const char* topics[]={MQTT_MSG_TOPIC ,
                        MQTT_FREQUENCY_RX_TOPIC,
                        MQTT_FREQUENCY_TX_TOPIC,
                        MQTT_REFRESH_SETTINGS,
                        MQTT_PARIS_SPEED_SETTING,
                        MQTT_TONE_SETTING,
                        MQTT_SPACE_SETTING};

   //fprintf(stderr, "\x1b[34mINFO\x1b[0m: 2");
   // open the non-blocking TCP socket (connecting to the broker)
   int sockfd = open_nb_socket(addr, port);

   if (sockfd == -1) {
      perror("Failed to open socket: ");
      exit_example(EXIT_FAILURE, sockfd, NULL);
   }  

   // setup a client
   struct mqtt_client client;
   uint8_t sendbuf[2048]; // sendbuf should be large enough to hold multiple whole mqtt messages 
   uint8_t recvbuf[1024]; // recvbuf should be large enough any whole mqtt message expected to be received 
   mqtt_init(&client, sockfd, sendbuf, sizeof(sendbuf), recvbuf, sizeof(recvbuf), publish_callback);
   // Create an anonymous session 
   const char* client_id = NULL;
   // Ensure we have a clean session 
   uint8_t connect_flags = MQTT_CONNECT_CLEAN_SESSION;
   // Send connection request to the broker. 
   mqtt_connect(&client, client_id, NULL, NULL, 0, NULL, NULL, connect_flags, 400);

   // check that we don't have any errors 
   if (client.error != MQTT_OK) {
      fprintf(stderr, "error: %s\n", mqtt_error_str(client.error));
      exit_example(EXIT_FAILURE, sockfd, NULL);
   }
   //fprintf(stderr, "\x1b[34mINFO\x1b[0m: 3");
   // start a thread to refresh the client (handle egress and ingree client traffic) 
   pthread_t client_daemon;
   if(pthread_create(&client_daemon, NULL, client_refresher, &client)) {
      fprintf(stderr, "Failed to start client daemon.\n");
      exit_example(EXIT_FAILURE, sockfd, NULL);

   }

   //subscribe 
   for(int i=0;i<MQTT_SUBSCRIBE_CNT;i++)
   {
      mqtt_subscribe(&client, topics[i], 0);
      fprintf(stderr,"Pluto listening for '%s' messages.\n",  topics[i]);
   }
   pthread_mutex_lock(&mtx_data);
   data->client_rdy = true;
   pthread_mutex_unlock(&mtx_data);
   
   


   
   char data_out_str[DATA_OUT_SIZE*5+1];//+- 4096
   data_out_str[DATA_OUT_SIZE*5]='\0';//set last as end
   int32_t data_out_int[DATA_OUT_SIZE];

   while(1)
   {
      pthread_mutex_lock(&mtx_data);
      bool quit = data->quit;
      bool data_refresh = data->refresh;
      bool new_data = data->new_data;
      bool new_recieved_msg = data->new_recived_msg; 
      pthread_mutex_unlock(&mtx_data);
      if(quit) 
         break;
      if(new_data)
      {
         pthread_mutex_lock(&mtx_data);
         data->new_data=false;
         memcpy(data_out_int,data->data_out,sizeof(int32_t)*DATA_OUT_SIZE);
         pthread_mutex_unlock(&mtx_data);
         for(int i=0;i<DATA_OUT_SIZE;i++)
         {
            int n=data_out_int[i];
            if(n>=0)
            {
               data_out_str[5*i]='+';
            }
            else
            {
               n=-n;
               data_out_str[5*i]='-';
            }

            int n1=n/1000;
            int n2=(n-1000*n1)/100;
            int n3=(n-1000*n1-100*n2)/10;
            int n4=(n-1000*n1-100*n2-10*n3);
            data_out_str[5*i+1]=n1+'0';
            data_out_str[5*i+2]=n2+'0';
            data_out_str[5*i+3]=n3+'0';
            data_out_str[5*i+4]=n4+'0';
         }


         mqtt_publish(&client, MQTT_PUBLISH_DATA_OUT, data_out_str,  DATA_OUT_SIZE*5+1 , MQTT_PUBLISH_QOS_0);
      }
      if(data_refresh)
      {
         char s_tx[20];
         char s_rx[20];
         char s_speed[20];
         char s_tone[20];
         char s_space[20];
         pthread_mutex_lock(&mtx_data);
         data->refresh = false; //just reset it here
         
         itoa(data->frequncy_rx, s_rx, 10);
         itoa(data->frequncy_tx, s_tx, 10);
         itoa(data->transmission_speed, s_speed , 10);
         itoa(data->sin_freq, s_tone , 10);
         itoa(data->space_betwwen_symbols, s_space , 10);
         pthread_mutex_unlock(&mtx_data); 
         mqtt_publish(&client, MQTT_PUBLISH_TX_FREQ, s_tx, strlen(s_tx) + 1, MQTT_PUBLISH_QOS_0);
         mqtt_publish(&client, MQTT_PUBLISH_RX_FREQ, s_rx, strlen(s_rx) + 1, MQTT_PUBLISH_QOS_0);
         mqtt_publish(&client, MQTT_PUBLISH_PARIS_SPEED, s_speed, strlen(s_speed) + 1, MQTT_PUBLISH_QOS_0);
         mqtt_publish(&client, MQTT_PUBLISH_TONE, s_tone, strlen(s_tone) + 1, MQTT_PUBLISH_QOS_0);
         mqtt_publish(&client, MQTT_PUBLISH_SPACE, s_space, strlen(s_space) + 1, MQTT_PUBLISH_QOS_0);
         printf("\x1b[34mINFO\x1b[0m published settings\n");
      }
      if(new_recieved_msg)
      {
         pthread_mutex_lock(&mtx_data);
         printf("%s  len: %i\n",data->msg_out,data->msg_len);

         mqtt_publish(&client, MQTT_PUBLISH_MSG_OUT , data->msg_out,data->msg_out_len+1,MQTT_PUBLISH_QOS_0);
         data->new_recived_msg = false;
         pthread_mutex_unlock(&mtx_data);
      }
      /*pthread_mutex_lock(&mtx_data);
      if(data->msg_len>0)
         mqtt_publish(&client, MQTT_PUBLISH_MSG_OUT , data->msg_out,data->msg_out_len,MQTT_PUBLISH_QOS_0);
      pthread_mutex_unlock(&mtx_data);*/
        
      usleep(1000*10);//sleep for 10ms

   }
   exit_example(EXIT_SUCCESS, sockfd, &client_daemon);
   return 0;
}
void* webscrape_thread(void* d)
{data_t *data = (data_t*)d;
   while(1)
   {
      pthread_mutex_lock(&mtx_data);
      bool quit = data->quit;
      pthread_mutex_unlock(&mtx_data);
      if(quit) 
         break;  
        
      sleep(1);
      

   }
   return 0;
}
void* transmit_thread(void* d)
{  data_t *data = (data_t*)d;
   char message[MESSAGE_MAX_LEN+1];
   //streaming devices
   struct iio_device *tx;
	struct iio_device *rx;

   // Stream configurations
	struct stream_cfg rxcfg;
	struct stream_cfg txcfg;

   // RX and TX sample counters
	size_t nrx = 0;
	size_t ntx = 0;

   // Listen to ctrl+c and IIO_ENSURE
	signal(SIGINT, handle_sig);

    // RX default stream config
	rxcfg.bw_hz = MHZ(2);   // 2 MHz rf bandwidth
	rxcfg.fs_hz = MHZ(2.5);   // 2.5 MS/s rx sample rate
	rxcfg.lo_hz = 1000*DEFAULT_RX_FREQ; // 2.5 GHz rf frequency
	rxcfg.rfport = "A_BALANCED"; // port A (select for rf freq.)

   // TX stream config
	txcfg.bw_hz = MHZ(1.5); // 1.5 MHz rf bandwidth
	txcfg.fs_hz = MHZ(2.5);   // 2.5 MS/s tx sample rate
	txcfg.lo_hz = 1000*DEFAULT_TX_FREQ; // 2.5 GHz rf frequency
	txcfg.rfport = "A"; // port A (select for rf freq.)
   
   

   fprintf(stderr,"\x1b[34mINFO\x1b[0m Acquiring IIO context\n");
	
	IIO_ENSURE((ctx = iio_create_default_context()) && "No context");
	
	
	IIO_ENSURE(iio_context_get_devices_count(ctx) > 0 && "No devices");

	fprintf(stderr,"\x1b[34mINFO\x1b[0m Acquiring AD9361 streaming devices\n");
	IIO_ENSURE(get_ad9361_stream_dev(TX, &tx) && "No tx dev found");
	IIO_ENSURE(get_ad9361_stream_dev(RX, &rx) && "No rx dev found");

	fprintf(stderr,"\x1b[34mINFO\x1b[0m Configuring AD9361 for streaming\n");
	IIO_ENSURE(cfg_ad9361_streaming_ch(&rxcfg, RX, 0) && "RX port 0 not found");
	IIO_ENSURE(cfg_ad9361_streaming_ch(&txcfg, TX, 0) && "TX port 0 not found");

	fprintf(stderr,"\x1b[34mINFO\x1b[0m Initializing AD9361 IIO streaming channels\n");
	IIO_ENSURE(get_ad9361_stream_ch(RX, rx, 0, &rx0_i) && "RX chan i not found");
	IIO_ENSURE(get_ad9361_stream_ch(RX, rx, 1, &rx0_q) && "RX chan q not found");
	IIO_ENSURE(get_ad9361_stream_ch(TX, tx, 0, &tx0_i) && "TX chan i not found");
	IIO_ENSURE(get_ad9361_stream_ch(TX, tx, 1, &tx0_q) && "TX chan q not found");

	fprintf(stderr,"\x1b[34mINFO\x1b[0m Enabling IIO streaming channels\n");
	iio_channel_enable(rx0_i);
	iio_channel_enable(rx0_q);
	iio_channel_enable(tx0_i);
	iio_channel_enable(tx0_q);

	fprintf(stderr,"\x1b[34mINFO\x1b[0m Creating non-cyclic IIO buffers with 1 MiS\n");
	rxbuf = iio_device_create_buffer(rx, BUFFER_LEN, false);
	if (!rxbuf) {
		perror("Could not create RX buffer");
		tx_shutdown();
	}
	txbuf = iio_device_create_buffer(tx, BUFFER_LEN, false);
	if (!txbuf) {
		perror("Could not create TX buffer");
		tx_shutdown();
	}

   pthread_mutex_lock(&mtx_data);
   
   //seting tx gain to max
   
   int ret = iio_channel_attr_write(iio_device_find_channel(get_ad9361_phy(), "voltage0",false), "gain_control_mode", "manual");
      if (ret < 0) {
      fprintf(stderr, "Failed to set AGC to manual mode: %d\n", ret);
      tx_shutdown();
      }
   
   
   ret = iio_channel_attr_write_longlong(iio_device_find_channel(get_ad9361_phy(), "voltage0",true), "hardwaregain", 0);
      if (ret < 0) {
      fprintf(stderr, "Failed to set TX gain: %d\n", ret);
      return 0;
      }

   ret = iio_channel_attr_write_longlong(iio_device_find_channel(get_ad9361_phy(), "voltage0",false), "hardwaregain", 10);
      if (ret < 0) {
      fprintf(stderr, "Failed to set RX gain: %d\n", ret);
      return 0;
      }
   
   data->frequncy_rx=DEFAULT_RX_FREQ;
   data->frequncy_tx=DEFAULT_TX_FREQ;
   int symbol_len=paris_speed(data->transmission_speed);
   int sinus_freq = data->sin_freq;

   pthread_mutex_unlock(&mtx_data);
   int sinus_len;
   
   int *sinus = get_sinus(sinus_freq,SAMPLE_SPEED,&sinus_len);

   int symbol_counter = 0;//counts symbols used for managing sending of messages since end of symbols does not align with buffer lenghts.
	int counter = 0; // count samples
   int sinus_counter =0;//couns sinus wave samples
   bool no_tx_msg = true;//indicates if NO message is present
   int* bin_msg = NULL;//
   int binary_lenght=0;
   bool quit = false;
   int zeros_cnt=0;
   bool reading_msg = false;
   int samples_to_next_read = 0;
   //int samples_per_period = symbol_len;sample_cnt
   int zeros_in_row = 0;
   int read_val = 0;
   
   //int *data = malloc(MUL*1024*sizeof(int));
   int *buffer_for_detection=(int*)malloc(BUFFER_LEN*sizeof(int));

   int idx = 0;
   float x1=0,x2=0,y1=0,y2=0;//used for keeping last data of the lowpass filter
   float *filter_data_buffer=(float*)malloc(BUFFER_LEN*sizeof(float));
   
   const int skip=32;
   float *filter_data_decimated=(float*)malloc(BUFFER_LEN/skip*sizeof(float));

   int comp_data[BUFFER_LEN/skip];

   int *marker_starts=(int*)malloc(BUFFER_LEN/skip/32*sizeof(int));
   int *marker_ends=(int*)malloc(BUFFER_LEN/skip/32*sizeof(int));
   float last_sample_from_last_set = 0;
   int hystersis_timeout = -1,marker_start_idx = 0,marker_end_idx = 0,sample_cnt = 0;
   bool in_marker = false;

   int *morse_out=(int*)malloc(BUFFER_LEN/skip/32*sizeof(int));
   int morse_len=0;

   int msg_len=0;
   char msg_out[1000];

   int last_idx = 0;

   int *on_ramp = get_on_ramp();
   int *off_ramp = get_off_ramp();

   int last_symbol = 0;
   bool rising_ramp = false;
   bool fallin_ramp = false;
   

   int num1;
   int num2;

   int msg_recieved_timeout = 0;
   //checks memory
   if(buffer_for_detection==NULL)
   {
      printf("Memory not allocated.\n");
      exit(0);
   }
   if(filter_data_decimated==NULL)
   {
      printf("Memory not allocated.\n");
      exit(0);
   }
   if(marker_ends==NULL)
   {
      printf("Memory not allocated.\n");
      exit(0);
   }
   if(filter_data_buffer==NULL)
   {
      printf("Memory not allocated.\n");
      exit(0);
   }
   if(marker_starts==NULL)
   {
      printf("Memory not allocated.\n");
      exit(0);
   }
   if(morse_out==NULL)
   {  
      printf("Memory not allocated.\n");
      exit(0);
   }   
   FILE *log;
   log = fopen("log.txt", "w");

   while(1)
   {
      if(no_tx_msg){//no message attempt to get one
         pthread_mutex_lock(&mtx_data);
         quit = data->quit;
         bool new_message = data->new_message;
         bool new_setting = data->new_settings;

         if(new_message == true)
           {
            memcpy(&message,&data->message,data->message_lenght);
            no_tx_msg = false;
            bin_msg = morse_output(message,strlen(message),&binary_lenght,data->space_betwwen_symbols);//get new message
           }
         data->new_message = false;//data has been saved from shared struct so next message to send can be recieved
         
         if(new_setting)
         {
                txcfg.lo_hz=1000*data->frequncy_tx;
                rxcfg.lo_hz=1000*data->frequncy_rx;
                fprintf(stderr,"\x1b[34mINFO\x1b[0m Reconfiguring AD9361\n");
                IIO_ENSURE(cfg_ad9361_streaming_ch(&rxcfg, RX, 0) && "RX port 0 not found");
                IIO_ENSURE(cfg_ad9361_streaming_ch(&txcfg, TX, 0) && "TX port 0 not found");
                symbol_len=paris_speed(data->transmission_speed);
                free(sinus);
                int *sinus = get_sinus(data->sin_freq,SAMPLE_SPEED,&sinus_len);
                data->new_settings = false;
                //samples_per_period = symbol_len;
               
         }
         pthread_mutex_unlock(&mtx_data);
         
      }
		ssize_t nbytes_rx, nbytes_tx;
		char *p_dat, *p_end;
		ptrdiff_t p_inc;
      //int begin = clock();
		// Schedule TX buffer
		nbytes_tx = iio_buffer_push(txbuf);
		if (nbytes_tx < 0) { printf("Error pushing buf %d\n", (int) nbytes_tx); tx_shutdown(); }
      //int end = clock();
      //printf("my_lowpass:%f ms\n",1000*(double)(end - begin) / CLOCKS_PER_SEC);
		// Refill RX buffer
		nbytes_rx = iio_buffer_refill(rxbuf);
		if (nbytes_rx < 0) { printf("Error refilling buf %d\n",(int) nbytes_rx); tx_shutdown(); }

		// READ: Get pointers to RX buf and read IQ from RX buf port 0
		p_inc = iio_buffer_step(rxbuf);
		p_end = iio_buffer_end(rxbuf);


		int max_q,max_i;
      int write_jump = BUFFER_LEN/DATA_OUT_SIZE;
      int write_counter = 0;
		pthread_mutex_lock(&mtx_data);
      data->new_data = true;
      pthread_mutex_unlock(&mtx_data);
      
      
      
      int skip_jump = 1;
      long long my_max=0;
      long long my_sum=0;
      long long my_min=0x7ffffff0;
      int detect_counter = 0 ;
      int detect_sample_counter = 0;
      
      int max_ones_in_row;
      bool last = false;
      int det_len = 15;

      idx = 0;

      p_inc = iio_buffer_step(rxbuf);
		p_end = iio_buffer_end(rxbuf);
      

      clock_t begin = clock();

      for (p_dat = (char *)iio_buffer_first(rxbuf, rx0_i); p_dat < p_end; p_dat += (p_inc * 1))
         {
         num1 = ((int16_t*)p_dat)[0];
         num2 = ((int16_t*)p_dat)[1];
         
         buffer_for_detection[idx] = num1*num1 + num2*num2;//square(num1)+square(num2);//square(((int16_t*)p_dat)[0])+square(((int16_t*)p_dat)[1]);
         idx++;
         }
      //printf("size of array:%i",idx);
      

      clock_t end = clock();
      printf("get_data_from_RX_buffer:%f ms\n",1000*(double)(end - begin) / CLOCKS_PER_SEC);
      

      //begin = clock();
      my_lowpass(buffer_for_detection,BUFFER_LEN,filter_data_buffer,
                 &y1,&y2,&x1,&x2);
      
      //end = clock();
      //printf("my_lowpass:%f ms\n",1000*(double)(end - begin) / CLOCKS_PER_SEC);


      //begin = clock();
      my_decimate(filter_data_buffer,filter_data_decimated,BUFFER_LEN,skip);
      //end = clock();
      //printf("my_decimate:%f ms\n",1000*(double)(end - begin) / CLOCKS_PER_SEC);


      /*for(int i = 0;i<BUFFER_LEN/skip;i++)
      {fprintf(log,"%f\n",filter_data_decimated[i]);}*/

      //begin = clock();
      treshold_comp(filter_data_decimated,comp_data,BUFFER_LEN/skip,FILTER_LEVEL);
      //end = clock();
      //printf("my_treshold_comp:%f ms\n",1000*(double)(end - begin) / CLOCKS_PER_SEC);
      
      //begin = clock();
      my_find_markers(marker_starts,marker_ends,comp_data,&last_sample_from_last_set,
      &hystersis_timeout,BUFFER_LEN/skip,&marker_start_idx,&marker_end_idx,&sample_cnt,&in_marker);
      //end = clock();
      //printf("my_find_markers:%f ms\n",1000*(double)(end - begin) / CLOCKS_PER_SEC);
      //for(int i = 0;i<BUFFER_LEN/skip;i++)
      //{fprintf(log,"%i\n",comp_data[i]);}
      
      
      printf("rising edge_idx %d falling edge_idx %d\n",marker_start_idx,marker_end_idx);
      
      if(((last_idx-marker_start_idx)==0)&&(marker_start_idx!=0))//no new markers , must mean end of transmission
      {
         msg_recieved_timeout++;
         if(msg_recieved_timeout==5)
         {
         morse_len = 0;
         msg_len = 0;
         decode_sequence(morse_out,marker_starts,marker_ends,marker_start_idx,symbol_len/skip,&morse_len);
         morse_to_string(msg_out,morse_out,morse_len,&msg_len);
         printf("lens:%i %i",morse_len,msg_len);
         pthread_mutex_lock(&mtx_data);
         memcpy(data->msg_out,msg_out,1000*sizeof(char));
         data->msg_out[msg_len]='\0';//terminate string
         data->msg_out_len = msg_len;
         data->new_recived_msg = true;
         printf("\nMESSAGE RECIEVED: %s \n",data->msg_out);    
         pthread_mutex_unlock(&mtx_data);
         
         
         marker_start_idx = 0;
         marker_end_idx = 0;
         sample_cnt = 0;
         msg_recieved_timeout = 0;   
         }
      }
      /*if((marker_start_idx==0)&&(marker_end_idx==1))//happens at start of program
      {
         marker_start_idx = 0;
         marker_end_idx = 0;
      }*/

      last_idx = marker_start_idx;
      //for (p_dat = (char *)iio_buffer_first(rxbuf, rx0_i); p_dat < p_end; p_dat += (p_inc * skip_jump)) {//p_inc * write_jump moves tru buffer skiping elements
			/*
			const int16_t i = ((int16_t*)p_dat)[0]; // Real (I)
			const int16_t q = ((int16_t*)p_dat)[1]; // Imag (Q)
			
         my_sum = my_sum + (long long)i * (long long)i + (long long)q * (long long)q;
         detect_counter++;
         if(detect_counter == 1024)
         {
   int last_idx = 0;xt_read);(sinus_counter>=sinus_len)
               {//printf("new period\n");
               sinus_counter=0;
            if((samples_to_next_read<=0)&&(reading_msg))
            {  printf("%i ",read_val);
               read_arr[read_counter] = read_val;
               read_counter++;
               read_arr[read_counter] = 0;   
               samples_to_next_read = samples_to_next_read + samples_per_period;
               
               if(read_val == 1)
                  zeros_in_row = 0;
               else{
                  zeros_in_row++;
                  if((zeros_in_row>10)&&(reading_msg==true))(sinus_counter>=sinus_len)
               {//printf("new period\n");
               sinus_counter=0;
                     {reading_msg = false;
                     
                  
                  
                     for(int i=0;i<read_counter;i++)
                        printf("%i ",read_arr[i]);
                     
                     printf("\n");
                     zeros_in_row = 0;
               }
               read_val = 0;
               
               
            }
            		pthread_mutex_lock(&mtx_data);sample_cnt

            
            }

            

            if(my_sum>calculate_det_treshold(1700,1024))//1 detected
               {
               if(!reading_msg){//start new msg
                  printf("new msg");
                  reading_msg = true;
                  read_counter = 0;
                  samples_to_next_read = samples_per_period;}
               read_val++;// = 1;
               }
            
            my_sum = 0;
            samples_to_next_read = samples_to_next_read - 1024;
         }
      }
      */
         

         //fprintf(f,"%i %i\n",i,q);
			
			//((int16_t*)p_dat)[0] = q;
			//((int16_t*)p_dat)[1] = i;

		
      //printf("\n\nnew max %lli new min %lli",my_max,my_min);
      

		// WRITE: Get pointers to TX buf and write IQ to TX buf port 0
		p_inc = iio_buffer_step(txbuf);
		p_end = iio_buffer_end(txbuf);
		int val;
		int i= 0;
		int flip = 0;
		//begin = clock();
      if(no_tx_msg)
         for (p_dat = (char *)iio_buffer_first(txbuf, tx0_i); p_dat < p_end; p_dat += p_inc) {//no msg is present fill buffer with zeros
			

			// https://wiki.analog.com/resources/eval/user-guides/ad-fmcomms2-ebz/software/basic_iq_datafiles#binary_format
			((int16_t*)p_dat)[0] = 0;//real[counter]; // Real (I)
			((int16_t*)p_dat)[1] = 0;//imag[counter]; // Imag (Q)

		}
      else //we have a message
      {  /*for(int i=0; i<binary_lenght;i++)
            printf("%i",bin_msg[i]);
         //printf("\n");*/
         for (p_dat = (char *)iio_buffer_first(txbuf, tx0_i); p_dat < p_end; p_dat += p_inc) 
         {
            if(rising_ramp)
            {
               ((int16_t*)p_dat)[0] = (bin_msg[symbol_counter]* sinus[sinus_counter]          *on_ramp[counter]) >> 6;//real[counter]; // Real (I)
               ((int16_t*)p_dat)[1] = (bin_msg[symbol_counter]* sinus[sinus_len+sinus_counter]*on_ramp[counter]) >> 6;//imag[counter]; // Imag (Q)

            }
            else if(fallin_ramp)
            {
               ((int16_t*)p_dat)[0] = (bin_msg[symbol_counter]* sinus[sinus_counter]          *off_ramp[counter]) >> 6;//real[counter]; // Real (I)
               ((int16_t*)p_dat)[1] = (bin_msg[symbol_counter]* sinus[sinus_len+sinus_counter]*off_ramp[counter]) >> 6;//imag[counter]; // Imag (Q)
            }
            else
            {// https://wiki.analog.com/resources/eval/user-guides/ad-fmcomms2-ebz/software/basic_iq_datafiles#binary_format
               ((int16_t*)p_dat)[0] = (bin_msg[symbol_counter]* sinus[sinus_counter]          ) << 4;//real[counter]; // Real (I)
               ((int16_t*)p_dat)[1] = (bin_msg[symbol_counter]* sinus[sinus_len+sinus_counter]) << 4;//imag[counter]; // Imag (Q)
            }
            counter++;
            sinus_counter++;
            if(counter == RAMP_LEN)//done ramping
               {
                  rising_ramp = false;
                  fallin_ramp = false;
               }
            
            if(counter>=symbol_len)//symbol is done transmittin
            {  //printf("symbol #%i : is %i\n samples %i",symbol_counter,bin_msg[symbol_counter],counter);
               counter=0;
               if(symbol_counter<binary_lenght)//more to send
               {
                  symbol_counter++;
                  if((last_symbol==0)&&(bin_msg[symbol_counter]==1))
                     rising_ramp = true;
                  
                  else if ((last_symbol==1)&&(bin_msg[symbol_counter]==0))
                     fallin_ramp = true;
                  
                  
               }

               
            }
            if(sinus_counter>=sinus_len)
               {//printf("new period\n");
               sinus_counter=0;
               }	
		   }
      }
      //end = clock();
      //printf("set TX data:%f ms\n",1000*(double)(end - begin) / CLOCKS_PER_SEC);
      if((symbol_counter==binary_lenght)&&(symbol_counter>0))
		{
			printf("MESSAGE SENT\n");
			free(bin_msg);//remove old
			//bin_msg = morse_output(str,strlen(str),&binary_lenght);//get new message
			counter = 0;
			symbol_counter=0;
         no_tx_msg = true;
			//printf("new message_len=%i",binary_lenght);

		}

		//printf("%i \n",i);
		// Sample counter increment and status output
		nrx += nbytes_rx / iio_device_get_sample_size(rx);
		ntx += nbytes_tx / iio_device_get_sample_size(tx);
		printf("\tRX %8.2f MSmp, TX %8.2f MSmp\n", nrx/1e6, ntx/1e6);
        
      if(quit) 
         break;  


   }
   fclose(log);
   free(sinus);
   free(buffer_for_detection);
   free(filter_data_buffer);
   free(filter_data_decimated);
   free(marker_starts);
   free(marker_ends);
   free(morse_out);
   return 0;
}

void* recieve_thread(void* d)
{data_t *data = (data_t*)d;
   while(1)
   {
      pthread_mutex_lock(&mtx_data);
      bool quit = data->quit;
      
      pthread_mutex_unlock(&mtx_data);
      if(quit) 
         break;  
        
      sleep(1);

   }
   return 0;
}




int main() {

   const char *threads_names[] = {"MQTT","TX/RX" };
 
   void* (*thr_functions[])(void*) = { mqtt_thread,transmit_thread};
 
   data_t data = { .quit = false ,
                   .message_lenght = 0 , 
                   .new_message=false, 
                   .client_rdy=false, 
                   .transmission_speed=10, 
                   .sin_freq = 800, 
                   .new_settings=false,
                   .space_betwwen_symbols=3,
                   .new_recived_msg=false};

   pthread_t threads[NUM_THREADS];
   shared_data_pointer = &data;
   pthread_mutex_init(&mtx_data, NULL);


   for (int i = 0; i < NUM_THREADS; ++i) 
   {
      int r = pthread_create(&threads[i], NULL, thr_functions[i], &data);
      fprintf(stderr, "\x1b[34mINFO\x1b[0m: Create thread '%s' %s\n", threads_names[i], ( r == 0 ? "OK" : "FAIL") );
   }
   /*
   printf("heloo?");
   char str[] = "PARIS PARIS PARIS PARIS PARIS PARIS PARIS PARIS PARIS PARIS PARIS PARIS PARIS PARIS PARIS PARIS PARIS PARIS PARIS PARIS";
   int len = strlen(str);
   printf("%i",len);
   int binary_lenght;
   int* bin_msg = morse_output(str,strlen(str),&binary_lenght,3);
   printf("msg1:");
   for(int i=0;i<binary_lenght;i++)
      printf("%i",bin_msg[i]);
   printf("\n");
   free(bin_msg);

   bin_msg = morse_output(str,strlen(str),&binary_lenght,6);
   printf("msg2:");
   for(int i=0;i<binary_lenght;i++)
      printf("%i",bin_msg[i]);
   printf("\n");
   free(bin_msg);*/
   //int len;
   //int *e = get_sinus(16,1024,&len);
   //printf("len:%i\n",len);
   //for(int i=0;i<len;i++)
   //   printf("real:%i img %i\n",e[i],e[i+len]); 
   
   //free(e);

   fprintf(stderr,"\x1b[34mINFO\x1b[0m: Press CTRL-D to exit.\n\n");

    /* block */
   while(fgetc(stdin) != EOF);


   pthread_mutex_lock(&mtx_data);
   data.quit = 1;
   pthread_mutex_unlock(&mtx_data);


   for (int i = 0; i < NUM_THREADS; ++i) 
   {
      fprintf(stderr, "\x1b[34mINFO\x1b[0m: Call join to the thread %s\n", threads_names[i]);
      int r = pthread_join(threads[i], NULL);
      fprintf(stderr, "\x1b[34mINFO\x1b[0m: Joining the thread %s has been %s\n", threads_names[i], (r == 0 ? "OK" : "FAIL"));
   }
   
   
   return 0;
}



void exit_example(int status, int sockfd, pthread_t *client_daemon)
{
    if (sockfd != -1) close(sockfd);
    if (client_daemon != NULL) pthread_cancel(*client_daemon);
    exit(status);
}



void publish_callback(void** unused, struct mqtt_response_publish *published)
{  data_t *data = (data_t*)shared_data_pointer;
   // note that published->topic_name is NOT null-terminated (here we'll change it to a c-string) 
   //another note published->application_message is ALSO NOT null-terminated (here we'll change it to a c-string) 
   char* topic_name = (char*) malloc(published->topic_name_size + 1);
   char* application_message = (char*) malloc(published->application_message_size + 1);
   
   memcpy(topic_name, published->topic_name, published->topic_name_size);
   memcpy(application_message, published->application_message, published->application_message_size);

   topic_name[published->topic_name_size] = '\0';
   application_message[published->application_message_size] = '\0';

   printf("recieve message_lenght: %i\n",published->application_message_size);
   printf("Received publish('%s'): %s\n", topic_name, application_message);
   
   if(strcmp(topic_name,MQTT_MSG_TOPIC)==0)
   {
      pthread_mutex_lock(&mtx_data);

      if(published->application_message_size>MESSAGE_MAX_LEN)
         {
         fprintf(stderr, "\x1b[34mINFO\x1b[0m: message too long cutting it to %i\n",MESSAGE_MAX_LEN);
         memcpy(data->message,application_message,MESSAGE_MAX_LEN);
         data->message[MESSAGE_MAX_LEN]='\0'; //end message
         }
      else //just copy message with termination
         memcpy(data->message,application_message,published->application_message_size);
      data->message[published->application_message_size]='\0';
      data->new_message=true; // signal to pickup new message
      data->message_lenght = published->application_message_size +1;
      printf("msg rec_len:%i\n",data->message_lenght);
      pthread_mutex_unlock(&mtx_data);      
   }   
   else if (strcmp(topic_name,MQTT_FREQUENCY_TX_TOPIC)==0)
   {
      pthread_mutex_lock(&mtx_data);
      //printf("MQTT_FREQUENCY_TX_TOPIC , %i\n",atoi(application_message));
      data->frequncy_tx =(long long) atoi(application_message);
      data->new_settings = true;
      pthread_mutex_unlock(&mtx_data);    
   }   
   else if (strcmp(topic_name,MQTT_FREQUENCY_RX_TOPIC)==0)
   {
      pthread_mutex_lock(&mtx_data);
      //printf("MQTT_FREQUENCY_RX_TOPIC , %i\n",atoi(application_message));
      data->frequncy_rx = (long long) atoi(application_message);
      data->new_settings = true;
      pthread_mutex_unlock(&mtx_data);    
   }
   //copy message to shared data struct
   else if (strcmp(topic_name,MQTT_REFRESH_SETTINGS)==0)
   {
      pthread_mutex_lock(&mtx_data);
      //printf("MQTT_FREQUENCY_RX_TOPIC , %i\n",atoi(application_message));
      data->refresh = true;
      
      pthread_mutex_unlock(&mtx_data);    
   }
   else if (strcmp(topic_name,MQTT_PARIS_SPEED_SETTING)==0)
   {
      pthread_mutex_lock(&mtx_data);
      //printf("MQTT_FREQUENCY_RX_TOPIC , %i\n",atoi(application_message));
      data->transmission_speed = atoi(application_message);
      data->new_settings = true;
      pthread_mutex_unlock(&mtx_data);    
   }
   else if (strcmp(topic_name,MQTT_TONE_SETTING)==0)
   {
      pthread_mutex_lock(&mtx_data);
      //printf("MQTT_FREQUENCY_RX_TOPIC , %i\n",atoi(application_message));
      data->sin_freq = atoi(application_message);
      data->new_settings = true;
      pthread_mutex_unlock(&mtx_data);    
   }
   else if (strcmp(topic_name,MQTT_SPACE_SETTING)==0)
   {
      pthread_mutex_lock(&mtx_data);
      //printf("MQTT_FREQUENCY_RX_TOPIC , %i\n",atoi(application_message));
      data->space_betwwen_symbols = atoi(application_message);
      pthread_mutex_unlock(&mtx_data);    
   }
   free(topic_name);
   free(application_message);
}

void* client_refresher(void* client)
{
    while(1)
    {
        mqtt_sync((struct mqtt_client*) client);
        usleep(100000U);
    }
    return NULL;
}

