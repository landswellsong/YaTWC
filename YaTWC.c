#include <unistd.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

void listcams()
{
    short i;
    int fd;
    
    for (i=0;i<256;i++)
    {
        char namebuf[64];
        sprintf(namebuf,"/dev/video%d",i);
        
        if ( (fd=open(namebuf,O_RDWR)) != -1 )
        {
            close(fd);
            printf("Webcam detected: %s\n",namebuf);
        }
    }
}

int main()
{
    listcams();
}
