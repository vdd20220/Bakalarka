
#ifndef true
#include  <stdbool.h>
#endif
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
//#include "my_morse.c"

void my_lowpass(int *raw_data,int len_of_data,float *result,
                float* y1,float* y2, float* x1, float* x2)
{//returns lowpassed filtered data
    float a1 = 1.99444642;
    float a2 = -0.9944618;
    float b0 = 1.01321184e-07;
    float b1 = 4.50158158e-04;
    float b2 = 1.0;
    float x0;
    for(int i=0;i<len_of_data;i++)
    {   
        x0 = (float)raw_data[i];
        result[i] = b0*x0 + b1*(*x1) + b2*(*x2) + a1*(*y1) + a2*(*y2);
        *x2 = *x1;
        *x1 = x0;
        *y2 = *y1;
        *y1 = result[i];
        
    }
}


void my_decimate(float *input,float *output,int len_of_data,int skip)
{
    for(int i=0;i<len_of_data;i=i+skip)
        output[i/skip] = input[i];
}

void treshold_comp(float *input,int *output,int len_of_data,float treshold)
{
    float max = 0;
    for(int i=0;i<len_of_data;i++)
    {
        if(input[i]>treshold)
            output[i]=1;
        else
            output[i]=0;
        if(input[i]>max)
            max = input[i];
    }
    //printf("new max:%f",max);
}

void my_find_markers(int marker_starts[],int marker_ends[],int filtered_data[],
                     float *last_sample_from_last_set,int* hystersis_timeout,int data_len,
                     int* marker_starts_index,int* marker_ends_index,int *sample_cnt,bool *in_marker)
{
const int const_timeout=64;
//bool in_marker = false;


//printf("markers1: %i %i \n",*marker_starts_index,*marker_ends_index);
//printf("*hystersis_timeout *last_sample_from_last_set filtered_data[0]\n|%i|%f|%i \n",*hystersis_timeout,*last_sample_from_last_set,filtered_data[0]);

//deal with starting new sample rollover 
*hystersis_timeout=*hystersis_timeout-1;
if((*in_marker==false)&&(*hystersis_timeout<0)&&(*last_sample_from_last_set==0)&&(filtered_data[0]==1))
    {
    
    marker_starts[*marker_starts_index] = *sample_cnt  + 1 ;// index starts at 0
    *marker_starts_index=*marker_starts_index+1;
    *hystersis_timeout = const_timeout;
    *in_marker = true;
    }
else if((*hystersis_timeout>0)&&(filtered_data[0]==1))
    *hystersis_timeout = const_timeout;
if((*hystersis_timeout==0)&&(in_marker))
{
    marker_ends[*marker_ends_index] = *sample_cnt + 1;
    *marker_ends_index=*marker_ends_index+1;
    *in_marker = false;
}

//printf("markers2: %i %i\n",*marker_starts_index,*marker_ends_index);
for(int i=1;i<data_len;i++)
    {*hystersis_timeout=*hystersis_timeout-1;
    //if(i<8000)
        //printf("hys. timeout %i filterd data %i in_marker %i\n",*hystersis_timeout,filtered_data[i],in_marker);

    if((*in_marker==false)&&(*hystersis_timeout<0)&&(filtered_data[i-1]==0)&&(filtered_data[i]==1))
        {printf("sample start %i\n",i);
        marker_starts[*marker_starts_index] = *sample_cnt + i + 1 ;// index starts at 0
        *marker_starts_index=*marker_starts_index+1;
        *hystersis_timeout = const_timeout;
        *in_marker = true;
        }
    else if((*hystersis_timeout>0)&&(filtered_data[i]==1))
        *hystersis_timeout = const_timeout;
    
    else if((*hystersis_timeout==0)&&(in_marker))
    {   printf("sample end %i\n",i);
        marker_ends[*marker_ends_index] = *sample_cnt + i + 1;
        (*marker_ends_index)++;
        *in_marker = false;
    }
    
}
 *last_sample_from_last_set = filtered_data[data_len-1]; 
 *sample_cnt = *sample_cnt + data_len;//shifts for next set  
}


