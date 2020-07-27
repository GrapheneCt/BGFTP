/*
 * Copyright (c) 2015-2016 Sergi Granell (xerpi)
 * Copyright (c) 2020 Graphene
 */

#include "ftpvita.h"
#include "libc.h"

#include <stdio.h>
#include <string.h>
#include <sys/syslimits.h>

#include <psp2/kernel/threadmgr.h>
#include <psp2/kernel/clib.h>

#include <psp2/kernel/iofilemgr.h>

#include <psp2/net/net.h>
#include <psp2/net/netctl.h>

#include <psp2/fiber.h>

#include <psp2/rtc.h>

#define UNUSED(x) (void)(x)

#define NET_CTL_ERROR_NOT_TERMINATED 0x80412102

#define FTP_PORT 1337
#define NET_INIT_SIZE (64 * 1024)
#define DEFAULT_FILE_BUF_SIZE (4 * 1024 * 1024)

#define FTP_DEFAULT_PATH   "/"

#define MAX_DEVICES 20
#define MAX_CUSTOM_COMMANDS 20
#define MAX_ASYNC_IO_HANDLES 6

/* PSVita paths are in the form:
 *     <device name>:<filename in device>
 * for example: cache0:/foo/bar
 * We will send Unix-like paths to the FTP client, like:
 *     /cache0:/foo/bar
 */

extern int SceNotificationUtil_DE6F33F4(const char*);

char* sceClibStrchr(char* str, int character);

typedef struct {
	const char *cmd;
	cmd_dispatch_func func;
} cmd_dispatch_entry;

static struct {
	char name[PATH_MAX];
	int valid;
} device_list[MAX_DEVICES];

static struct {
	const char *cmd;
	cmd_dispatch_func func;
	int valid;
} custom_command_dispatchers[MAX_CUSTOM_COMMANDS];

static void *net_memory = NULL;
static int ftp_initialized = 0;
static unsigned int file_buf_size = DEFAULT_FILE_BUF_SIZE;
static SceNetInAddr vita_addr;
static SceUID server_thid;
static int server_sockfd;
static int number_clients = 0;
static ftpvita_client_info_t *client_list = NULL;
static SceUID client_list_mtx;
static SceUID async_io_handle[MAX_ASYNC_IO_HANDLES];

static int netctl_init = -1;
static int net_init = -1;

static void (*info_log_cb)(const char *) = NULL;
static void (*debug_log_cb)(const char *) = NULL;

size_t strlen(const char * str)
{
	return sceLibcStrlen(str);
}

static void log_func(ftpvita_log_cb_t log_cb, const char *s, ...)
{
	if (log_cb) {
		char buf[256];
		va_list argptr;
		va_start(argptr, s);
		sceClibVsnprintf(buf, sizeof(buf), s, argptr);
		va_end(argptr);
		log_cb(buf);
	}
}

#define INFO(...) log_func(info_log_cb, __VA_ARGS__)
#define DEBUG(...) log_func(debug_log_cb, __VA_ARGS__)

#define client_send_ctrl_msg(cl, str) \
	sceNetSend(cl->ctrl_sockfd, str, strlen(str), 0)

static inline void client_send_data_msg(ftpvita_client_info_t *client, const char *str)
{
	if (client->data_con_type == FTP_DATA_CONNECTION_ACTIVE) {
		sceNetSend(client->data_sockfd, str, sceLibcStrlen(str), 0);
	} else {
		sceNetSend(client->pasv_sockfd, str, sceLibcStrlen(str), 0);
	}
}

static inline int client_recv_data_raw(ftpvita_client_info_t *client, void *buf, unsigned int len)
{
	if (client->data_con_type == FTP_DATA_CONNECTION_ACTIVE) {
		return sceNetRecv(client->data_sockfd, buf, len, 0);
	} else {
		return sceNetRecv(client->pasv_sockfd, buf, len, 0);
	}
}

static inline void client_send_data_raw(ftpvita_client_info_t *client, const void *buf, unsigned int len)
{
	if (client->data_con_type == FTP_DATA_CONNECTION_ACTIVE) {
		sceNetSend(client->data_sockfd, buf, len, 0);
	} else {
		sceNetSend(client->pasv_sockfd, buf, len, 0);
	}
}

static inline const char *get_vita_path(const char *path)
{
	if (sceLibcStrlen(path) > 1)
		/* /cache0:/foo/bar -> cache0:/foo/bar */
		return &path[1];
	else
		return NULL;
}

static int file_exists(const char *path)
{
	SceIoStat stat;
	return (sceIoGetstat(path, &stat) >= 0);
}

static void cmd_NOOP_func(ftpvita_client_info_t *client)
{
	client_send_ctrl_msg(client, "200 No operation ;)" FTPVITA_EOL);
}

static void cmd_USER_func(ftpvita_client_info_t *client)
{
	client_send_ctrl_msg(client, "331 Username OK, need password b0ss." FTPVITA_EOL);
}

static void cmd_PASS_func(ftpvita_client_info_t *client)
{
	client_send_ctrl_msg(client, "230 User logged in!" FTPVITA_EOL);
}

static void cmd_QUIT_func(ftpvita_client_info_t *client)
{
	client_send_ctrl_msg(client, "221 Goodbye senpai :'(" FTPVITA_EOL);
}

static void cmd_SYST_func(ftpvita_client_info_t *client)
{
	client_send_ctrl_msg(client, "215 UNIX Type: L8" FTPVITA_EOL);
}

