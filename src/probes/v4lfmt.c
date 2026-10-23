#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <sys/ioctl.h>
#include <linux/videodev2.h>
int main(int argc,char**argv){
  int i; for(i=1;i<argc;i++){
    int fd=open(argv[i],O_RDWR); struct v4l2_capability cap; struct v4l2_fmtdesc f; 
    if(fd<0){perror(argv[i]);continue;}
    memset(&cap,0,sizeof cap); ioctl(fd,VIDIOC_QUERYCAP,&cap);
    printf("%s: %s caps=0x%08x dev_caps=0x%08x %s\n",argv[i],cap.card,cap.capabilities,cap.device_caps,(cap.device_caps&V4L2_CAP_VIDEO_CAPTURE)?"CAPTURE":"(not capture)");
    for(f.index=0,f.type=V4L2_BUF_TYPE_VIDEO_CAPTURE;ioctl(fd,VIDIOC_ENUM_FMT,&f)==0;f.index++){
      struct v4l2_frmsizeenum s; printf("  fmt %.4s (%s)\n",(char*)&f.pixelformat,f.description);
      for(s.index=0,s.pixel_format=f.pixelformat;ioctl(fd,VIDIOC_ENUM_FRAMESIZES,&s)==0&&s.index<6;s.index++)
        if(s.type==V4L2_FRMSIZE_TYPE_DISCRETE) printf("     %ux%u\n",s.discrete.width,s.discrete.height);
    }
    close(fd);
  } return 0; }
