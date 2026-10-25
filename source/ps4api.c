/*
 * PS4 Save Remote — LAN HTTP API
 *
 * GET  /api/ping
 * GET  /api/info
 * GET  /api/decrypt?account=HEX&title=CUSAxxxxx&dir=DIRNAME
 * GET  /api/resign?account=HEX&title=CUSAxxxxx&dir=DIRNAME
 * GET  /api/download?file=NAME.zip
 * PUT  /api/upload?name=NAME.zip   (raw body, Content-Length)
 *
 * Encrypted drop zone (also used if you FTP via GoldHEN):
 *   /data/fakeusb/PS4/SAVEDATA/<16-hex-Account-ID>/<TITLE>/<dir> + <dir>.bin
 *
 * Decrypt writes plaintext zip to outbox.
 * Resign reads plaintext from inbox/work and writes sealed package under the
 * same Account-ID folder (Jack in → Jack out).
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <ctype.h>
#include <dirent.h>
#include <time.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <orbis/NetCtl.h>
#include <orbis/SaveData.h>
#include <SDL2/SDL.h>

#include "ps4api.h"
#include "saves.h"
#include "sfo.h"
#include "common.h"
#include "menu.h"
#include "util.h"

extern app_config_t apollo_config;

static volatile int g_run = 0;
static int g_listen_fd = -1;
static SDL_Thread* g_thread = NULL;
static char g_ip[64] = {0};
static char g_status[256] = "idle";

static void set_status(const char* msg)
{
	snprintf(g_status, sizeof(g_status), "%s", msg ? msg : "");
	LOG("ps4api: %s", g_status);
}

static char* find_header_ci(char* hay, const char* needle)
{
	size_t nlen = strlen(needle);
	for (char* p = hay; *p; p++)
	{
		size_t i = 0;
		while (i < nlen && p[i] &&
			tolower((unsigned char)p[i]) == tolower((unsigned char)needle[i]))
			i++;
		if (i == nlen)
			return p;
	}
	return NULL;
}

static int parse_hex_u64(const char* hex, uint64_t* out)
{
	char buf[17];
	char* end = NULL;
	size_t n;

	if (!hex || !out)
		return 0;
	n = strlen(hex);
	if (n != 16)
		return 0;
	for (size_t i = 0; i < 16; i++)
	{
		if (!isxdigit((unsigned char)hex[i]))
			return 0;
		buf[i] = (char)tolower((unsigned char)hex[i]);
	}
	buf[16] = 0;
	*out = strtoull(buf, &end, 16);
	return (end && *end == 0);
}

static void url_decode_inplace(char* s)
{
	char hex[3] = {0};
	int i, j;

	for (i = 0, j = 0; s[i]; i++, j++)
	{
		if (s[i] == '+' )
		{
			s[j] = ' ';
			continue;
		}
		if (s[i] == '%' && isxdigit((unsigned char)s[i + 1]) && isxdigit((unsigned char)s[i + 2]))
		{
			hex[0] = s[i + 1];
			hex[1] = s[i + 2];
			s[j] = (char)strtol(hex, NULL, 16);
			i += 2;
			continue;
		}
		s[j] = s[i];
	}
	s[j] = 0;
}

static const char* query_get(const char* query, const char* key, char* out, size_t out_sz)
{
	char needle[64];
	const char* p;
	size_t klen;

	if (!query || !key || !out || out_sz < 2)
		return NULL;
	out[0] = 0;
	snprintf(needle, sizeof(needle), "%s=", key);
	klen = strlen(needle);
	p = query;
	while (p && *p)
	{
		if (strncmp(p, needle, klen) == 0)
		{
			p += klen;
			size_t i = 0;
			while (p[i] && p[i] != '&' && i + 1 < out_sz)
			{
				out[i] = p[i];
				i++;
			}
			out[i] = 0;
			url_decode_inplace(out);
			return out;
		}
		p = strchr(p, '&');
		if (p)
			p++;
	}
	return NULL;
}

static int sfo_at(const char* dir)
{
	char p[700];
	size_t n = strlen(dir);
	if (n && dir[n - 1] == '/')
		snprintf(p, sizeof(p), "%ssce_sys/param.sfo", dir);
	else
		snprintf(p, sizeof(p), "%s/sce_sys/param.sfo", dir);
	return file_exists(p) == SUCCESS;
}

/* Walk work dir for …/sce_sys/param.sfo (depth-limited). out_plain gets trailing /. */
static int find_param_sfo(const char* root, char* out_plain, size_t out_sz)
{
	DIR* d;
	struct dirent* de;
	char child[600];

	if (sfo_at(root))
	{
		size_t n = strlen(root);
		if (n && root[n - 1] == '/')
			snprintf(out_plain, out_sz, "%s", root);
		else
			snprintf(out_plain, out_sz, "%s/", root);
		return 1;
	}

	d = opendir(root);
	if (!d)
		return 0;
	while ((de = readdir(d)) != NULL)
	{
		if (de->d_name[0] == '.')
			continue;
		snprintf(child, sizeof(child), "%s/%s", root, de->d_name);
		if (dir_exists(child) != SUCCESS)
			continue;
		if (sfo_at(child))
		{
			snprintf(out_plain, out_sz, "%s/", child);
			closedir(d);
			return 1;
		}
		/* one more level (e.g. work/<mount>/sce_sys) */
		{
			DIR* d2 = opendir(child);
			struct dirent* de2;
			char child2[700];
			if (!d2)
				continue;
			while ((de2 = readdir(d2)) != NULL)
			{
				if (de2->d_name[0] == '.')
					continue;
				snprintf(child2, sizeof(child2), "%s/%s", child, de2->d_name);
				if (dir_exists(child2) != SUCCESS)
					continue;
				if (sfo_at(child2))
				{
					snprintf(out_plain, out_sz, "%s/", child2);
					closedir(d2);
					closedir(d);
					return 1;
				}
			}
			closedir(d2);
		}
	}
	closedir(d);
	return 0;
}

