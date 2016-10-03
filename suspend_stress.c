/*
 * Copyright (C) 2014 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/uio.h> 
#include <sys/wait.h>
#include <getopt.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <sys/epoll.h>
#include <sys/timerfd.h>
#include <fcntl.h>

#define ANDROID_LOG_INFO 4

#ifndef LINUX
int print_alog (char *message){
	int fd;
	unsigned char prio=ANDROID_LOG_INFO;
	struct iovec vec[3];
	unsigned char *tag="timerfd";

	fd = open ("/dev/log/main", O_WRONLY);
	if (fd < 0) {
		return 1;
	}
	vec[0].iov_base=(unsigned char *)&prio;
	vec[0].iov_len=1;
	vec[1].iov_base=(void *)tag;
	vec[1].iov_len=strlen(tag)+1;
	vec[2].iov_base=(void *)message;
	vec[2].iov_len=strlen(message)+1;
	writev(fd,vec,3);
	close(fd);
}
#else
#ifndef EPOLLWAKEUP 
#define EPOLLWAKEUP (1 << 29)
#endif

int print_alog (char *message){
	int fd;

	fd = open ("/dev/kmsg", O_WRONLY);
	if (fd < 0) {
		return 1;
	}
	write(fd,message,strlen(message));
	write(fd,"\n",1);
	close(fd);
}

#endif



#define NSEC_PER_SEC (1000*1000*1000)
#define MSEC_PER_SEC 1000
#define NSEC_PER_MSEC (NSEC_PER_SEC/MSEC_PER_SEC)

long long timediff_ns(const struct timespec *a, const struct timespec *b) {
    return ((long long)(a->tv_sec - b->tv_sec)) * NSEC_PER_SEC +
            (a->tv_nsec - b->tv_nsec);
}


int main(int argc, char **argv)
{
    int alarm_time = 3600; //every 60 sec
    int abort_on_failure = 0;
	int pid=fork();
	if (pid<0){ exit(EXIT_FAILURE);}
	if (pid>0){ exit(EXIT_SUCCESS);}
	umask(0);
	int sid=setsid();
	if (sid<0) {exit(EXIT_FAILURE);}
	if ((chdir("/")) <0) {exit(EXIT_FAILURE);}
	close (STDIN_FILENO);
	close (STDOUT_FILENO);
	close (STDERR_FILENO);
	print_alog ("timerfd daemonized");


    int fd = timerfd_create(CLOCK_BOOTTIME_ALARM, 0);
    if (fd < 0) {
        print_alog("timerfd_create failed");
        exit(EXIT_FAILURE);
    }

    struct itimerspec delay;
    bzero (&delay,sizeof(struct itimerspec));
    delay.it_value.tv_sec = alarm_time;

    int epoll_fd = epoll_create(1);
    if (epoll_fd < 0) {
        print_alog("epoll_create failed");
        exit(EXIT_FAILURE);
    }

    struct epoll_event ev;
    bzero (&ev,sizeof(struct epoll_event));
    ev.events = EPOLLIN | EPOLLWAKEUP;
    int ret = epoll_ctl(epoll_fd, EPOLL_CTL_ADD, fd, &ev);
    if (ret < 0) {
        print_alog("epoll_ctl failed");
        exit(EXIT_FAILURE);
    }

    while (1) {
        struct timespec actual_time;
        uint64_t fired = 0;

        ret = timerfd_settime(fd, 0, &delay, NULL);
        if (ret < 0) {
            print_alog("timerfd_settime failed");
            exit(EXIT_FAILURE);
        }


        ret = 0;
        while (ret != 1) {
            struct epoll_event out_ev;
            ret = epoll_wait(epoll_fd, &out_ev, 1, -1);
            if (ret < 0 && errno != EINTR) {
                print_alog("epoll_wait failed");
                exit(EXIT_FAILURE);
            }
        }

	int wakelock_fd;
	if ((wakelock_fd=open("/sys/power/wake_lock",O_RDWR))==-1){
		print_alog ("error opening wakelock");
		exit(EXIT_FAILURE);
	}
	if (write  (wakelock_fd,"upload",strlen("upload"))!=strlen("upload")) {
		print_alog ("error writing to wakelock");
		close (wakelock_fd);
		exit (EXIT_FAILURE);
	}
	print_alog ("written to /sys/power/wake_lock");
	close (wakelock_fd);


//read epoll to release

        ssize_t bytes = read(fd, &fired, sizeof(fired));
        if (bytes < 0) {
            print_alog("read from timer fd failed");
            exit(EXIT_FAILURE);
        } else if (bytes < (ssize_t)sizeof(fired)) {
            print_alog("unexpected read from timer fd");
        }

        if (fired != 1) {
            print_alog("unexpected timer fd fired count");
        }

        


	ret=fork();
	if (ret <0) {
		print_alog ("Couldn't fork");
		exit (EXIT_FAILURE);
	}
	if (ret==0) {
#ifndef LINUX
		execl ("/data/local/scripts/upload","/data/local/scripts/upload",NULL);
#else
		//exit(EXIT_SUCCESS);
		execl ("/bin/sh","/bin/sh", "/root/upload",NULL);
#endif
		_exit(EXIT_FAILURE);
		
	} else {
	
	
	int i;
	if (waitpid (ret,&i,0) != ret) {
		print_alog("error waiting for child to exit");
		exit(EXIT_FAILURE);
		}

	//exit(EXIT_SUCCESS);


	if ((wakelock_fd=open("/sys/power/wake_unlock",O_RDWR))==-1){
		print_alog ("error opening wakelock");
		exit(EXIT_FAILURE);
	}
	if (write  (wakelock_fd,"upload",strlen("upload"))!=strlen("upload")) {
		print_alog ("error writing to wakelock");
		close (wakelock_fd);		
		exit (EXIT_FAILURE);
	}
	close (wakelock_fd);

/*	int ofd;
	char buffer[30];
	if ((ofd=open ("/sdcard/timer-fired",O_RDWR|O_CREAT|O_APPEND,0666))<0){
	print_alog ("Error opening log");
	exit(EXIT_FAILURE);
	}
	ret = clock_gettime(CLOCK_REALTIME, &actual_time);
        if (ret < 0) {
            print_alog("failed to get time");
            exit(EXIT_FAILURE);
        }

	sprintf(buffer,"%d.%d\n",actual_time.tv_sec,actual_time.tv_nsec);
	write (ofd,buffer,strlen(buffer));
	close (ofd);
*/
    }
    
}
return 0;
}
