#include <libsysmodule.h>
#include <bgapputil.h> 

int main()
{
	sceSysmoduleLoadModule(SCE_SYSMODULE_BG_APP_UTIL);
	sceBgAppUtilStartBgApp(0);

	return 0;
}