static void cmd_PASV_func(ftpvita_client_info_t *client)
{
	int ret;
	UNUSED(ret);

	char cmd[512];
	unsigned int namelen;
	SceNetSockaddrIn picked;

	/* Create data mode socket name */
	char data_socket_name[64];
	sceClibSnprintf(data_socket_name, 0x470, "FTPVita_client_%i_data_socket",
		client->num);

	/* Create the data socket */
	client->data_sockfd = sceNetSocket(data_socket_name,
		SCE_NET_AF_INET,
		SCE_NET_SOCK_STREAM,
		0);

	DEBUG("PASV data socket fd: %d\n", client->data_sockfd);

	/* Fill the data socket address */
	client->data_sockaddr.sin_family = SCE_NET_AF_INET;
	client->data_sockaddr.sin_addr.s_addr = sceNetHtonl(SCE_NET_INADDR_ANY);
	/* Let the PSVita choose a port */
	client->data_sockaddr.sin_port = sceNetHtons(0);

	/* Bind the data socket address to the data socket */
	ret = sceNetBind(client->data_sockfd,
		(SceNetSockaddr *)&client->data_sockaddr,
		sizeof(client->data_sockaddr));
	DEBUG("sceNetBind(): 0x%08X\n", ret);

	/* Start listening */
	ret = sceNetListen(client->data_sockfd, 128);
	DEBUG("sceNetListen(): 0x%08X\n", ret);

	/* Get the port that the PSVita has chosen */
	namelen = sizeof(picked);
	sceNetGetsockname(client->data_sockfd, (SceNetSockaddr *)&picked,
		&namelen);

	DEBUG("PASV mode port: 0x%04X\n", picked.sin_port);

	/* Build the command */
	sceClibSnprintf(cmd, 0x470, "227 Entering Passive Mode (%hhu,%hhu,%hhu,%hhu,%hhu,%hhu)" FTPVITA_EOL,
		(vita_addr.s_addr >> 0) & 0xFF,
		(vita_addr.s_addr >> 8) & 0xFF,
		(vita_addr.s_addr >> 16) & 0xFF,
		(vita_addr.s_addr >> 24) & 0xFF,
		(picked.sin_port >> 0) & 0xFF,
		(picked.sin_port >> 8) & 0xFF);

	client_send_ctrl_msg(client, cmd);

	/* Set the data connection type to passive! */
	client->data_con_type = FTP_DATA_CONNECTION_PASSIVE;
}

static void cmd_PORT_func(ftpvita_client_info_t *client)
{
	unsigned int data_ip[4];
	unsigned int porthi, portlo;
	unsigned short data_port;
	char ip_str[16];
	SceNetInAddr data_addr;

	sceLibcSscanf(client->recv_cmd_args, "%d,%d,%d,%d,%d,%d",
		&data_ip[0], &data_ip[1], &data_ip[2], &data_ip[3],
		&porthi, &portlo);

	data_port = portlo + porthi*256;

	/* Convert to an X.X.X.X IP string */
	sceClibSnprintf(ip_str, 0x470, "%d.%d.%d.%d",
		data_ip[0], data_ip[1], data_ip[2], data_ip[3]);

	/* Convert the IP to a SceNetInAddr */
	sceNetInetPton(SCE_NET_AF_INET, ip_str, &data_addr);

	DEBUG("PORT connection to client's IP: %s Port: %d\n", ip_str, data_port);

	/* Create data mode socket name */
	char data_socket_name[64];
	sceClibSnprintf(data_socket_name, 0x470, "FTPVita_client_%i_data_socket",
		client->num);

	/* Create data mode socket */
	client->data_sockfd = sceNetSocket(data_socket_name,
		SCE_NET_AF_INET,
		SCE_NET_SOCK_STREAM,
		0);

	DEBUG("Client %i data socket fd: %d\n", client->num,
		client->data_sockfd);

	/* Prepare socket address for the data connection */
	client->data_sockaddr.sin_family = SCE_NET_AF_INET;
	client->data_sockaddr.sin_addr = data_addr;
	client->data_sockaddr.sin_port = sceNetHtons(data_port);

	/* Set the data connection type to active! */
	client->data_con_type = FTP_DATA_CONNECTION_ACTIVE;

	client_send_ctrl_msg(client, "200 PORT command successful!" FTPVITA_EOL);
}

static void client_open_data_connection(ftpvita_client_info_t *client)
{
	int ret;
	UNUSED(ret);

	unsigned int addrlen;

	if (client->data_con_type == FTP_DATA_CONNECTION_ACTIVE) {
		/* Connect to the client using the data socket */
		ret = sceNetConnect(client->data_sockfd,
			(SceNetSockaddr *)&client->data_sockaddr,
			sizeof(client->data_sockaddr));

		DEBUG("sceNetConnect(): 0x%08X\n", ret);
	} else {
		/* Listen to the client using the data socket */
		addrlen = sizeof(client->pasv_sockaddr);
		client->pasv_sockfd = sceNetAccept(client->data_sockfd,
			(SceNetSockaddr *)&client->pasv_sockaddr,
			&addrlen);
		DEBUG("PASV client fd: 0x%08X\n", client->pasv_sockfd);
	}
}

static void client_close_data_connection(ftpvita_client_info_t *client)
{
	sceNetSocketClose(client->data_sockfd);
	/* In passive mode we have to close the client pasv socket too */
	if (client->data_con_type == FTP_DATA_CONNECTION_PASSIVE) {
		sceNetSocketClose(client->pasv_sockfd);
	}
	client->data_con_type = FTP_DATA_CONNECTION_NONE;
}

static int gen_list_format(char *out, int n, int dir, const SceIoStat *stat, const char *filename)
{
	static const char num_to_month[][4] = {
		"Jan", "Feb", "Mar", "Apr", "May", "Jun",
		"Jul", "Aug", "Sep", "Oct", "Nov", "Dec"
	};

	char yt[6];
	SceDateTime cdt;
	sceRtcGetCurrentClockLocalTime(&cdt);

	if  (cdt.year == stat->st_mtime.year) {
		sceClibSnprintf(yt, sizeof(yt), "%02d:%02d", stat->st_mtime.hour, stat->st_mtime.minute);
	}
	else {
		sceClibSnprintf(yt, sizeof(yt), "%04d", stat->st_mtime.year);
	}

	return sceClibSnprintf(out, n,
		"%c%s 1 vita vita %u %s %-2d %s %s" FTPVITA_EOL,
		dir ? 'd' : '-',
		dir ? "rwxr-xr-x" : "rw-r--r--",
		(unsigned int) stat->st_size,
		num_to_month[stat->st_mtime.month<=0?0:(stat->st_mtime.month-1)%12],
		stat->st_mtime.day,
		yt,
		filename);
}

