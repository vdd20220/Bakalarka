#ifndef _STRING_H
    #include <string.h>
#endif
#define NUMBER_OF_CHARS 36
#define SPACE_LENGHT 7
const int morse_chars[2][NUMBER_OF_CHARS]={{'A','B' ,'C' ,'D','E','F' ,'G','H' ,'I','J' ,'K','L' ,'M','N','O','P' ,'Q' ,'R','S','T','U','V' ,'W','X' ,'Y' ,'Z' , '0' , '1' , '2' , '3' , '4' , '5' , '6' , '7' , '8' , '9' } , 
                                           {13 ,3111,3131,311, 1 ,1131,331,1111, 11,1333,313,1311,33 ,31 ,333,1331,3313,131,111, 3 ,113,1113,133,3113,3133,3311,33333,13333,11333,11133,11113,11111,31111,33111,33311,33331}};

int* str_to_morse_arr(char *message,int len)
{
    int* arr = (int*)calloc(len ,sizeof(int));

    for(int i=0;i<len;i++)
    {//printf("%c \n",message[i]);
        for(int j=0;j<NUMBER_OF_CHARS;j++)
            if(message[i]==morse_chars[0][j])
            {
                arr[i] = morse_chars[1][j];
                break;
            }
    }
    
    return arr;        

    

}

int get_morse_character_len(int character,int space)
{   
    if(character==0)//is space
        return space+4;
    
    int char_len = 0;
    while (character>0)//is decodable
    {
        char_len = char_len + character % 10 + 1;//space between . and -
        character = character / 10;
    }
    return char_len + space + 1;//+3 for space between symbols-1 for oalready added
}

int* morse_output(char *message,int len,int* binary_len,int space_btw_sym)
{   printf("%i",len);
    int* msg = str_to_morse_arr(message,len);
    int message_lenght = 0;
    for(int i=0;i<len;i++)  //count message lenght for memory allocation
        {message_lenght=message_lenght+get_morse_character_len(msg[i],space_btw_sym);
        printf("%i add:%i total:%i\n",msg[i],get_morse_character_len(msg[i],space_btw_sym),message_lenght);    
        }
    *binary_len = message_lenght; 
    
    // alocate and initiate
    int* binary_msg = (int*)calloc(message_lenght , sizeof(int));
    
    for(int i=0;i<message_lenght;i++)
        binary_msg[i]=0;

    int write_pos = 0;
    int was_space = 0;
    for(int i=0;i<len;i++)
    {
        printf("pos:%i , symbol:%i",write_pos,msg[i]);
        if(msg[i]==0)//is space
            {if(!was_space)
                write_pos = write_pos+space_btw_sym+1;
             else
                write_pos = write_pos+space_btw_sym+4;
            was_space = 1;
            continue;}
        
        was_space = 0;
        int n5=msg[i]/10000%10;
        int n4=msg[i]/1000 %10;
        int n3=msg[i]/100  %10;
        int n2=msg[i]/10   %10;
        int n1=msg[i]/1    %10;
        int n[] = {n5,n4,n3,n2,n1};
        for(int j=0;j<5;j++)
        {
            printf(" %i ",n[j]);
            if(n[j]==3)
            {   
                
                binary_msg[write_pos] = 1;
                binary_msg[write_pos+1]=1;
                binary_msg[write_pos+2]=1;
                write_pos = write_pos+4;
            }
            if(n[j]==1)
            {
                
                binary_msg[write_pos] = 1;
                write_pos = write_pos+2;
            }
        }
        //printf("\n");
        write_pos = write_pos+ space_btw_sym-1; //space betwwen symbols-1
    }


    free(msg);
    return binary_msg;
    


}