static void send_raw(int fd, const char* data, size_t len)
{
	size_t sent = 0;
	while (sent < len)
	{
		ssize_t n = send(fd, data + sent, len - sent, 0);
		if (n <= 0)
			break;
		sent += (size_t)n;
	}
}

static void send_response(int fd, int code, const char* ctype, const char* body, size_t body_len)
{
	char hdr[512];
	const char* reason = (code == 200) ? "OK" : (code == 400) ? "Bad Request" :
		(code == 404) ? "Not Found" : (code == 501) ? "Not Implemented" : "Error";

	snprintf(hdr, sizeof(hdr),
		"HTTP/1.0 %d %s\r\n"
		"Connection: close\r\n"
		"Access-Control-Allow-Origin: *\r\n"
		"Content-Type: %s\r\n"
		"Content-Length: %zu\r\n"
		"Server: PS4SaveRemote/%s\r\n\r\n",
		code, reason, ctype, body_len, PS4API_VERSION);
	send_raw(fd, hdr, strlen(hdr));
	if (body && body_len)
		send_raw(fd, body, body_len);
}

static void send_json(int fd, int code, const char* json)
{
	send_response(fd, code, "application/json", json, json ? strlen(json) : 0);
}

static void send_file(int fd, const char* path)
{
	FILE* f;
	char* buf;
	long fsize;
	char hdr[512];
	size_t n;

	f = fopen(path, "rb");
	if (!f)
	{
		send_json(fd, 404, "{\"ok\":false,\"error\":\"file not found\"}");
		return;
	}
	fseek(f, 0, SEEK_END);
	fsize = ftell(f);
	fseek(f, 0, SEEK_SET);
	if (fsize < 0)
	{
		fclose(f);
		send_json(fd, 500, "{\"ok\":false,\"error\":\"stat failed\"}");
		return;
	}

	snprintf(hdr, sizeof(hdr),
		"HTTP/1.0 200 OK\r\n"
		"Connection: close\r\n"
		"Access-Control-Allow-Origin: *\r\n"
		"Content-Type: application/octet-stream\r\n"
		"Content-Length: %ld\r\n"
		"Content-Disposition: attachment; filename=\"%s\"\r\n"
		"Server: PS4SaveRemote/%s\r\n\r\n",
		fsize, strrchr(path, '/') ? strrchr(path, '/') + 1 : path, PS4API_VERSION);
	send_raw(fd, hdr, strlen(hdr));

	buf = malloc(64 * 1024);
	if (!buf)
	{
		fclose(f);
		return;
	}
	while ((n = fread(buf, 1, 64 * 1024, f)) > 0)
		send_raw(fd, buf, n);
	free(buf);
	fclose(f);
}

