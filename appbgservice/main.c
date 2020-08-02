/*
 * Copyright (c) 2020 Graphene
 */

#include <psp2/kernel/threadmgr.h>
#include <psp2/kernel/processmgr.h>
#include <psp2/registrymgr.h> 
#include <psp2/appmgr.h> 
#include <psp2/sysmodule.h>
#include <psp2/kernel/iofilemgr.h> 
#include <psp2/kernel/sysmem.h> 
#include <psp2/kernel/clib.h> 
#include <psp2/apputil.h> 
#include <psp2/notification_util.h> 
#include <psp2/incoming_dialog.h> 
#include <psp2/fiber.h> 

#include <ftpvita.h>

typedef struct SceAppMgrEvent { // size is 0x64
	int event;						/* Event ID */
	SceUID appId;						/* Application ID. Added when required by the event */
	char  param[56];		/* Parameters to pass with the event */
} SceAppMgrEvent;

/* appmgr */
extern int _sceAppMgrReceiveEvent(SceAppMgrEvent *appEvent);

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
	sceNotificationUtilSendNotification((SceWChar16 *)msgbuf);
	sceClibMemset(msgbuf, 0, 0x470);
}

void sendNotificationCustom()
{
	copycon(msgbuf, msg);
	sceNotificationUtilSendNotification((SceWChar16 *)msgbuf);
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

void _start(unsigned int args, void *argp)
{
	/* network check */

	SceAppMgrEvent appEvent;
	int plane, wifi, dialogShown;
	dialogShown = 0;

	sceRegMgrGetKeyInt("/CONFIG/NET", "wifi_flag", &wifi);
	sceRegMgrGetKeyInt("/CONFIG/SYSTEM", "flight_mode", &plane);

	if (!wifi || plane) {

		sceSysmoduleLoadModule(SCE_SYSMODULE_INCOMING_DIALOG);

		sceIncomingDialogInit(0);

		SceIncomingDialogParam params;
		sceClibMemset(&params, 0, sizeof(SceIncomingDialogParam));
		params.sdkVersion = SCE_PSP2_SDK_VERSION;
		sceClibStrncpy((char *)params.titleid, "GRVA00002", sizeof(params.titleid));
		params.dialogTimer = 0x7FFFFFF0;
		params.unk_BC = 1;
		copycon((char *)params.buttonRightText, "OK");
		copycon((char *)params.dialogText, "Wi-Fi is disabled or system is in airplane mode.\n   Please enable Wi-Fi and start BGFTP again.");
		sceIncomingDialogOpen(&params);

		while (1) {
			_sceAppMgrReceiveEvent(&appEvent);
			if (appEvent.event == 0x20000004 && dialogShown)
				sceAppMgrDestroyAppByAppId(-2);
			else if (appEvent.event == 0x20000004)
				dialogShown = 1;
			sceKernelDelayThread(10000);
		}
	}
	
	/* mount virtual drives */

	sceAppMgrAppDataMount(105, "music0:");
	sceAppMgrAppDataMount(100, "photo0:");

	/* BG application*/

	sceSysmoduleLoadModule(SCE_SYSMODULE_NOTIFICATION_UTIL);

	sceNotificationUtilBgAppInitialize();

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
}
