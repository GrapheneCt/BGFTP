#include <psp2/sysmodule.h>
#include <psp2/bgapputil.h> 

extern int sceAppMgrQuitApp(void);

int _newlib_heap_size_user = 128 * 1024;

int main(void)
{
	sceSysmoduleLoadModule(SCE_SYSMODULE_BG_APP_UTIL);
	sceBgAppUtilStartBgApp(0);

	return sceAppMgrQuitApp();
}
