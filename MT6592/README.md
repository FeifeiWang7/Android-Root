### MTK

This is a local exploit to gain root privilege on Huawei 3X.

#### Description

On phone MT658x & MT6592, e.g., Huawei 3X

/dev/graphics/fb0 shell could open 

crw-rw---- system graphics 29, 0 fb0

When displayid is negative, dispif_info[] out of bound, could write to arbitrary address.
 
Could write to thread_info->addr_limit. which is a limitation linux give to every task could access memory area.

For 32 bit android 4+, thread_info->addr_limit is 0xbf000000

mmset a big memory, memset to some pattern, and ioctl to some place

#### Bug Source Code

j608_kernel/drivers/misc/mediatek/video/mt6592/mtkfb.c

	static int mtkfb_ioctl(struct file *file, struct fb_info *info, unsigned int cmd, unsigned long arg)
	{
    		void __user *argp = (void __user *)arg;
    		DISP_STATUS ret = 0;
    		int r = 0;

    		switch (cmd) 
		{
			case MTKFB_GET_DISPLAY_IF_INFORMATION:
			{
				int displayid = 0;
				if (copy_from_user(&displayid, (void __user *)arg, sizeof(displayid)))
				{
					MTKFB_WRAN("[FB]: copy_from_user failed! line:%d \n", __LINE__);
					return -EFAULT;
				}
				MTKFB_INFO("%s, display_id=%d\n", __func__, displayid);
				if (displayid > MTKFB_MAX_DISPLAY_COUNT)
				{
					MTKFB_WRAN("[FB]: invalid display id:%d \n", displayid);
					return -EFAULT;
				}
				dispif_info[displayid].physicalHeight = DISP_GetPhysicalHeight();
				dispif_info[displayid].physicalWidth = DISP_GetPhysicalWidth() ;
				if (copy_to_user((void __user *)arg, &(dispif_info[displayid]),  sizeof(mtk_dispif_info_t)))
				{
					MTKFB_WRAN("[FB]: copy_to_user failed! line:%d \n", __LINE__);
					r = -EFAULT;
				}
				return (r);
			}
			...
		}
	}
