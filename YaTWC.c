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
#include <assert.h>
#include <stdio.h>              /* Temporarily */

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

/* Convert the image from YUV422 to RGB888 format */
int clamp(int n)
{
    n=n>255? 255 : n;
    return n<0 ? 0 : n;
}
    
void convertimage(unsigned char *inbuf, unsigned char *outbuf, size_t insize)
{
    size_t i;
    
    for (i=0; i<insize/4; i++)
    {
        int y1=     inbuf[i*4+0]<<8;
        int u=      inbuf[i*4+1]-128;
        int y2=     inbuf[i*4+2]<<8;
        int v=      inbuf[i*4+3]-128;
                      
        /*R*/outbuf[i*6+0]=clamp((y1 + (359 * v)) >> 8);
        /*G*/outbuf[i*6+1]=clamp((y1 - (88 * u) - (183 * v)) >> 8);
        /*B*/outbuf[i*6+2]=clamp((y1 + (454 * u)) >> 8);
        
        /*R*/outbuf[i*6+3]=clamp((y2 + (359 * v)) >> 8);
        /*G*/outbuf[i*6+4]=clamp((y2 - (88 * u) - (183 * v)) >> 8);
        /*B*/outbuf[i*6+5]=clamp((y2 + (454 * u)) >> 8);
    }
}

/* TODO: deal with errno */
void listcams()
{
    int i, fd;
    
    for (i=0; i<256; i++)
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
                size_t rgblen;
                unsigned char *databuf;
                unsigned char *rgbbuf;
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
                rgblen=6*buflen/4;
                databuf=malloc(buflen);
                rgbbuf=malloc(sizeof(unsigned char)*rgblen); /* TODO: use uint8_t explicitly */
                
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
                assert(buf.m.userptr == (unsigned long)databuf);
                printf("Received a %d byte image.\n", buf.bytesused);
                
                /* Converting image to RGB888 and outputting it into a file */
                convertimage(databuf, rgbbuf, buflen);
                FILE *fp=fopen("rgbimage.data","w"); /* WARNING no checks, it's temporary code */
                fwrite(rgbbuf, sizeof(char unsigned), rgblen, fp);
                fclose(fp);
                
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