static void send_LIST(ftpvita_client_info_t *client, const char *path)
{
	int i, io_num;
	char buffer[512];
	SceUID dir = 0;
	SceIoDirent dirent;
	SceIoStat stat;
	SceIoAsyncParam param;
	char *devname;
	int send_devices = 0;
	io_num = -1;

	/* "/" path is a special case, if we are here we have
	 * to send the list of devices (aka mountpoints). */
	if (sceClibStrcmp(path, "/") == 0) {
		send_devices = 1;
	}

	if (!send_devices) {
		dir = sceIoDopen(get_vita_path(path));
		if (dir < 0) {
			client_send_ctrl_msg(client, "550 Invalid directory." FTPVITA_EOL);
			return;
		}
	}

	client_send_ctrl_msg(client, "150 Opening ASCII mode data transfer for LIST." FTPVITA_EOL);

	client_open_data_connection(client);

	if (send_devices) {
		for (i = 0; i < MAX_DEVICES; i++) {
			if (device_list[i].valid) {
				devname = device_list[i].name;
				if (sceIoGetstat(devname, &stat) >= 0) {
					gen_list_format(buffer, sizeof(buffer),	1, &stat, devname);
					client_send_data_msg(client, buffer);
				}
			}
		}
	} else {
		sceClibMemset(&dirent, 0, sizeof(dirent));

		while (sceIoDread(dir, &dirent) > 0) {
			gen_list_format(buffer, sizeof(buffer), SCE_STM_ISDIR(dirent.d_stat.st_mode),
				&dirent.d_stat, dirent.d_name);
			client_send_data_msg(client, buffer);
			sceClibMemset(&dirent, 0, sizeof(dirent));
			sceClibMemset(buffer, 0, sizeof(buffer));
		}

		for (int i = 0; i < MAX_ASYNC_IO_HANDLES; i++) {
			if (async_io_handle[i] == 0) {
				io_num = i;
				break;
			}
		}

		if (io_num != -1) {
			async_io_handle[io_num] = sceIoDcloseAsync(dir, &param);
			io_num = -1;
		}
		else
			sceIoDclose(dir);
	}

	DEBUG("Done sending LIST\n");

	client_close_data_connection(client);
	client_send_ctrl_msg(client, "226 Transfer complete." FTPVITA_EOL);
}

static void cmd_LIST_func(ftpvita_client_info_t *client)
{
	char list_path[PATH_MAX];
	int list_cur_path = 1;

	int n = sceLibcSscanf(client->recv_cmd_args, "%[^\r\n\t]", list_path);

	if (n > 0 && file_exists(get_vita_path(list_path)))
		list_cur_path = 0;

	if (list_cur_path)
		send_LIST(client, client->cur_path);
	else
		send_LIST(client, list_path);
}

static void cmd_PWD_func(ftpvita_client_info_t *client)
{
	char msg[PATH_MAX];
	sceClibSnprintf(msg, sizeof(msg), "257 \"%s\" is the current directory." FTPVITA_EOL, client->cur_path);
	client_send_ctrl_msg(client, msg);
}

static int path_is_at_root(const char *path)
{
	return sceClibStrrchr(path, '/') == (path + sceLibcStrlen(path) - 1);
}

static void dir_up(char *path)
{
	char *pch;
	size_t len_in = sceLibcStrlen(path);
	if (len_in == 1) {
		sceLibcStrcpy(path, "/");
		return;
	}
	if (path_is_at_root(path)) { /* Case root of the device (/foo0:/) */
		sceLibcStrcpy(path, "/");
	} else {
		pch = sceClibStrrchr(path, '/');
		size_t s = len_in - (pch - path);
		sceClibMemset(pch, '\0', s);
		/* If the path is like: /foo: add slash */
		if (sceClibStrrchr(path, '/') == path)
			sceLibcStrcat(path, "/");
	}
}

static void cmd_CWD_func(ftpvita_client_info_t *client)
{
	char cmd_path[PATH_MAX];
	char tmp_path[PATH_MAX];
	SceUID pd;
	int io_num = -1;
	SceIoAsyncParam param;
	int n = sceLibcSscanf(client->recv_cmd_args, "%[^\r\n\t]", cmd_path);

	if (n < 1) {
		client_send_ctrl_msg(client, "500 Syntax error, command unrecognized." FTPVITA_EOL);
	} else {
		if (sceClibStrcmp(cmd_path, "/") == 0) {
			sceLibcStrcpy(client->cur_path, cmd_path);
		} else  if (sceClibStrcmp(cmd_path, "..") == 0) {
			dir_up(client->cur_path);
		} else {
			if (cmd_path[0] == '/') { /* Full path */
				sceLibcStrcpy(tmp_path, cmd_path);
			} else { /* Change dir relative to current dir */
				/* If we are at the root of the device, don't add
				 * an slash to add new path */
				if (path_is_at_root(client->cur_path))
					sceClibSnprintf(tmp_path, sizeof(tmp_path), "%s%s", client->cur_path, cmd_path);
				else
					sceClibSnprintf(tmp_path, sizeof(tmp_path), "%s/%s", client->cur_path, cmd_path);
			}

			/* If the path is like: /foo: add an slash */
			if (sceClibStrrchr(tmp_path, '/') == tmp_path)
				sceLibcStrcat(tmp_path, "/");

			/* If the path is not "/", check if it exists */
			if (sceClibStrcmp(tmp_path, "/") != 0) {
				/* Check if the path exists */
				pd = sceIoDopen(get_vita_path(tmp_path));
				if (pd < 0) {
					client_send_ctrl_msg(client, "550 Invalid directory." FTPVITA_EOL);
					return;
				}

				for (int i = 0; i < MAX_ASYNC_IO_HANDLES; i++) {
					if (async_io_handle[i] == 0) {
						io_num = i;
						break;
					}
				}

				if (io_num != -1) {
					async_io_handle[io_num] = sceIoDcloseAsync(pd, &param);
					io_num = -1;
				}
				else
					sceIoDclose(pd);
			}
			sceLibcStrcpy(client->cur_path, tmp_path);
		}
		client_send_ctrl_msg(client, "250 Requested file action okay, completed." FTPVITA_EOL);
	}
}

