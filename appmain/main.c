#include <psp2/sysmodule.h>
#include <psp2/kernel/threadmgr.h> 
#include <psp2/kernel/modulemgr.h> 
#include <psp2/kernel/processmgr.h> 

/*typedef struct sceAppMgrBudgetInfo {
	SceSize size;
	uint32_t mode;
	uint32_t unk_4;
	uint32_t budgetLPDDR2;
	uint32_t freeLPDDR2;
	uint32_t allow0x0E208060;
	uint32_t unk_14;
	uint32_t budget0x0E208060;
	uint32_t free0x0E208060;
	uint32_t unk_20;
	uint32_t unk_24;
	uint32_t budgetPHYCONT;
	uint32_t freePHYCONT;
	uint32_t allow;
	char unk_34[0x20];
	uint32_t unk_54;
	uint32_t budgetCDRAM;
	uint32_t freeCDRAM;
	char reserved_60[0x24];
} sceAppMgrBudgetInfo;

int sceAppMgrGetBudgetInfo(sceAppMgrBudgetInfo*);*/

/* start BG service */
extern int SceBgAppUtil_7C3525B5(int);

int main()
{
	sceSysmoduleLoadModule(SCE_SYSMODULE_BG_APP_UTIL);
	SceBgAppUtil_7C3525B5(0);

	return sceKernelExitProcess(0);
}
