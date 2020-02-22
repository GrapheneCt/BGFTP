/*
 * Copyright (c) 2020 Graphene
 */

#include <stdio.h>
#include <string.h>
#include <psp2/kernel/threadmgr.h>
#include <psp2/kernel/processmgr.h> 
#include <psp2/appmgr.h> 
#include <psp2/sysmodule.h>
#include <psp2/io/fcntl.h> 
#include <psp2/types.h>
#include <psp2/kernel/sysmem.h> 
#include <psp2/apputil.h> 
#include <psp2/power.h> 

#include <ftpvita.h>

#define CLIB_HEAP_SIZE 14 * 1024 * 1024

/* init BG notification */
extern int SceNotificationUtilBgApp_CBE814C1(void);

/* send notification request */
extern int SceNotificationUtil_DE6F33F4(const char*);

/* clib */
extern void* sceClibMspaceCreate(void* base, uint32_t size);

static char msgbuf[0x470] = { 0 };
static char msg[0x470] = { 0 };

/* for notification text */
void copycon(char* str1, const char* str2)
{
	while (*str2)
	{
		*str1 = *str2;
		str1++;
		*str1 = '\0';
		str1++;
		str2++;
	}
}

void sendNotificationFixed(const char* str)
{
	copycon(msgbuf, str);
	SceNotificationUtil_DE6F33F4(msgbuf);
	memset(msgbuf, 0, 0x470);
}

void sendNotificationCustom()
{
	copycon(msgbuf, msg);
	SceNotificationUtil_DE6F33F4(msgbuf);
	memset(msgbuf, 0, 0x470);
	memset(msg, 0, 0x470);
}

void ftpvita_init_app()
{
	char vita_ip[16];
	unsigned short int vita_port;

	ftpvita_init(vita_ip, &vita_port);

	ftpvita_add_device("ux0:");
	ftpvita_add_device("ur0:");
	ftpvita_add_device("uma0:");
	ftpvita_add_device("imc0:");

	ftpvita_add_device("os0:");
	ftpvita_add_device("pd0:");
	ftpvita_add_device("sa0:");
	ftpvita_add_device("tm0:");
	ftpvita_add_device("ud0:");
	ftpvita_add_device("vd0:");
	ftpvita_add_device("vs0:");

	ftpvita_add_device("app0:");
	ftpvita_add_device("savedata0:");
	
	ftpvita_add_device("music0:");
	ftpvita_add_device("photo0:");
	/*ftpvita_add_device("video0:");
	ftpvita_add_device("cache0:");
	ftpvita_add_device("empr0:");
	ftpvita_add_device("book0:");*/

	sprintf(msg, "IP: %s\nPort: %i", vita_ip, vita_port);
	sendNotificationCustom();
}

int powerCallback(int notifyId, int notifyCount, int powerInfo, void *common) 
{
	if (powerInfo & SCE_POWER_CB_SUSPENDING)
	{
		ftpvita_fini();
	}
	else if (powerInfo & SCE_POWER_CB_RESUME_COMPLETE) 
	{
		ftpvita_init_app();
	}

	return 0;
}

int main(void) 
{
	/* ftpvita clib mspace*/

	void* mspace;
	void* clibm_base;
	SceUID clib_heap = sceKernelAllocMemBlock("ClibHeap", SCE_KERNEL_MEMBLOCK_TYPE_USER_RW, CLIB_HEAP_SIZE, NULL);
	sceKernelGetMemBlockBase(clib_heap, &clibm_base);

	mspace = sceClibMspaceCreate(clibm_base, CLIB_HEAP_SIZE);

	ftpvita_pass_mspace(mspace);
	
	/* mount virtual drives */

	sceAppMgrAppDataMount(105, "music0:");
	sceAppMgrAppDataMount(100, "photo0:");

	/* load settings - not implemented yet */

	/*SceUID fd = sceIoOpen("ux0:data/ftp_bg/config.ini", SCE_O_RDONLY, 0777);
	sceIoRead(fd, &notificationMode, sizeof(int));
	sceIoClose(fd);*/

	/* BG application*/

	sceSysmoduleLoadModule(SCE_SYSMODULE_NOTIFICATION_UTIL);

	SceNotificationUtilBgApp_CBE814C1();

	sendNotificationFixed("BGFTP has started successfully.");
	
	/* ftpvita */

	sceSysmoduleLoadModule(SCE_SYSMODULE_NET);

	ftpvita_set_info_log_cb(sendNotificationFixed);
	ftpvita_init_app();

	/* emergency callback in case system somehow gets suspended */

	SceUID cbid = sceKernelCreateCallback("Power Callback", 0, powerCallback, NULL);
	scePowerRegisterCallback(cbid);

	/* main loop */

	while (1) {
		sceKernelPowerTick(SCE_KERNEL_POWER_TICK_DISABLE_AUTO_SUSPEND);
		sceKernelDelayThreadCB(100);
	}

	return sceAppMgrDestroyAppByAppId(-2);
}