static void cmd_TYPE_func(ftpvita_client_info_t *client)
{
	char data_type;
	char format_control[8];
	int n_args = sceLibcSscanf(client->recv_cmd_args, "%c %s", &data_type, format_control);

	if (n_args > 0) {
		switch(data_type) {
		case 'A':
		case 'I':
			client_send_ctrl_msg(client, "200 Okay" FTPVITA_EOL);
			break;
		case 'E':
		case 'L':
		default:
			client_send_ctrl_msg(client, "504 Error: bad parameters?" FTPVITA_EOL);
			break;
		}
	} else {
		client_send_ctrl_msg(client, "504 Error: bad parameters?" FTPVITA_EOL);
	}
}

static void cmd_CDUP_func(ftpvita_client_info_t *client)
{
	dir_up(client->cur_path);
	client_send_ctrl_msg(client, "200 Command okay." FTPVITA_EOL);
}

static void send_file(ftpvita_client_info_t *client, const char *path)
{
	unsigned char *buffer;
	SceUID fd;
	SceIoStat stat;
	SceIoAsyncParam param;
	unsigned int iterator = 1, bytes_remainder;
	int io_num = -1;

	DEBUG("Opening: %s\n", path);

	for (int i = 0; i < MAX_ASYNC_IO_HANDLES; i++) {
		if (async_io_handle[i] == 0) {
			io_num = i;
			break;
		}
	}

	if (io_num != -1) {
		async_io_handle[io_num] = sceIoGetstatAsync(path, &stat, &param);
		io_num = -1;
	}
	else
		sceIoGetstat(path, &stat);

	if ((fd = sceIoOpen(path, SCE_O_RDONLY, 0777)) >= 0) {

		for (int i = 0; i < MAX_ASYNC_IO_HANDLES; i++) {
			if (async_io_handle[i] == 0) {
				io_num = i;
				break;
			}
		}

		if (io_num != -1) {
			async_io_handle[io_num] = sceIoLseekAsync(fd, (SceOff)client->restore_point, SCE_SEEK_SET, &param);
			io_num = -1;
		}
		else
			sceIoLseek32(fd, client->restore_point, SCE_SEEK_SET);

		stat.st_size = (unsigned int)stat.st_size - client->restore_point;

		/* Calculate number of full iterations */
		bytes_remainder = (unsigned int)stat.st_size;

		while (bytes_remainder > file_buf_size){
			bytes_remainder = (unsigned int)stat.st_size - file_buf_size * iterator;
			if (bytes_remainder > 0)
				iterator++;
		}

		buffer = sceLibcMalloc(file_buf_size);
		if (buffer == NULL) {
			client_send_ctrl_msg(client, "550 Could not allocate memory." FTPVITA_EOL);
			return;
		}

		client_open_data_connection(client);
		client_send_ctrl_msg(client, "150 Opening Image mode data transfer." FTPVITA_EOL);

		/* Send full iterations */
		for (int i = 1; i < iterator; i++) {
			sceIoRead(fd, buffer, file_buf_size);
			client_send_data_raw(client, buffer, file_buf_size);
		}

		/* Send remainder */
		if (bytes_remainder != 0) {
			sceIoRead(fd, buffer, bytes_remainder);
			client_send_data_raw(client, buffer, bytes_remainder);
		}

		for (int i = 0; i < MAX_ASYNC_IO_HANDLES; i++) {
			if (async_io_handle[i] == 0) {
				io_num = i;
				break;
			}
		}

		if (io_num != -1) {
			async_io_handle[io_num] = sceIoCloseAsync(fd, &param);
			io_num = -1;
		}
		else
			sceIoClose(fd);

		sceLibcFree(buffer);
		client->restore_point = 0;
		client_send_ctrl_msg(client, "226 Transfer completed." FTPVITA_EOL);
		client_close_data_connection(client);

	} else {
		client_send_ctrl_msg(client, "550 File not found." FTPVITA_EOL);
	}
}

/* This function generates an FTP full-path with the input path (relative or absolute)
 * from RETR, STOR, DELE, RMD, MKD, RNFR and RNTO commands */
static void gen_ftp_fullpath(ftpvita_client_info_t *client, char *path, size_t path_size)
{
	char cmd_path[PATH_MAX];
	sceLibcSscanf(client->recv_cmd_args, "%[^\r\n\t]", cmd_path);

	if (cmd_path[0] == '/') {
		/* Full path */
		sceClibStrncpy(path, cmd_path, path_size);
	} else {
		if (sceLibcStrlen(cmd_path) >= 5 && cmd_path[3] == ':' && cmd_path[4] == '/') {
			/* Case "ux0:/foo */
			sceClibSnprintf(path, path_size, "/%s", cmd_path);
		} else {
			/* The file is relative to current dir, so
			 * append the file to the current path */
			sceClibSnprintf(path, path_size, "%s/%s", client->cur_path, cmd_path);
		}
	}
}

static void cmd_RETR_func(ftpvita_client_info_t *client)
{
	char dest_path[PATH_MAX];
	gen_ftp_fullpath(client, dest_path, sizeof(dest_path));
	send_file(client, get_vita_path(dest_path));
}