static int ensure_dirs(void)
{
	return (mkdirs(PS4API_INBOX) == SUCCESS &&
		mkdirs(PS4API_OUTBOX) == SUCCESS &&
		mkdirs(PS4API_WORK) == SUCCESS &&
		mkdirs(PS4API_FAKEUSB_SAVE) == SUCCESS);
}

/* --- save jobs ---------------------------------------------------------- */

static int job_decrypt(const char* account, const char* title, const char* dir_name, char* out_zip, size_t out_sz, char* err, size_t err_sz)
{
	save_entry_t save;
	char mount[ORBIS_SAVE_DATA_DIRNAME_DATA_MAXSIZE];
	char base_path[256];
	char volume_path[300];
	char key_path[300];
	char mount_fs[300];
	char zip_base[256];
	char* parent;
	int ok;

	uint64_t account_id;

	snprintf(err, err_sz, "unknown");
	if (!parse_hex_u64(account, &account_id))
	{
		snprintf(err, err_sz, "account must be 16 hex chars");
		return 0;
	}
	(void)account_id;

	if (!title || strlen(title) < 4 || !dir_name || !dir_name[0])
	{
		snprintf(err, err_sz, "title and dir required");
		return 0;
	}

	snprintf(base_path, sizeof(base_path), PS4API_FAKEUSB_SAVE "%s/%s/", account, title);
	snprintf(volume_path, sizeof(volume_path), "%s%s", base_path, dir_name);
	snprintf(key_path, sizeof(key_path), "%s%s.bin", base_path, dir_name);

	if (file_exists(volume_path) != SUCCESS || file_exists(key_path) != SUCCESS)
	{
		snprintf(err, err_sz, "missing volume or .bin under %s", base_path);
		return 0;
	}

	/* Firmware keyset gate (same idea as Apollo _copy_save_pfs) */
	{
		char hdr[16];
		if (read_file(key_path, (uint8_t*)hdr, 0x10) >= 0 && get_max_pfskey_ver() < (uint8_t)hdr[8])
		{
			snprintf(err, err_sz, "save needs higher FW keyset (file=0x%02X console_max=0x%02X)",
				(unsigned)(uint8_t)hdr[8], (unsigned)get_max_pfskey_ver());
			return 0;
		}
	}

	memset(&save, 0, sizeof(save));
	save.path = base_path;
	save.dir_name = (char*)dir_name;
	save.title_id = (char*)title;
	save.flags = SAVE_FLAG_PS4 | SAVE_FLAG_LOCKED;
	save.type = FILE_TYPE_PS4;

	set_status("decrypt: mounting");
	if (!orbis_SaveMount(&save, SAVE_FLAG_LOCKED, mount))
	{
		snprintf(err, err_sz, "mountSave failed (fw keyset / corrupt?)");
		return 0;
	}

	snprintf(mount_fs, sizeof(mount_fs), APOLLO_SANDBOX_PATH, mount);
	snprintf(out_zip, out_sz, PS4API_OUTBOX "decrypt_%s_%s_%s.zip", account, title, dir_name);
	unlink_secure(out_zip);

	/* Match Apollo web export: base=/data/apollo, path=/data/apollo/mount/<dir>/ */
	snprintf(zip_base, sizeof(zip_base), APOLLO_SANDBOX_PATH, "");
	parent = strdup(zip_base);
	if (!parent)
	{
		orbis_SaveUmount(mount);
		snprintf(err, err_sz, "oom");
		return 0;
	}
	if (strrchr(parent, '/'))
		*strrchr(parent, '/') = 0;
	if (strrchr(parent, '/'))
		*strrchr(parent, '/') = 0;

	set_status("decrypt: zipping");
	ok = zip_directory(parent, mount_fs, out_zip);
	free(parent);
	orbis_SaveUmount(mount);

	if (!ok)
	{
		snprintf(err, err_sz, "zip failed");
		return 0;
	}
	set_status("decrypt: done");
	return 1;
}