void decode_sequence(int morse_out[],int marker_starts[],int marker_ends[],
                     int marker_len,int samples_per_dot,int* morse_len)
{
int min_dot_len=0.75*samples_per_dot;// 75% of marker len required
int max_dot_len=1.25*samples_per_dot;// 125% of marker len max
int min_dash_len = 2.5*samples_per_dot;
int max_dash_len = 3.5*samples_per_dot;
int min_space_between_letters = 2.5*samples_per_dot;
int max_space_between_letters = 3.5*samples_per_dot;
int min_space_between_words = 6*samples_per_dot;

int letter = 0;

for(int i=0;i<marker_len;i++)
{
    if((min_dot_len<=marker_ends[i]-marker_starts[i])&&(marker_ends[i]-marker_starts[i]<=max_dot_len))//is a dot?
        {printf(".");
        letter = 10*letter + 1;
        }    
    //morse_out[*morse_len++] = 1;
    else if((min_dash_len<=marker_ends[i]-marker_starts[i])&&(marker_ends[i]-marker_starts[i]<=max_dash_len))//is a dash?
        {printf("-");
        letter = 10*letter + 3;
        }    
    //morse_out[*morse_len++] = 3;
    else //fail to identify
        {
        letter = -1;    
        printf("?");    
        }
    //morse_out[*morse_len++] = 0;
    
    if(i!=(marker_len-1))
        {
        if((min_space_between_letters<=marker_starts[i+1]-marker_ends[i])&&(marker_starts[i+1]-marker_ends[i]<=max_space_between_letters))//new letter
            {morse_out[*morse_len] = letter;
            letter = 0;
            (*morse_len)++;
            printf("|");
            }
        else if(min_space_between_words<=marker_starts[i+1]-marker_ends[i])
            {//morse_out[*morse_len++] = -7;
                morse_out[*morse_len] = letter;
                letter = 0;
                (*morse_len)++;
                morse_out[*morse_len] = -7;
                (*morse_len)++;
            
                printf(" ");
            }
        }

    
}
    morse_out[*morse_len] = letter;//dump last letter
    letter = 0;
    (*morse_len)++;

}
#define MY_SIZE 983000 
/*int main()
{
    FILE *in_data = fopen("../comparison_data.txt","r");
    int buffer[MY_SIZE];
    for(int i=0;i<MY_SIZE;i++)
        fscanf(in_data,"%i",&buffer[i]);

    

    int *marker_stars = malloc(1024*sizeof(int));
    int *marker_ends = malloc(1024*sizeof(int));
    float last_sample=0;
    int hystersis_timeout=-1,marker_start_idx=0,marker_end_idx=0,sample_cnt=0;
    bool in_marker = false;

    my_find_markers(marker_stars,marker_ends,buffer,&last_sample,&hystersis_timeout,MY_SIZE,
                    &marker_start_idx,&marker_end_idx,&sample_cnt,&in_marker);
    
    
    for(int i=0;i<marker_start_idx;i++)
        printf("marker start idx #%i : %i\n",i,marker_stars[i]);
    for(int i=0;i<marker_end_idx;i++)
        printf("marker end idx #%i : %i\n",i,marker_ends[i]);   
    
    int morse[1000];
    int morse_len;
    decode_sequence(morse,marker_stars,marker_ends,marker_start_idx,9390,&morse_len);
    
    printf("\n%i\n",morse_len);
    for(int i=0;i<morse_len;i++)
        printf("%i|",morse[i]);
    
    char english[1000];
    int char_len;
    morse_to_string(english,morse,morse_len,&char_len);

    for(int i=0;i<char_len;i++)
    printf("%c",english[i]);

    return 0;
}

*/
//#define MUL 1024
/*int main()
{
    
    
    FILE *u_data = fopen("unfilterd_data.txt","w");
    FILE *f_data = fopen("filterd_data.txt","w");
    FILE *d_data = fopen("decimated_filterd_data.txt","w");
    
    if(u_data==NULL)
        {perror("file u_data not opend");
        return 1;}
    if(f_data==NULL)
        {perror("file f_data not opend");
        return 1;}sudo ssh -t root@192.168.2.1 4*sizeof(int));
    //printf("%i\n",rand()%256);
    
    for(int i=0;i<MUL*1024;i++)
    {
        if(((32*MUL)<=i)&&(i<(64*MUL)))
            data[i]=MUL-128+rand()%256;
        else if(((128*MUL)<=i)&&(i<(224*MUL)))
            data[i]=MUL-128+rand()%256;
        else
            data[i]=0-128+rand()%256;
        fprintf(u_data,"%i\n",data[i]);
    }
    
    fclose(u_data);
    
    
    float y1=0,y2=0,x1=0,x2=0;
    float *filterd_data = malloc(MUL*1024*sizeof(float));
    
    
    my_lowpass(data,MUL*1024,filterd_data,&y1,&y2,&x1,&x2);
    
    
    for(int i=0;i<MUL*1024;i++)sudo ssh -t root@192.168.2.1 
        fprintf(f_data,"%f\n",filterd_data[i]);

    fclose(f_data);
    
    float *decimated_filterd_data = malloc(MUL*1024/32*sizeof(float));
    
    my_decimate(filterd_data,decimated_filterd_data,MUL*1024,32);

    for(int i=0;i<MUL*1024/32;i++)
        fprintf(d_data,"%f\n",decimated_filterd_data[i]);

    fclose(d_data);
    
    int *tresholds = malloc(MUL*1024/32*sizeof(int));

    treshold_comp(decimated_filterd_data,tresholds,MUL*1024/32,1e6);

    int *marker_stars = malloc(1024*sizeof(int));
    int *marker_ends = malloc(1024*sizeof(int));
    float last_sample=0;
    int hystersis_timeout=0,marker_start_idx=0,marker_end_idx=0,sample_cnt=0;
    my_find_markers(marker_stars,marker_ends,tresholds,&last_sample,&hystersis_timeout,MUL*1024/32,
                    &marker_start_idx,&marker_end_idx,&sample_cnt);
    

    printf("%i",marker_end_idx);

    free(data);
    free(filterd_data);
    free(decimated_filterd_data);
    
return 0;
} */