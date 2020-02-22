#include <psp2/io/fcntl.h>
#include <psp2/ctrl.h>
#include <psp2/appmgr.h>
#include <psp2/sysmodule.h>
#include <psp2/types.h> 
#include <psp2/kernel/sysmem.h>
#include <psp2/kernel/threadmgr.h> 

#include <vita2d_sys.h>

#define  CLIB_HEAP_SIZE 1 * 1024 * 1024
#define ONPRESS(flag) ((ctrl.buttons & (flag)) && !(ctrl_old.buttons & (flag)))

/* start BG service */
extern int SceBgAppUtil_7C3525B5(int);

/* clib */
extern void* sceClibMspaceCreate(void* base, uint32_t size);

int main() 
{

	char mes1[] = "    BGFTP\nby Graphene";
	char mes2[] = "Press X button to start BGFTP process.";
	char mes3[] = "BGFTP will be active until you peel off LiveArea screen of this application or start\nenlarged memory mode game.";
	char mes4[] = "To disable notifications from BGFTP, go to Settings->Notifications->BGFTP.";

	/* vita2d_sys init*/

	void* mspace;
	void* clibm_base;
	SceUID clib_heap = sceKernelAllocMemBlock("ClibHeap", SCE_KERNEL_MEMBLOCK_TYPE_USER_RW_UNCACHE, CLIB_HEAP_SIZE, NULL);
	sceKernelGetMemBlockBase(clib_heap, &clibm_base);

	mspace = sceClibMspaceCreate(clibm_base, CLIB_HEAP_SIZE);

	vita2d_clib_pass_mspace(mspace);
	vita2d_set_shfb_mode(1);
	vita2d_init();

	vita2d_set_clear_color(0xFF7F7F7F);
	vita2d_pgf *pgf;
	pgf = vita2d_load_default_pgf();

	sceSysmoduleLoadModule(SCE_SYSMODULE_BG_APP_UTIL);
	/*printf("\nBG Application Sample for PS Vita\n");
	printf("Press X button to start BG application\n\n");*/

	SceCtrlData ctrl,ctrl_old={};

	/* main loop */

	while(1){

		ctrl_old = ctrl;
		sceCtrlReadBufferPositive(0, &ctrl, 1);
		if (ONPRESS(SCE_CTRL_CROSS))
			//Start BG Application
			SceBgAppUtil_7C3525B5(0);

		vita2d_start_drawing();

		vita2d_draw_rectangle(0, 100, 960, 444, 0xFF7F7F7F);
		vita2d_draw_rectangle(0, 0, 272, 100, 0xFF7F7F7F);
		vita2d_pgf_draw_text(pgf, 50, 50, RGBA8(30, 26, 38, 255), 1.2f, mes1);
		vita2d_pgf_draw_text(pgf, 20, 150, RGBA8(30, 26, 38, 255), 1.2f, mes2);
		vita2d_pgf_draw_text(pgf, 20, 200, RGBA8(30, 26, 38, 255), 1.2f, mes3);
		vita2d_pgf_draw_text(pgf, 20, 250, RGBA8(30, 26, 38, 255), 1.2f, mes4);

		vita2d_end_drawing();
		//vita2d_wait_rendering_done();
		vita2d_swap_buffers();
	}

	return 0;
}
