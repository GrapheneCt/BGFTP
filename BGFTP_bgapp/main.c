/*
 * Copyright (c) 2020 Graphene
 */

#include <registrymgr.h>
#include <appmgr.h>
#include <libsysmodule.h>
#include <kernel.h>
#include <apputil.h>
#include <libnetctl.h>
#include <notification_util.h>
#include <incoming_dialog.h>
#include <ces.h>

#include "ftpvita.h"

 // User main thread parameters
const char		sceUserMainThreadName[] = "BGFTP_bgserv";
int				sceUserMainThreadPriority = SCE_KERNEL_DEFAULT_PRIORITY_USER;
unsigned int	sceUserMainThreadStackSize = SCE_KERNEL_STACK_SIZE_DEFAULT_USER_MAIN;

// Libc parameters
unsigned int	sceLibcHeapSize = 14 * 1024 * 1024;

typedef struct SceAppMgrEvent {
	int		event;			/* Event ID */
	SceUID	appId;			/* Application ID. Added when required by the event */
	char	param[56];		/* Parameters to pass with the event */
} SceAppMgrEvent;

/* appmgr */
extern int _sceAppMgrReceiveEvent(SceAppMgrEvent *appEvent);

void sendNotification(const char *text, ...)
{
	SceNotificationUtilSendParam param;
	uint32_t inSize, outSize;

	char buf[SCE_NOTIFICATION_UTIL_TEXT_MAX / sizeof(uint16_t)];
	va_list argptr;
	va_start(argptr, text);
	sceClibVsnprintf(buf, sizeof(buf), text, argptr);
	va_end(argptr);

	sceClibMemset(&param, 0, sizeof(SceNotificationUtilSendParam));

	SceCesUcsContext context;
	sceCesUcsContextInit(&context);
	sceCesUtf8StrToUtf16Str(
		&context,
		(uint8_t *)buf,
		SCE_NOTIFICATION_UTIL_TEXT_MAX / sizeof(uint16_t),
		&inSize,
		(uint16_t *)param.text,
		SCE_NOTIFICATION_UTIL_TEXT_MAX / sizeof(uint16_t),
		&outSize);

	sceNotificationUtilSendNotification(&param);
}

void ftpvita_init_app()
{
	char vita_ip[16];
	int state;
	unsigned short int vita_port;

	ftpvita_init(vita_ip, &vita_port);

	ftpvita_add_device("ux0:");
	ftpvita_add_device("ur0:");
	ftpvita_add_device("uma0:");
	ftpvita_add_device("imc0:");

	ftpvita_add_device("gro0:");
	ftpvita_add_device("grw0:");

	ftpvita_add_device("os0:");
	ftpvita_add_device("pd0:");
	ftpvita_add_device("sa0:");
	ftpvita_add_device("tm0:");
	ftpvita_add_device("ud0:");
	ftpvita_add_device("vd0:");
	ftpvita_add_device("vs0:");

	ftpvita_add_device("app0:");
	ftpvita_add_device("savedata0:");

	sendNotification("IP: %s\nPort: %i", vita_ip, vita_port);
}

int main()
{
	/* network check */

	SceAppMgrEvent appEvent;
	int plane, wifi, dialogShown;
	uint32_t inSize, outSize;
	dialogShown = 0;

	sceRegMgrGetKeyInt("/CONFIG/NET", "wifi_flag", &wifi);
	sceRegMgrGetKeyInt("/CONFIG/SYSTEM", "flight_mode", &plane);

	if (!wifi || plane) {

		sceSysmoduleLoadModule(SCE_SYSMODULE_INCOMING_DIALOG);

		sceIncomingDialogInit(0);

		SceIncomingDialogParam params;
		sceIncomingDialogParamInit(&params);
		sceClibStrncpy((char *)params.titleId, "GRVA00002", sizeof(params.titleId));
		params.timeout = 0x7FFFFFF0;

		SceCesUcsContext context;

		sceCesUcsContextInit(&context);
		sceCesUtf8StrToUtf16Str(
			&context,
			"OK",
			SCE_NOTIFICATION_UTIL_TEXT_MAX / sizeof(uint16_t),
			&inSize,
			(uint16_t *)params.acceptText,
			0x20,
			&outSize);

		sceCesUcsContextInit(&context);
		sceCesUtf8StrToUtf16Str(
			&context,
			"Wi-Fi is disabled or system is in airplane mode.",
			95,
			&inSize,
			(uint16_t *)params.dialogText,
			0x40,
			&outSize);

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

	/* BG application*/

	sceSysmoduleLoadModule(SCE_SYSMODULE_NOTIFICATION_UTIL);
	sceNotificationUtilBgAppInitialize();
	sendNotification("BGFTP has started successfully.");

	/* ftpvita */

	sceSysmoduleLoadModule(SCE_SYSMODULE_NET);

	ftpvita_set_info_log_cb(sendNotification);
	ftpvita_init_app();

	/* main loop */

	while (1) {
		sceKernelPowerTick(SCE_KERNEL_POWER_TICK_DISABLE_AUTO_SUSPEND);
		sceKernelDelayThread(10000);
	}
}
