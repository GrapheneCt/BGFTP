#include <psp2/kernel/processmgr.h>
#include <psp2/sysmodule.h>
#include <psp2/bgapputil.h> 

void _start(unsigned int args, void *argp)
{
	sceSysmoduleLoadModule(SCE_SYSMODULE_BG_APP_UTIL);
	sceBgAppUtilStartBgApp(0);

	sceKernelExitProcess(0);
}
