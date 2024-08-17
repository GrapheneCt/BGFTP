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
#include <kernel/process_param.h>
#include <ces.h>

#include "ftpvita.h"

 // User main thread parameters
const char		sceUserMainThreadName[] = "BGFTP_bgserv";
int				sceUserMainThreadPriority = SCE_KERNEL_DEFAULT_PRIORITY_USER;
unsigned int	sceUserMainThreadStackSize = SCE_KERNEL_STACK_SIZE_DEFAULT_USER_MAIN;

extern unsigned int	sce_process_preload_disabled = (SCE_PROCESS_PRELOAD_DISABLED_LIBDBG \
	| SCE_PROCESS_PRELOAD_DISABLED_LIBCDLG | SCE_PROCESS_PRELOAD_DISABLED_LIBPERF \
	| SCE_PROCESS_PRELOAD_DISABLED_APPUTIL | SCE_PROCESS_PRELOAD_DISABLED_LIBSCEFT2 | SCE_PROCESS_PRELOAD_DISABLED_LIBPVF);

// Libc parameters
unsigned int	sceLibcHeapSize = 14 * 1024 * 1024;

void sendNotification(const char *text, ...)
{
	SceNotificationUtilSendParam param;
	uint32_t inSize, outSize;

	char buf[SCE_NOTIFICATION_UTIL_TEXT_MAX * 2];
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
		SCE_NOTIFICATION_UTIL_TEXT_MAX * 2,
		&inSize,
		(uint16_t *)param.text,
		SCE_NOTIFICATION_UTIL_TEXT_MAX,
		&outSize);

	sceNotificationUtilSendNotification(&param);
}

void ftpvita_init_app()
{
	char vita_ip[16];
	int state;
	unsigned short vita_port;

	ftpvita_set_file_buf_size(6 * 1024 * 1024);

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
	/* BG application*/

	sceSysmoduleLoadModule(SCE_SYSMODULE_NOTIFICATION_UTIL);
	sceNotificationUtilBgAppInitialize();

	/* ftpvita */

	sceSysmoduleLoadModule(SCE_SYSMODULE_NET);

#ifdef _DEBUG
	ftpvita_set_info_log_cb(sendNotification);
#endif
	ftpvita_set_notif_log_cb(sendNotification);

	ftpvita_init_app();

	/* main loop */

	while (1) {
		sceKernelPowerTick(SCE_KERNEL_POWER_TICK_DISABLE_AUTO_SUSPEND);
		sceKernelDelayThread(10000);
	}
}
