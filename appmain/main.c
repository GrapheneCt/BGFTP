#include <psp2/io/fcntl.h>
#include <psp2/ctrl.h>
#include <psp2/appmgr.h>
#include <psp2/sysmodule.h>
#include <psp2/types.h> 
#include <psp2/kernel/sysmem.h>
#include <psp2/kernel/threadmgr.h> 
#include <psp2/shellutil.h>

#include <vita2d_sys.h>

#define  CLIB_HEAP_SIZE 1 * 1024 * 1024
#define ONPRESS(flag) ((ctrl.buttons & (flag)) && !(ctrl_old.buttons & (flag)))
//#define DEBUG

typedef struct sceAppMgrBudgetInfo {
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

int sceAppMgrGetBudgetInfo(sceAppMgrBudgetInfo*);

static int ret = -1;

/* start BG service */
extern int SceBgAppUtil_7C3525B5(int);

/* clib */
extern void* sceClibMspaceCreate(void* base, uint32_t size);

int ctrl(SceSize argc, void* argv)
{
	SceCtrlData ctrl, ctrl_old = {};

	while (1) {

		ctrl_old = ctrl;
		sceCtrlReadBufferPositive(0, &ctrl, 1);
		if (ONPRESS(SCE_CTRL_CROSS))
			ret = SceBgAppUtil_7C3525B5(0);
	}
}

int main() 
{
	char mes1[] = "    BGFTP\nby Graphene";
	char mes2[] = "Press X button to start BGFTP process.";
	char mes22[] = "BGFTP process has started successfully.";
	char mes3[] = "BGFTP will be active until you peel off LiveArea screen of this application or start\nenlarged memory mode game.";
	char mes4[] = "To disable notifications from BGFTP, go to Settings->Notifications->BGFTP.";

	/* vita2d_sys init*/

	void* mspace;
	void* clibm_base;
	SceUID clib_heap = sceKernelAllocMemBlock("ClibHeap", SCE_KERNEL_MEMBLOCK_TYPE_USER_RW_UNCACHE, CLIB_HEAP_SIZE, NULL);
	sceKernelGetMemBlockBase(clib_heap, &clibm_base);

	mspace = sceClibMspaceCreate(clibm_base, CLIB_HEAP_SIZE);

	vita2d_clib_pass_mspace(mspace);
	vita2d_init();

	vita2d_set_vblank_wait(0);
	vita2d_set_clear_color(0xFFBFB6B6);
	vita2d_pgf *pgf;
	pgf = vita2d_load_default_pgf();

	sceSysmoduleLoadModule(SCE_SYSMODULE_BG_APP_UTIL);

#ifdef DEBUG
	char dbg1[21];
	char dbg2[23];
	sceAppMgrBudgetInfo info = { 0 };
	info.size = 0x88;
	sceAppMgrGetBudgetInfo(&info);
	sprintf(dbg1, "Free LPDDR2: %ld MiB", info.freeLPDDR2 / 1024 / 1024);
	sprintf(dbg2, "LPDDR2 Budget: %ld MiB", info.budgetLPDDR2 / 1024 / 1024);
#endif

	/* create ctrl thread */

	SceUID id_ctrl = sceKernelCreateThread("CtrlThread", ctrl, 191, 0x10, 0, 0, NULL);
	sceKernelStartThread(id_ctrl, 0, NULL);

	/* main loop */

	while(1){

		vita2d_start_drawing();
		vita2d_clear_screen();

		vita2d_pgf_draw_text(pgf, 400, 50, RGBA8(30, 26, 38, 255), 1.2f, mes1);

		if (ret == 0)
			vita2d_pgf_draw_text(pgf, 20, 150, RGBA8(30, 26, 38, 255), 1.2f, mes22);
		else
			vita2d_pgf_draw_text(pgf, 20, 150, RGBA8(30, 26, 38, 255), 1.2f, mes2);

		vita2d_pgf_draw_text(pgf, 20, 200, RGBA8(30, 26, 38, 255), 1.2f, mes3);
		vita2d_pgf_draw_text(pgf, 20, 260, RGBA8(30, 26, 38, 255), 1.2f, mes4);

#ifdef DEBUG
		vita2d_pgf_draw_text(pgf, 20, 320, RGBA8(30, 26, 38, 255), 1.2f, dbg1);
		vita2d_pgf_draw_text(pgf, 20, 380, RGBA8(30, 26, 38, 255), 1.2f, dbg2);
#endif
		vita2d_end_drawing();
		vita2d_wait_rendering_done();
		vita2d_end_shfb();
	}

	return 0;
}
