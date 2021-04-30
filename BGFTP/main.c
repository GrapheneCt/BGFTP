#include <libsysmodule.h>
#include <bgapputil.h>
#include <kernel.h>
#include <kernel/process_param.h>

// User main thread parameters
const char		sceUserMainThreadName[] = "BGFTP_boot";
int				sceUserMainThreadPriority = SCE_KERNEL_HIGHEST_PRIORITY_USER;
unsigned int	sceUserMainThreadStackSize = SCE_KERNEL_THREAD_STACK_SIZE_MIN;

extern unsigned int	sce_process_preload_disabled = (SCE_PROCESS_PRELOAD_DISABLED_LIBDBG | SCE_PROCESS_PRELOAD_DISABLED_LIBC \
	| SCE_PROCESS_PRELOAD_DISABLED_LIBCDLG | SCE_PROCESS_PRELOAD_DISABLED_LIBFIOS2 | SCE_PROCESS_PRELOAD_DISABLED_LIBPERF \
	| SCE_PROCESS_PRELOAD_DISABLED_APPUTIL | SCE_PROCESS_PRELOAD_DISABLED_LIBSCEFT2 | SCE_PROCESS_PRELOAD_DISABLED_LIBPVF);

void common_exit(int status)
{
	sceKernelExitProcess(status);
}

void abort()
{
	common_exit(0);
}

void exit(int status)
{
	common_exit(status);
}

void __at_quick_exit(int status)
{
	common_exit(status);
}

void __cxa_atexit(int status)
{
	common_exit(status);
}

void __cxa_set_dso_handle_main(void *dso)
{

}

int _sceLdTlsRegisterModuleInfo()
{
	return 0;
}

int __aeabi_unwind_cpp_pr0()
{
	return 9;
}

int __aeabi_unwind_cpp_pr1()
{
	return 9;
}

int main()
{
	sceSysmoduleLoadModule(SCE_SYSMODULE_BG_APP_UTIL);
	sceBgAppUtilStartBgApp(0);

	return 0;
}