static void receive_file(ftpvita_client_info_t *client, const char *path)
{
	unsigned char *buffer;
	SceUID fd;
	SceIoAsyncParam param;
	int bytes_recv, io_num = -1;;

	DEBUG("Opening: %s\n", path);

	int mode = SCE_O_CREAT | SCE_O_RDWR;
	/* if we resume broken - append missing part
	 * else - overwrite file */
	if (client->restore_point) {
		mode = mode | SCE_O_APPEND;
	}
	else {
		mode = mode | SCE_O_TRUNC;
	}

	if ((fd = sceIoOpen(path, mode, 0777)) >= 0) {

		buffer = sceLibcMalloc(file_buf_size);
		if (buffer == NULL) {
			client_send_ctrl_msg(client, "550 Could not allocate memory." FTPVITA_EOL);
			return;
		}

		client_open_data_connection(client);
		client_send_ctrl_msg(client, "150 Opening Image mode data transfer." FTPVITA_EOL);

		while ((bytes_recv = client_recv_data_raw(client, buffer, file_buf_size)) > 0) {
			sceIoWrite(fd, buffer, bytes_recv);
		}

		for (int i = 0; i < MAX_ASYNC_IO_HANDLES; i++) {
			if (async_io_handle[i] == 0) {
				io_num = i;
				break;
			}
		}

		if (io_num != -1) {
			async_io_handle[io_num] = sceIoCloseAsync(fd, &param);
			io_num = -1;
		}
		else
			sceIoClose(fd);

		sceLibcFree(buffer);
		client->restore_point = 0;
		if (bytes_recv == 0) {
			client_send_ctrl_msg(client, "226 Transfer completed." FTPVITA_EOL);
		} else {
			
			for (int i = 0; i < MAX_ASYNC_IO_HANDLES; i++) {
				if (async_io_handle[i] == 0) {
					io_num = i;
					break;
				}
			}

			if (io_num != -1) {
				async_io_handle[io_num] = sceIoRemoveAsync(path, &param);
				io_num = -1;
			}
			else
				sceIoRemove(path);

			client_send_ctrl_msg(client, "426 Connection closed; transfer aborted." FTPVITA_EOL);
		}
		client_close_data_connection(client);
	} else {
		client_send_ctrl_msg(client, "550 File not found." FTPVITA_EOL);
	}
}

static void cmd_STOR_func(ftpvita_client_info_t *client)
{
	char dest_path[PATH_MAX];
	gen_ftp_fullpath(client, dest_path, sizeof(dest_path));
	receive_file(client, get_vita_path(dest_path));
}

static void delete_file(ftpvita_client_info_t *client, const char *path)
{
	DEBUG("Deleting: %s\n", path);

	if (sceIoRemove(path) >= 0) {
		client_send_ctrl_msg(client, "226 File deleted." FTPVITA_EOL);
	} else {
		client_send_ctrl_msg(client, "550 Could not delete the file." FTPVITA_EOL);
	}
}

static void cmd_DELE_func(ftpvita_client_info_t *client)
{
	char dest_path[PATH_MAX];
	gen_ftp_fullpath(client, dest_path, sizeof(dest_path));
	delete_file(client, get_vita_path(dest_path));
}

static void delete_dir(ftpvita_client_info_t *client, const char *path)
{
	int ret;
	DEBUG("Deleting: %s\n", path);
	ret = sceIoRmdir(path);
	if (ret >= 0) {
		client_send_ctrl_msg(client, "226 Directory deleted." FTPVITA_EOL);
	} else if (ret == 0x8001005A) { /* DIRECTORY_IS_NOT_EMPTY */
		client_send_ctrl_msg(client, "550 Directory is not empty." FTPVITA_EOL);
	} else {
		client_send_ctrl_msg(client, "550 Could not delete the directory." FTPVITA_EOL);
	}
}

static void cmd_RMD_func(ftpvita_client_info_t *client)
{
	char dest_path[PATH_MAX];
	gen_ftp_fullpath(client, dest_path, sizeof(dest_path));
	delete_dir(client, get_vita_path(dest_path));
}

static void create_dir(ftpvita_client_info_t *client, const char *path)
{
	DEBUG("Creating: %s\n", path);

	if (sceIoMkdir(path, 0777) >= 0) {
		client_send_ctrl_msg(client, "226 Directory created." FTPVITA_EOL);
	} else {
		client_send_ctrl_msg(client, "550 Could not create the directory." FTPVITA_EOL);
	}
}

static void cmd_MKD_func(ftpvita_client_info_t *client)
{
	char dest_path[PATH_MAX];
	gen_ftp_fullpath(client, dest_path, sizeof(dest_path));
	create_dir(client, get_vita_path(dest_path));
}

static void cmd_RNFR_func(ftpvita_client_info_t *client)
{
	char path_src[PATH_MAX];
	const char *vita_path_src;
	/* Get the origin filename */
	gen_ftp_fullpath(client, path_src, sizeof(path_src));
	vita_path_src = get_vita_path(path_src);

	/* Check if the file exists */
	if (!file_exists(vita_path_src)) {
		client_send_ctrl_msg(client, "550 The file doesn't exist." FTPVITA_EOL);
		return;
	}
	/* The file to be renamed is the received path */
	sceLibcStrcpy(client->rename_path, vita_path_src);
	client_send_ctrl_msg(client, "350 I need the destination name b0ss." FTPVITA_EOL);
}

static void cmd_RNTO_func(ftpvita_client_info_t *client)
{
	char path_dst[PATH_MAX];
	const char *vita_path_dst;
	/* Get the destination filename */
	gen_ftp_fullpath(client, path_dst,sizeof(path_dst));
	vita_path_dst = get_vita_path(path_dst);

	DEBUG("Renaming: %s to %s\n", client->rename_path, vita_path_dst);

	if (sceIoRename(client->rename_path, vita_path_dst) < 0) {
		client_send_ctrl_msg(client, "550 Error renaming the file." FTPVITA_EOL);
	}

	client_send_ctrl_msg(client, "226 Rename completed." FTPVITA_EOL);
}

static void cmd_SIZE_func(ftpvita_client_info_t *client)
{
	SceIoStat stat;
	char path[PATH_MAX];
	char cmd[64];
	/* Get the filename to retrieve its size */
	gen_ftp_fullpath(client, path, sizeof(path));

	/* Check if the file exists */
	if (sceIoGetstat(get_vita_path(path), &stat) < 0) {
		client_send_ctrl_msg(client, "550 The file doesn't exist." FTPVITA_EOL);
		return;
	}
	/* Send the size of the file */
	sceClibSnprintf(cmd, 0x470, "213 %lld" FTPVITA_EOL, stat.st_size);
	client_send_ctrl_msg(client, cmd);
}