static int job_resign(const char* account, const char* title, const char* dir_name, char* out_dir, size_t out_sz, char* err, size_t err_sz)
{
	save_entry_t save;
	sfo_context_t* sfo;
	sfo_patch_t patch;
	uint64_t account_id;
	char mount[ORBIS_SAVE_DATA_DIRNAME_DATA_MAXSIZE];
	char plain_root[300];
	char sfo_path[320];
	char mount_fs[300];
	char dst_base[300];
	char src_vol[320], src_key[320], dst_vol[320], dst_key[320];
	char titleid[16], dirname[64];
	uint32_t blocks = 0;
	int created = 0;

	snprintf(err, err_sz, "unknown");
	if (!parse_hex_u64(account, &account_id))
	{
		snprintf(err, err_sz, "account must be 16 hex chars");
		return 0;
	}

	/* Prefer work tree; else extract inbox/plaintext.zip and search for param.sfo */
	{
		char probe[300];
		int found = 0;

		plain_root[0] = 0;
		if (title && title[0] && dir_name && dir_name[0])
		{
			snprintf(probe, sizeof(probe), PS4API_WORK "%s/%s/", title, dir_name);
			found = find_param_sfo(probe, plain_root, sizeof(plain_root));
		}
		if (!found)
			found = find_param_sfo(PS4API_WORK, plain_root, sizeof(plain_root));
		if (!found)
		{
			char inbox_zip[256];
			snprintf(inbox_zip, sizeof(inbox_zip), PS4API_INBOX "plaintext.zip");
			if (file_exists(inbox_zip) == SUCCESS)
			{
				set_status("resign: extracting inbox zip");
				mkdirs(PS4API_WORK);
				if (!extract_zip(inbox_zip, PS4API_WORK))
				{
					snprintf(err, err_sz, "extract plaintext.zip failed");
					return 0;
				}
				found = find_param_sfo(PS4API_WORK, plain_root, sizeof(plain_root));
			}
		}
		if (!found)
		{
			snprintf(err, err_sz, "no plaintext param.sfo (put files in work/ or inbox/plaintext.zip)");
			return 0;
		}
	}

	snprintf(sfo_path, sizeof(sfo_path), "%ssce_sys/param.sfo", plain_root);

	sfo = sfo_alloc();
	if (sfo_read(sfo, sfo_path) < 0)
	{
		sfo_free(sfo);
		snprintf(err, err_sz, "cannot read param.sfo");
		return 0;
	}

	{
		u8* p_title = sfo_get_param_value(sfo, "TITLE_ID");
		u8* p_dir = sfo_get_param_value(sfo, "SAVEDATA_DIRECTORY");
		u8* p_blocks = sfo_get_param_value(sfo, "SAVEDATA_BLOCKS");
		if (!p_title || !p_dir || !p_blocks)
		{
			sfo_free(sfo);
			snprintf(err, err_sz, "param.sfo missing TITLE_ID/DIRECTORY/BLOCKS");
			return 0;
		}
		memset(titleid, 0, sizeof(titleid));
		memset(dirname, 0, sizeof(dirname));
		strncpy(titleid, (char*)p_title, sizeof(titleid) - 1);
		strncpy(dirname, (char*)p_dir, sizeof(dirname) - 1);
		blocks = *((uint32_t*)p_blocks);
	}
	sfo_free(sfo);

	if (title && title[0] && strcmp(title, titleid) != 0)
		LOG("ps4api: title query %s vs sfo %s (using sfo)", title, titleid);
	if (dir_name && dir_name[0] && strcmp(dir_name, dirname) != 0)
		LOG("ps4api: dir query %s vs sfo %s (using sfo)", dir_name, dirname);

	memset(&save, 0, sizeof(save));
	save.title_id = titleid;
	save.dir_name = dirname;
	save.blocks = blocks ? blocks : 96;
	save.flags = SAVE_FLAG_PS4 | SAVE_FLAG_HDD;
	save.type = FILE_TYPE_PS4;
	save.path = "";

	set_status("resign: create/mount");
	if (!orbis_SaveMount(&save,
		ORBIS_SAVE_DATA_MOUNT_MODE_RDWR | ORBIS_SAVE_DATA_MOUNT_MODE_CREATE2 | ORBIS_SAVE_DATA_MOUNT_MODE_COPY_ICON,
		mount))
	{
		/* maybe exists — try rdwr */
		if (!orbis_SaveMount(&save, ORBIS_SAVE_DATA_MOUNT_MODE_RDWR, mount))
		{
			snprintf(err, err_sz, "cannot create/mount HDD save");
			return 0;
		}
	}
	else
		created = 1;

	snprintf(mount_fs, sizeof(mount_fs), APOLLO_SANDBOX_PATH, mount);
	set_status("resign: copy plaintext");
	if (copy_directory(plain_root, plain_root, mount_fs) != SUCCESS)
	{
		orbis_SaveUmount(mount);
		snprintf(err, err_sz, "copy into mount failed");
		return 0;
	}

	/* Jack stays Jack — patch Account-ID from request, keep console user/psid for local mount validity */
	memset(&patch, 0, sizeof(patch));
	patch.user_id = apollo_config.user_id;
	patch.psid = (u8*)apollo_config.psid;
	patch.account_id = account_id;

	snprintf(sfo_path, sizeof(sfo_path), APOLLO_SANDBOX_PATH "sce_sys/param.sfo", mount);
	set_status("resign: patch_sfo");
	if (patch_sfo(sfo_path, &patch) != SUCCESS)
	{
		orbis_SaveUmount(mount);
		snprintf(err, err_sz, "patch_sfo failed");
		return 0;
	}

	orbis_SaveUmount(mount);

	/* Copy sealed volume + key out under their Account-ID tree */
	snprintf(dst_base, sizeof(dst_base), PS4API_FAKEUSB_SAVE "%s/%s/", account, titleid);
	mkdirs(dst_base);

	snprintf(src_vol, sizeof(src_vol), SAVES_PATH_HDD "%s/sdimg_%s", apollo_config.user_id, titleid, dirname);
	snprintf(src_key, sizeof(src_key), SAVES_PATH_HDD "%s/%s.bin", apollo_config.user_id, titleid, dirname);
	snprintf(dst_vol, sizeof(dst_vol), "%s%s", dst_base, dirname);
	snprintf(dst_key, sizeof(dst_key), "%s%s.bin", dst_base, dirname);

	set_status("resign: export sealed");
	if (copy_file(src_vol, dst_vol) != SUCCESS || copy_file(src_key, dst_key) != SUCCESS)
	{
		snprintf(err, err_sz, "copy sealed volume/.bin to fakeusb failed");
		return 0;
	}

	snprintf(out_dir, out_sz, "%s", dst_base);
	set_status(created ? "resign: done (created)" : "resign: done");
	(void)created;
	return 1;
}

