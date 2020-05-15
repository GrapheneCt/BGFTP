/*
 * Copyright (c) 2020 Graphene
 */

#include <psp2/kernel/threadmgr.h>
#include <psp2/kernel/processmgr.h>
#include <psp2/registrymgr.h> 
#include <psp2/appmgr.h> 
#include <psp2/sysmodule.h>
#include <psp2/io/fcntl.h> 
#include <psp2/kernel/sysmem.h> 
#include <psp2/kernel/clib.h> 
#include <psp2/apputil.h> 

#include <ftpvita.h>

typedef struct SceIncomingDialogParam {
	int fw; //ex. 0x1650041
	char titleid[0x10]; //ex. "PCSA00044" (icon0.png of that app will be shown in dialog window)
	char audioPath[0x80]; //ex. "app0:resource/CallRingingIn.mp3" .at9 and .aac also supported (audio will be played while dialog is active, playback is done by sceShell)
	unsigned int dialogTimer; //Time to show dialog in seconds (including audio)
	int unk_BC; //ex. 1
	char reserved1[0x3E];
	char buttonRightText[0x3E]; //UTF-16 (function - accept. Opens app from titleid)
	short separator0; //must be 0
	char buttonLeftText[0x3E]; //UTF-16 (function - reject). If 0, only right button will be created
	short separator1; //must be 0
	char dialogWindowText[0x100]; //UTF-16 (also displayed in first notification)
	short separator2; //must be 0
} SceIncomingDialogParam;

typedef struct SceAppMgrEvent { // size is 0x64
	int event;						/* Event ID */
	SceUID appId;						/* Application ID. Added when required by the event */
	char  param[56];		/* Parameters to pass with the event */
} SceAppMgrEvent;

#define CLIB_HEAP_SIZE 14 * 1024 * 1024
int _newlib_heap_size_user = 128 * 1024;

/* appmgr */
extern int _sceAppMgrReceiveEvent(SceAppMgrEvent *appEvent);

/* init BG notification */
extern int SceNotificationUtilBgApp_CBE814C1(void);

/* send notification request */
extern int SceNotificationUtil_DE6F33F4(const char*);

/* incoming dialog */
extern int SceIncomingDialog_18AF99EB(int);
extern int SceIncomingDialog_2BEDC1A0(const SceIncomingDialogParam*);

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
	sceClibMemset(msgbuf, 0, 0x470);
}

void sendNotificationCustom()
{
	copycon(msgbuf, msg);
	SceNotificationUtil_DE6F33F4(msgbuf);
	sceClibMemset(msgbuf, 0, 0x470);
	sceClibMemset(msg, 0, 0x470);
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

	sceClibSnprintf(msg, 0x470, "IP: %s\nPort: %i", vita_ip, vita_port);
	sendNotificationCustom();
}

int main(void) 
{
	/* network check */

	SceAppMgrEvent appEvent;
	int plane, wifi, dialogShown;
	dialogShown = 0;

	sceRegMgrGetKeyInt("/CONFIG/NET", "wifi_flag", &wifi);
	sceRegMgrGetKeyInt("/CONFIG/SYSTEM", "flight_mode", &plane);

	if (wifi == 0 || plane == 1) {

		sceSysmoduleLoadModule(SCE_SYSMODULE_INCOMING_DIALOG);

		SceIncomingDialog_18AF99EB(0);

		SceIncomingDialogParam params;
		sceClibMemset(&params, 0, sizeof(SceIncomingDialogParam));
		params.fw = 0x3570011;
		sceClibStrncpy(params.titleid, "BGFTP0011", sizeof(params.titleid));
		params.dialogTimer = 0x7FFFFFF0;
		params.unk_BC = 1;
		copycon(params.buttonRightText, "OK");
		copycon(params.dialogWindowText, "Wi-Fi is disabled or system is in airplane mode.\n   Please enable Wi-Fi and start BGFTP again.");
		SceIncomingDialog_2BEDC1A0(&params);

		while (1) {
			_sceAppMgrReceiveEvent(&appEvent);
			if (appEvent.event == 0x20000004 && dialogShown)
				sceAppMgrDestroyAppByAppId(-2);
			else if (appEvent.event == 0x20000004)
				dialogShown = 1;
			sceKernelDelayThread(10000);
		}
	}

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

	/* main loop */

	while (1) {
		sceKernelPowerTick(SCE_KERNEL_POWER_TICK_DISABLE_AUTO_SUSPEND);
		sceKernelDelayThread(10000);
	}

	return sceAppMgrDestroyAppByAppId(-2);
}
