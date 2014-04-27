#include <unistd.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <limits.h>
#include <linux/videodev2.h>

/* Check that the camera device exists and is a-priori usable for us */
int checkcam(const char *path)
{
    struct stat st;
    gid_t grps[NGROUPS_MAX];
    int grps_count=-1,i;
    
    /* Sequence point happens before || so it's safe */
    if ( stat(path, &st) == -1 || !S_ISCHR(st.st_mode) || (grps_count=getgroups(NGROUPS_MAX,grps)) == -1 )
        return 0;
    
    /* Let's try to see whether we can directly have a readwrite access */
    if ( ((S_IROTH|S_IWOTH)&st.st_mode) == (S_IROTH|S_IWOTH) || 
        ( geteuid() == st.st_uid && ((S_IRUSR|S_IWUSR)&st.st_mode) == (S_IRUSR|S_IWUSR) ) )
        return 1;
  
    /* Browse through groups user belongs to and see if we can dig something */
    if ( ((S_IRGRP|S_IWGRP)&st.st_mode) == (S_IRGRP|S_IWGRP) )
        for ( i=0; i<grps_count; i++ )
            if ( grps[i] == st.st_gid )
                return 1;
    
    return 0;
}

/* Open the camera device, process errors, returns a handle or -1 in case of error */
/* TODO: opaque type for cross-platform compatibility */
/* TODO: consider O_NONBLOCK for real code in Toxic */
int opencam(const char *path)
{
    return open(path, O_RDWR); 
}

/* Stops camera operation, compatibility wrapper */
int closecam(int fd)
{
    return close(fd);
}

void listcams()
{
    int i,fd;
    
    for (i=0;i<256;i++)
    {
        char namebuf[64];
        sprintf(namebuf,"/dev/video%d",i);
        
        if ( checkcam(namebuf) )
        {
            struct v4l2_capability caps;
            
            /* TODO errno */
            if ( (fd=opencam(namebuf)) == -1 )
            {
                printf("Error opening %s.\n", namebuf);
                goto deviceclosed;
            }
                
            if ( ioctl(fd, VIDIOC_QUERYCAP, &caps) == -1 )
            {
                printf("Error doing ioctl on %s.\n", namebuf);
                goto deviceopen;
            }
            printf("Webcam detected: %s at %s, driver: %s\n", caps.card, namebuf, caps.driver);
            
            if ( (caps.capabilities & (V4L2_CAP_VIDEO_CAPTURE|V4L2_CAP_VIDEO_CAPTURE_MPLANE)) == 0 )
                printf("\tWebcam does not support video capture.");
            else
            {
            }

            
            deviceopen:
            if ( close(fd) == -1 )
                printf("Error closing %s.\n", namebuf);
            deviceclosed: ;
        }
    }
}

int main()
{
    listcams();
}