static int handle_upload(int fd, const char* name, const char* body, size_t body_len)
{
	char path[300];
	FILE* f;
	char json[256];

	if (!name || !name[0] || strchr(name, '/') || strchr(name, '\\'))
	{
		send_json(fd, 400, "{\"ok\":false,\"error\":\"bad name\"}");
		return 0;
	}
	mkdirs(PS4API_INBOX);
	snprintf(path, sizeof(path), PS4API_INBOX "%s", name);
	f = fopen(path, "wb");
	if (!f)
	{
		send_json(fd, 500, "{\"ok\":false,\"error\":\"cannot write inbox\"}");
		return 0;
	}
	if (body_len && fwrite(body, 1, body_len, f) != body_len)
	{
		fclose(f);
		send_json(fd, 500, "{\"ok\":false,\"error\":\"short write\"}");
		return 0;
	}
	fclose(f);

	/* Convenience: encrypted.zip → extract into fakeusb layout if it contains PS4/SAVEDATA */
	if (strcmp(name, "encrypted.zip") == 0)
	{
		set_status("upload: extracting encrypted.zip");
		extract_zip(path, FAKE_USB_PATH);
	}
	else if (strcmp(name, "plaintext.zip") == 0)
	{
		set_status("upload: plaintext.zip ready");
	}

	snprintf(json, sizeof(json), "{\"ok\":true,\"path\":\"%s\",\"bytes\":%zu}", path, body_len);
	send_json(fd, 200, json);
	return 1;
}