static void cmd_REST_func(ftpvita_client_info_t *client)
{
	char cmd[64];
	sceLibcSscanf(client->recv_buffer, "%*[^ ] %d", &client->restore_point);
	sceClibSnprintf(cmd, 0x470, "350 Resuming at %d" FTPVITA_EOL, client->restore_point);
	client_send_ctrl_msg(client, cmd);
}

static void cmd_FEAT_func(ftpvita_client_info_t *client)
{
	/*So client would know that we support resume */
	client_send_ctrl_msg(client, "211-extensions" FTPVITA_EOL);
	client_send_ctrl_msg(client, " REST STREAM" FTPVITA_EOL);
	client_send_ctrl_msg(client, " UTF8" FTPVITA_EOL);
	client_send_ctrl_msg(client, "211 end" FTPVITA_EOL);
}

static void cmd_OPTS_func(ftpvita_client_info_t *client)
{
	client_send_ctrl_msg(client, "501 bad OPTS" FTPVITA_EOL);
}

static void cmd_APPE_func(ftpvita_client_info_t *client)
{
	/* set restore point to not 0
	restore point numeric value only matters if we RETR file from vita.
	If we STOR or APPE, it is only used to indicate that we want to resume
	a broken transfer */
	client->restore_point = -1;
	char dest_path[PATH_MAX];
	gen_ftp_fullpath(client, dest_path, sizeof(dest_path));
	receive_file(client, get_vita_path(dest_path));
}

#define add_entry(name) {#name, cmd_##name##_func}
static const cmd_dispatch_entry cmd_dispatch_table[] = {
	add_entry(NOOP),
	add_entry(USER),
	add_entry(PASS),
	add_entry(QUIT),
	add_entry(SYST),
	add_entry(PASV),
	add_entry(PORT),
	add_entry(LIST),
	add_entry(PWD),
	add_entry(CWD),
	add_entry(TYPE),
	add_entry(CDUP),
	add_entry(RETR),
	add_entry(STOR),
	add_entry(DELE),
	add_entry(RMD),
	add_entry(MKD),
	add_entry(RNFR),
	add_entry(RNTO),
	add_entry(SIZE),
	add_entry(REST),
	add_entry(FEAT),
	add_entry(OPTS),
	add_entry(APPE),
	{NULL, NULL}
};

static cmd_dispatch_func get_dispatch_func(const char *cmd)
{
	int i;
	for(i = 0; cmd_dispatch_table[i].cmd && cmd_dispatch_table[i].func; i++) {
		if (sceClibStrcmp(cmd, cmd_dispatch_table[i].cmd) == 0) {
			return cmd_dispatch_table[i].func;
		}
	}
	// Check for custom commands
	for(i = 0; i < MAX_CUSTOM_COMMANDS; i++) {
		if (custom_command_dispatchers[i].valid) {
			if (sceClibStrcmp(cmd, custom_command_dispatchers[i].cmd) == 0) {
				return custom_command_dispatchers[i].func;
			}
		}
	}
	return NULL;
}

static void client_list_add(ftpvita_client_info_t *client)
{
	/* Add the client at the front of the client list */
	sceKernelLockMutex(client_list_mtx, 1, NULL);

	if (client_list == NULL) { /* List is empty */
		client_list = client;
		client->prev = NULL;
		client->next = NULL;
	} else {
		client->next = client_list;
		client_list->prev = client;
		client->prev = NULL;
		client_list = client;
	}
	client->restore_point = 0;
	number_clients++;

	sceKernelUnlockMutex(client_list_mtx, 1);
}

static void client_list_delete(ftpvita_client_info_t *client)
{
	/* Remove the client from the client list */
	sceKernelLockMutex(client_list_mtx, 1, NULL);

	if (client->prev) {
		client->prev->next = client->next;
	}
	if (client->next) {
		client->next->prev = client->prev;
	}
	if (client == client_list) {
		client_list = client->next;
	}

	number_clients--;

	sceKernelUnlockMutex(client_list_mtx, 1);
}

static void client_list_thread_end()
{
	ftpvita_client_info_t *it, *next;
	SceUID client_thid;
	const int data_abort_flags = SCE_NET_SOCKET_ABORT_FLAG_RCV_PRESERVATION |
				SCE_NET_SOCKET_ABORT_FLAG_SND_PRESERVATION;

	sceKernelLockMutex(client_list_mtx, 1, NULL);

	it = client_list;

	/* Iterate over the client list and close their sockets */
	while (it) {
		next = it->next;
		client_thid = it->thid;

		/* Abort the client's control socket, only abort
		 * receiving data so we can still send control messages */
		sceNetSocketAbort(it->ctrl_sockfd,
			SCE_NET_SOCKET_ABORT_FLAG_RCV_PRESERVATION);

		/* If there's an open data connection, abort it */
		if (it->data_con_type != FTP_DATA_CONNECTION_NONE) {
			sceNetSocketAbort(it->data_sockfd, data_abort_flags);
			if (it->data_con_type == FTP_DATA_CONNECTION_PASSIVE) {
				sceNetSocketAbort(it->pasv_sockfd, data_abort_flags);
			}
		}

		/* Wait until the client threads ends */
		sceKernelWaitThreadEnd(client_thid, NULL, NULL);

		it = next;
	}

	sceKernelUnlockMutex(client_list_mtx, 1);
}

