#include <unistd.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <limits.h>             /* for NGROUPS_MAX */
#include <linux/videodev2.h>
#include <string.h>             /* for memset() */
#include <stdlib.h>
#include <errno.h>

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

/* TODO: deal with errno */
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
            
            /* Querying and displaying the capabilities */
            if ( ioctl(fd, VIDIOC_QUERYCAP, &caps) == -1 )
            {
                printf("Error querying capabilities on %s.\n", namebuf);
                goto deviceopen;
            }
            printf("Webcam detected: %s at %s, driver: %s\n", caps.card, namebuf, caps.driver);
            
            /* Let's check if it's a camera at all */
            if ( (caps.capabilities & (V4L2_CAP_VIDEO_CAPTURE|V4L2_CAP_VIDEO_CAPTURE_MPLANE)) == 0 )
            {
                printf("Webcam does not support video capture.");
                goto deviceopen;
            }
            
            /* Streaming IO through a user prointer */
            /* TODO:  implement readwrite fallback */
            if ( caps.capabilities & V4L2_CAP_STREAMING )
            {
                /* TODO: evaluate mmap */
                /* Actual buffers */
                size_t buflen;
                void *databuf;
                struct v4l2_requestbuffers reqbuf;
                struct v4l2_format fmt;
                struct v4l2_buffer buf;
                
                /* Requesting native format info */
                fmt.type=V4L2_BUF_TYPE_VIDEO_CAPTURE;
                if ( ioctl(fd, VIDIOC_G_FMT, &fmt) == -1 )
                {
                    printf("Can not request format information on %s.\n", namebuf);
                    goto deviceopen;
                }
                char *fmtpxfmt=(char*)&fmt.fmt.pix.pixelformat;
                char pixelformat[5] = { fmtpxfmt[0], fmtpxfmt[1], fmtpxfmt[2], fmtpxfmt[3], '\0' };
                printf("Webcam suggestested format: %dx%d %s.\n", fmt.fmt.pix.width, fmt.fmt.pix.height, pixelformat);
                
                /* Allocating the buffer */
                buflen=fmt.fmt.pix.sizeimage;
                databuf=(void*)malloc(buflen);
                
                /* Requesting the buffer through a user pointer */
                memset(&reqbuf, 0, sizeof (reqbuf));
                reqbuf.type=V4L2_BUF_TYPE_VIDEO_CAPTURE;
                reqbuf.memory=V4L2_MEMORY_USERPTR;
                reqbuf.count=1;
               
                if ( ioctl(fd, VIDIOC_REQBUFS, &reqbuf) == -1 )
                {
                    printf("Error requesting buffers on %s.\n", namebuf);
                    goto buffersallocated;
                }            
                
                /* Binding the buffer */
                memset(&buf, 0, sizeof(buf));
                buf.type=V4L2_BUF_TYPE_VIDEO_CAPTURE;
                buf.memory=V4L2_MEMORY_USERPTR;
                buf.index=0;
                buf.m.userptr=(unsigned long)databuf;
                buf.length=buflen;

                if ( ioctl(fd, VIDIOC_QBUF, &buf) == -1 )
                {
                    printf("Can not bind the buffer on %s.\n", namebuf);
                    goto buffersallocated;
                }
                
                /* Starting the stream */
                if ( ioctl(fd, VIDIOC_STREAMON, &fmt.type) == -1 )
                {
                    printf("Can not start the stream on %s.\n", namebuf);
                    goto buffersallocated;
                }
                
                /* Reading a frame out */
                memset(&buf, 0, sizeof(buf));
                buf.type=V4L2_BUF_TYPE_VIDEO_CAPTURE;
                buf.memory=V4L2_MEMORY_USERPTR;
                
                if ( ioctl(fd, VIDIOC_DQBUF, &buf) == -1 )
                {
                    printf("Can not retrieve the buffer on %s.\n", namebuf);
                    goto buffersallocated;
                }
                /* TODO: check if pointers match */
                printf("Received a %d byte image.\n", buf.bytesused);
                
                /* Binding the buffer back */
                if ( ioctl(fd, VIDIOC_QBUF, &buf) == -1 )
                {
                    printf("Can not bind the buffer on %s.\n", namebuf);
                    goto buffersallocated;
                }
                
                /* Stopping the stream */
                if ( ioctl(fd, VIDIOC_STREAMOFF, &fmt.type) == -1 )
                {
                    printf("Can not stop the stream on %s.\n", namebuf);
                    goto buffersallocated;
                }
                
                buffersallocated:
                free(databuf);
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