static void handle_request(int fd, char* req, size_t req_len, const char* body, size_t body_len)
{
	char method[16] = {0};
	char target[512] = {0};
	char path[512] = {0};
	char* query = NULL;
	char account[32] = {0}, title[32] = {0}, dir[64] = {0}, file[128] = {0}, name[128] = {0};
	char out[320], err[256], json[1024];

	if (sscanf(req, "%15s %511s", method, target) != 2)
	{
		send_json(fd, 400, "{\"ok\":false,\"error\":\"bad request\"}");
		return;
	}

	strncpy(path, target, sizeof(path) - 1);
	query = strchr(path, '?');
	if (query)
	{
		*query = 0;
		query++;
	}

	if (strcmp(method, "GET") == 0 && strcmp(path, "/api/ping") == 0)
	{
		snprintf(json, sizeof(json),
			"{\"ok\":true,\"service\":\"PS4SaveRemote\",\"version\":\"%s\",\"ip\":\"%s\",\"port\":%d,\"status\":\"%s\"}",
			PS4API_VERSION, g_ip, PS4API_PORT, g_status);
		send_json(fd, 200, json);
		return;
	}

	if (strcmp(method, "GET") == 0 && strcmp(path, "/api/info") == 0)
	{
		snprintf(json, sizeof(json),
			"{\"ok\":true,\"version\":\"%s\",\"user_id\":\"%08x\",\"account_id\":\"%016lx\","
			"\"fakeusb\":\"%s\",\"inbox\":\"%s\",\"outbox\":\"%s\",\"status\":\"%s\"}",
			PS4API_VERSION, apollo_config.user_id, (unsigned long)apollo_config.account_id,
			PS4API_FAKEUSB_SAVE, PS4API_INBOX, PS4API_OUTBOX, g_status);
		send_json(fd, 200, json);
		return;
	}

	if (strcmp(method, "GET") == 0 && strcmp(path, "/api/decrypt") == 0)
	{
		query_get(query, "account", account, sizeof(account));
		query_get(query, "title", title, sizeof(title));
		query_get(query, "dir", dir, sizeof(dir));
		if (!job_decrypt(account, title, dir, out, sizeof(out), err, sizeof(err)))
		{
			snprintf(json, sizeof(json), "{\"ok\":false,\"error\":\"%s\"}", err);
			send_json(fd, 500, json);
			return;
		}
		snprintf(json, sizeof(json),
			"{\"ok\":true,\"zip\":\"%s\",\"download\":\"/api/download?file=%s\"}",
			out, strrchr(out, '/') ? strrchr(out, '/') + 1 : out);
		send_json(fd, 200, json);
		return;
	}

	if (strcmp(method, "GET") == 0 && strcmp(path, "/api/resign") == 0)
	{
		query_get(query, "account", account, sizeof(account));
		query_get(query, "title", title, sizeof(title));
		query_get(query, "dir", dir, sizeof(dir));
		if (!job_resign(account, title, dir, out, sizeof(out), err, sizeof(err)))
		{
			snprintf(json, sizeof(json), "{\"ok\":false,\"error\":\"%s\"}", err);
			send_json(fd, 500, json);
			return;
		}
		snprintf(json, sizeof(json),
			"{\"ok\":true,\"account\":\"%s\",\"out\":\"%s\",\"note\":\"sealed volume+.bin under Account-ID folder\"}",
			account, out);
		send_json(fd, 200, json);
		return;
	}

	if (strcmp(method, "GET") == 0 && strcmp(path, "/api/download") == 0)
	{
		query_get(query, "file", file, sizeof(file));
		if (!file[0] || strchr(file, '/') || strchr(file, '\\'))
		{
			send_json(fd, 400, "{\"ok\":false,\"error\":\"bad file\"}");
			return;
		}
		snprintf(out, sizeof(out), PS4API_OUTBOX "%s", file);
		send_file(fd, out);
		return;
	}

	/* Zip sealed fakeusb tree: /api/sealed?account=&title= */
	if (strcmp(method, "GET") == 0 && strcmp(path, "/api/sealed") == 0)
	{
		char src[320], zipout[320], base[320];
		query_get(query, "account", account, sizeof(account));
		query_get(query, "title", title, sizeof(title));
		if (!account[0] || !title[0])
		{
			send_json(fd, 400, "{\"ok\":false,\"error\":\"account and title required\"}");
			return;
		}
		snprintf(src, sizeof(src), PS4API_FAKEUSB_SAVE "%s/%s", account, title);
		if (dir_exists(src) != SUCCESS)
		{
			send_json(fd, 404, "{\"ok\":false,\"error\":\"sealed folder missing\"}");
			return;
		}
		snprintf(zipout, sizeof(zipout), PS4API_OUTBOX "sealed_%s_%s.zip", account, title);
		unlink_secure(zipout);
		/* basedir=/data/fakeusb so zip contains PS4/SAVEDATA/<account>/<title>/… */
		snprintf(base, sizeof(base), "%s", FAKE_USB_PATH);
		if (base[strlen(base) - 1] == '/')
			base[strlen(base) - 1] = 0;
		if (!zip_directory(base, src, zipout))
		{
			send_json(fd, 500, "{\"ok\":false,\"error\":\"zip sealed failed\"}");
			return;
		}
		send_file(fd, zipout);
		return;
	}

	if ((strcmp(method, "PUT") == 0 || strcmp(method, "POST") == 0) && strcmp(path, "/api/upload") == 0)
	{
		query_get(query, "name", name, sizeof(name));
		if (!name[0])
			snprintf(name, sizeof(name), "upload.bin");
		handle_upload(fd, name, body, body_len);
		return;
	}

	if (strcmp(method, "GET") == 0 && strcmp(path, "/") == 0)
	{
		const char* page =
			"<html><body><h1>PS4 Save Remote</h1>"
			"<p>LAN API for save decrypt/resign. Not a game pirate tool.</p>"
			"<ul><li><a href=\"/api/ping\">/api/ping</a></li>"
			"<li><a href=\"/api/info\">/api/info</a></li></ul></body></html>";
		send_response(fd, 200, "text/html", page, strlen(page));
		return;
	}

	send_json(fd, 404, "{\"ok\":false,\"error\":\"not found\"}");
	(void)req_len;
}