static int client_thread(SceSize args, void *argp)
{
	char cmd[16];
	cmd_dispatch_func dispatch_func;
	ftpvita_client_info_t *client = *(ftpvita_client_info_t **)argp;

	DEBUG("Client thread %i started!\n", client->num);

	client_send_ctrl_msg(client, "220 FTPVita Server ready." FTPVITA_EOL);

	while (1) {
		sceClibMemset(client->recv_buffer, 0, sizeof(client->recv_buffer));

		client->n_recv = sceNetRecv(client->ctrl_sockfd, client->recv_buffer, sizeof(client->recv_buffer), 0);
		if (client->n_recv > 0) {
			DEBUG("Received %i bytes from client number %i:\n",
				client->n_recv, client->num);

			//INFO("\t%i> %s", client->num, client->recv_buffer);

			/* The command is the first chars until the first space */
			sceLibcSscanf(client->recv_buffer, "%s", cmd);

			client->recv_cmd_args = sceClibStrchr(client->recv_buffer, ' ');
			if (client->recv_cmd_args)
				client->recv_cmd_args++; /* Skip the space */
			else
				client->recv_cmd_args = client->recv_buffer;

			/* Wait 1 ms before sending any data */
			sceKernelDelayThread(1*1000);

			if ((dispatch_func = get_dispatch_func(cmd))) {
				dispatch_func(client);
			} else {
				client_send_ctrl_msg(client, "502 Sorry, command not implemented. :(" FTPVITA_EOL);
			}

		} else if (client->n_recv == 0) {
			/* Value 0 means connection closed by the remote peer */
			INFO("Connection closed by the client %i.", client->num);
			/* Delete itself from the client list */
			client_list_delete(client);
			break;
		} else if (client->n_recv == SCE_NET_ERROR_EINTR) {
			/* Socket aborted (ftpvita_fini() called) */
			INFO("Client %i socket aborted.", client->num);
			break;
		} else {
			/* Other errors */
			INFO("Client %i socket error: 0x%08X", client->num, client->n_recv);
			client_list_delete(client);
			break;
		}
	}

	/* Close the client's socket */
	sceNetSocketClose(client->ctrl_sockfd);

	/* If there's an open data connection, close it */
	if (client->data_con_type != FTP_DATA_CONNECTION_NONE) {
		sceNetSocketClose(client->data_sockfd);
		if (client->data_con_type == FTP_DATA_CONNECTION_PASSIVE) {
			sceNetSocketClose(client->pasv_sockfd);
		}
	}

	DEBUG("Client thread %i exiting!\n", client->num);

	sceLibcFree(client);

	sceKernelExitDeleteThread(0);
	return 0;
}

static int server_thread(SceSize args, void *argp)
{
	int ret;
	UNUSED(ret);

	SceNetSockaddrIn serveraddr;

	DEBUG("Server thread started!\n");

	/* Create server socket */
	server_sockfd = sceNetSocket("FTPVita_server_sock",
		SCE_NET_AF_INET,
		SCE_NET_SOCK_STREAM,
		0);

	DEBUG("Server socket fd: %d\n", server_sockfd);

	/* Fill the server's address */
	serveraddr.sin_family = SCE_NET_AF_INET;
	serveraddr.sin_addr.s_addr = sceNetHtonl(SCE_NET_INADDR_ANY);
	serveraddr.sin_port = sceNetHtons(FTP_PORT);

	/* Bind the server's address to the socket */
	ret = sceNetBind(server_sockfd, (SceNetSockaddr *)&serveraddr, sizeof(serveraddr));
	DEBUG("sceNetBind(): 0x%08X\n", ret);

	/* Start listening */
	ret = sceNetListen(server_sockfd, 128);
	DEBUG("sceNetListen(): 0x%08X\n", ret);

	while (1) {

		/* Check async IO */
		for (int i = 0; i < MAX_ASYNC_IO_HANDLES; i++) {
			if (async_io_handle[i] > 0) {
				if (!sceIoPollAsync(async_io_handle[i]))
					async_io_handle[i] = 0;
			}
		}

		/* Accept clients */
		SceNetSockaddrIn clientaddr;
		int client_sockfd;
		unsigned int addrlen = sizeof(clientaddr);

		DEBUG("Waiting for incoming connections...\n");

		client_sockfd = sceNetAccept(server_sockfd, (SceNetSockaddr *)&clientaddr, &addrlen);
		if (client_sockfd >= 0) {
			DEBUG("New connection, client fd: 0x%08X\n", client_sockfd);

			/* Get the client's IP address */
			char remote_ip[16];
			sceNetInetNtop(SCE_NET_AF_INET,
				&clientaddr.sin_addr.s_addr,
				remote_ip,
				sizeof(remote_ip));

			INFO("Client %i connected,\nIP: %s port: %i",
				number_clients, remote_ip, clientaddr.sin_port);

			/* Create a new thread for the client */
			char client_thread_name[64];
			sceClibSnprintf(client_thread_name, 0x470, "FTPVita_client_%i_thread",
				number_clients);

			SceUID client_thid = sceKernelCreateThread(
				client_thread_name, client_thread,
				0x10000100, 0x10000, 0, 0, NULL);

			DEBUG("Client %i thread UID: 0x%08X\n", number_clients, client_thid);

			/* Allocate the ftpvita_client_info_t struct for the new client */
			ftpvita_client_info_t *client = sceLibcMalloc(sizeof(*client));
			client->num = number_clients;
			client->thid = client_thid;
			client->ctrl_sockfd = client_sockfd;
			client->data_con_type = FTP_DATA_CONNECTION_NONE;
			sceLibcStrcpy(client->cur_path, FTP_DEFAULT_PATH);
			sceClibMemcpy(&client->addr, &clientaddr, sizeof(client->addr));

			/* Add the new client to the client list */
			client_list_add(client);

			/* Start the client thread */
			sceKernelStartThread(client_thid, sizeof(client), &client);
		} else {
			/* if sceNetAccept returns < 0, it means that the listening
			 * socket has been closed, this means that we want to
			 * finish the server thread */
			DEBUG("Server socket closed, 0x%08X\n", client_sockfd);
			break;
		}
	}

	DEBUG("Server thread exiting!\n");

	sceKernelExitDeleteThread(0);
	return 0;
}