static int read_http(int fd, char** req_out, char** body_out, size_t* body_len_out)
{
	char* buf = NULL;
	size_t cap = 0, len = 0;
	char tmp[8192];
	ssize_t n;
	char* hdr_end;
	size_t hdr_len;
	size_t content_length = 0;
	char* cl;

	*req_out = NULL;
	*body_out = NULL;
	*body_len_out = 0;

	for (;;)
	{
		n = recv(fd, tmp, sizeof(tmp), 0);
		if (n <= 0)
			break;
		if (len + (size_t)n + 1 > cap)
		{
			cap = (cap ? cap * 2 : 16384) + (size_t)n;
			buf = realloc(buf, cap);
			if (!buf)
				return 0;
		}
		memcpy(buf + len, tmp, (size_t)n);
		len += (size_t)n;
		buf[len] = 0;
		hdr_end = strstr(buf, "\r\n\r\n");
		if (hdr_end)
			break;
		if (len > 1024 * 1024)
			break;
	}

	if (!buf || !(hdr_end = strstr(buf, "\r\n\r\n")))
	{
		free(buf);
		return 0;
	}

	hdr_len = (size_t)(hdr_end - buf) + 4;
	cl = find_header_ci(buf, "Content-Length:");
	if (cl)
		content_length = (size_t)strtoul(cl + 15, NULL, 10);

	/* Cap uploads at 512 MiB */
	if (content_length > 512ULL * 1024ULL * 1024ULL)
	{
		free(buf);
		return 0;
	}

	while (len < hdr_len + content_length)
	{
		n = recv(fd, tmp, sizeof(tmp), 0);
		if (n <= 0)
			break;
		if (len + (size_t)n + 1 > cap)
		{
			cap = len + (size_t)n + 1 + 4096;
			buf = realloc(buf, cap);
			if (!buf)
				return 0;
		}
		memcpy(buf + len, tmp, (size_t)n);
		len += (size_t)n;
		buf[len] = 0;
	}

	buf[hdr_len - 4] = 0; /* terminate headers for sscanf */
	*req_out = buf;
	if (content_length)
	{
		*body_out = buf + hdr_len;
		*body_len_out = (len > hdr_len) ? (len - hdr_len) : 0;
		if (*body_len_out > content_length)
			*body_len_out = content_length;
	}
	return 1;
}

static int api_thread(void* unused)
{
	struct sockaddr_in addr, client;
	socklen_t clen;
	int yes = 1;

	(void)unused;
	ensure_dirs();

	g_listen_fd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	if (g_listen_fd < 0)
	{
		set_status("socket failed");
		g_run = 0;
		return -1;
	}
	setsockopt(g_listen_fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));
	memset(&addr, 0, sizeof(addr));
	addr.sin_family = AF_INET;
	addr.sin_port = htons(PS4API_PORT);
	addr.sin_addr.s_addr = htonl(INADDR_ANY);

	if (bind(g_listen_fd, (struct sockaddr*)&addr, sizeof(addr)) < 0)
	{
		set_status("bind failed");
		close(g_listen_fd);
		g_listen_fd = -1;
		g_run = 0;
		return -1;
	}
	if (listen(g_listen_fd, 8) < 0)
	{
		set_status("listen failed");
		close(g_listen_fd);
		g_listen_fd = -1;
		g_run = 0;
		return -1;
	}

	set_status("listening");
	LOG("PS4 Save Remote API on %s:%d", g_ip, PS4API_PORT);

	while (g_run)
	{
		int cfd;
		char *req = NULL, *body = NULL;
		size_t body_len = 0;
		struct timeval tv;
		fd_set fds;
		int sel;

		FD_ZERO(&fds);
		FD_SET(g_listen_fd, &fds);
		tv.tv_sec = 1;
		tv.tv_usec = 0;
		sel = select(g_listen_fd + 1, &fds, NULL, NULL, &tv);
		if (sel <= 0)
			continue;

		clen = sizeof(client);
		cfd = accept(g_listen_fd, (struct sockaddr*)&client, &clen);
		if (cfd < 0)
			continue;

		if (read_http(cfd, &req, &body, &body_len))
			handle_request(cfd, req, strlen(req), body, body_len);
		else
			send_json(cfd, 400, "{\"ok\":false,\"error\":\"bad http\"}");

		free(req);
		shutdown(cfd, SHUT_RDWR);
		close(cfd);
	}

	if (g_listen_fd >= 0)
	{
		close(g_listen_fd);
		g_listen_fd = -1;
	}
	set_status("stopped");
	return 0;
}

int ps4api_start(void)
{
	OrbisNetCtlInfo info;

	if (g_run)
		return 1;

	memset(&info, 0, sizeof(info));
	if (sceNetCtlGetInfo(ORBIS_NET_CTL_INFO_IP_ADDRESS, &info) == 0)
		snprintf(g_ip, sizeof(g_ip), "%s", info.ip_address);
	else
		snprintf(g_ip, sizeof(g_ip), "0.0.0.0");

	ensure_dirs();
	g_run = 1;
	g_thread = SDL_CreateThread(api_thread, "ps4api", NULL);
	if (!g_thread)
	{
		g_run = 0;
		set_status("thread failed");
		return 0;
	}
	return 1;
}

void ps4api_stop(void)
{
	g_run = 0;
	if (g_listen_fd >= 0)
	{
		shutdown(g_listen_fd, SHUT_RDWR);
		close(g_listen_fd);
		g_listen_fd = -1;
	}
	if (g_thread)
	{
		SDL_WaitThread(g_thread, NULL);
		g_thread = NULL;
	}
}

int ps4api_is_running(void)
{
	return g_run;
}

const char* ps4api_ip(void)
{
	return g_ip;
}

int ps4api_port(void)
{
	return PS4API_PORT;
}

const char* ps4api_status(void)
{
	return g_status;
}