int ftpvita_init(char *vita_ip, unsigned short int *vita_port)
{
	int ret;
	int i;
	SceNetInitParam initparam;
	SceNetCtlInfo info;

	if (ftp_initialized) {
		return -1;
	}

	/* Init async io handle array */
	for (int i = 0; i < MAX_ASYNC_IO_HANDLES; i++) {
		async_io_handle[i] = 0;
	}

	/* Init Net */
	ret = sceNetShowNetstat();
	if (ret == 0) {
		DEBUG("Net is already initialized.\n");
		net_init = -1;
	} else if (ret == SCE_NET_ERROR_ENOTINIT) {
		net_memory = sceLibcMalloc(NET_INIT_SIZE);

		initparam.memory = net_memory;
		initparam.size = NET_INIT_SIZE;
		initparam.flags = 0;

		ret = net_init = sceNetInit(&initparam);
		DEBUG("sceNetInit(): 0x%08X\n", net_init);
		if (net_init < 0)
			goto error_netinit;
	} else {
		INFO("Net error: 0x%08X", net_init);
		goto error_netstat;
	}

	/* Init NetCtl */
	ret = netctl_init = sceNetCtlInit();
	DEBUG("sceNetCtlInit(): 0x%08X\n", netctl_init);
	if (netctl_init < 0 && netctl_init != NET_CTL_ERROR_NOT_TERMINATED)
		goto error_netctlinit;

	/* Get IP address */
	ret = sceNetCtlInetGetInfo(SCE_NETCTL_INFO_GET_IP_ADDRESS, &info);
	DEBUG("sceNetCtlInetGetInfo(): 0x%08X\n", ret);
	if (ret < 0)
		goto error_netctlgetinfo;

	/* Return data */
	sceLibcStrcpy(vita_ip, info.ip_address);
	*vita_port = FTP_PORT;

	/* Save the IP of PSVita to a global variable */
	sceNetInetPton(SCE_NET_AF_INET, info.ip_address, &vita_addr);

	/* Create server thread */
	server_thid = sceKernelCreateThread("FTPVita_server_thread",
		server_thread, 0x10000100, 0x10000, 0, 0, NULL);
	DEBUG("Server thread UID: 0x%08X\n", server_thid);

	/* Create the client list mutex */
	client_list_mtx = sceKernelCreateMutex("FTPVita_client_list_mutex", 0, 0, NULL);
	DEBUG("Client list mutex UID: 0x%08X\n", client_list_mtx);

	/* Init device list */
	for (i = 0; i < MAX_DEVICES; i++) {
		device_list[i].valid = 0;
	}

	for (i = 0; i < MAX_CUSTOM_COMMANDS; i++) {
		custom_command_dispatchers[i].valid = 0;
	}

	/* Start the server thread */
	sceKernelStartThread(server_thid, 0, NULL);

	ftp_initialized = 1;

	return 0;

error_netctlgetinfo:
	if (netctl_init == 0) {
		sceNetCtlTerm();
		netctl_init = -1;
	}
error_netctlinit:
	if (net_init == 0) {
		sceNetTerm();
		net_init = -1;
	}
error_netinit:
	if (net_memory) {
		sceLibcFree(net_memory);
		net_memory = NULL;
	}
error_netstat:
	return ret;
}

void ftpvita_fini()
{
	if (ftp_initialized) {
		/* In order to "stop" the blocking sceNetAccept,
		 * we have to close the server socket; this way
		 * the accept call will return an error */
		sceNetSocketClose(server_sockfd);

		/* Wait until the server threads ends */
		sceKernelWaitThreadEnd(server_thid, NULL, NULL);

		/* To close the clients we have to do the same:
		 * we have to iterate over all the clients
		 * and shutdown their sockets */
		client_list_thread_end();

		/* Delete the client list mutex */
		sceKernelDeleteMutex(client_list_mtx);

		client_list = NULL;
		number_clients = 0;

		if (netctl_init == 0)
			sceNetCtlTerm();
		if (net_init == 0)
			sceNetTerm();
		if (net_memory)
			sceLibcFree(net_memory);

		netctl_init = -1;
		net_init = -1;
		net_memory = NULL;
		ftp_initialized = 0;
	}
}

int ftpvita_is_initialized()
{
	return ftp_initialized;
}

int ftpvita_add_device(const char *devname)
{
	int i;
	for (i = 0; i < MAX_DEVICES; i++) {
		if (!device_list[i].valid) {
			sceLibcStrcpy(device_list[i].name, devname);
			device_list[i].valid = 1;
			return 1;
		}
	}
	return 0;
}

int ftpvita_del_device(const char *devname)
{
	int i;
	for (i = 0; i < MAX_DEVICES; i++) {
		if (sceClibStrcmp(devname, device_list[i].name) == 0) {
			device_list[i].valid = 0;
			return 1;
		}
	}
	return 0;
}

void ftpvita_set_info_log_cb(ftpvita_log_cb_t cb)
{
	info_log_cb = cb;
}

void ftpvita_set_debug_log_cb(ftpvita_log_cb_t cb)
{
	debug_log_cb = cb;
}

void ftpvita_set_file_buf_size(unsigned int size)
{
	file_buf_size = size;
}

int ftpvita_ext_add_custom_command(const char *cmd, cmd_dispatch_func func)
{
	int i;
	for (i = 0; i < MAX_CUSTOM_COMMANDS; i++) {
		if (!custom_command_dispatchers[i].valid) {
			custom_command_dispatchers[i].cmd = cmd;
			custom_command_dispatchers[i].func = func;
			custom_command_dispatchers[i].valid = 1;
			return 1;
		}
	}
	return 0;
}

int ftpvita_ext_del_custom_command(const char *cmd)
{
	int i;
	for (i = 0; i < MAX_CUSTOM_COMMANDS; i++) {
		if (sceClibStrcmp(cmd, custom_command_dispatchers[i].cmd) == 0) {
			custom_command_dispatchers[i].valid = 0;
			return 1;
		}
	}
	return 0;
}

void ftpvita_ext_client_send_ctrl_msg(ftpvita_client_info_t *client, const char *msg)
{
	client_send_ctrl_msg(client, msg);
}

void ftpvita_ext_client_send_data_msg(ftpvita_client_info_t *client, const char *str)
{
	client_send_data_msg(client, str);
}
