/*
 * bluectl_media.c - org.bluez.MediaPlayer1 媒体控制(AVRCP)
 */
#include "bluectl_internal.h"

#include <stdio.h>
#include <string.h>

struct bctl_find_player {
	const char *prefix;	/* 限定设备对象路径前缀, NULL = 任意 */
	char path[BLUECTL_PATH_LEN];
	bluectl_media_player_t player;
	int found;
};

static void player_fill(DBusMessageIter *props, bluectl_media_player_t *out)
{
	DBusMessageIter it;

	memset(out, 0, sizeof(*out));
	if (!props)
		return;
	if (bctl_dict_lookup(props, "Status", &it))
		bctl_read_str(&it, out->status, sizeof(out->status));
	if (bctl_dict_lookup(props, "Position", &it))
		out->position = (unsigned int)bctl_read_num(&it, 0);
	if (bctl_dict_lookup(props, "Track", &it) &&
	    dbus_message_iter_get_arg_type(&it) == DBUS_TYPE_ARRAY) {
		/*
		 * Track 是变体内容 a{sv}, it 即数组迭代器,
		 * 直接用它查键(recurse 后会变成首个 dict entry,
		 * dict_lookup 只认数组迭代器)。
		 */
		DBusMessageIter field;

		if (bctl_dict_lookup(&it, "Title", &field))
			bctl_read_str(&field, out->title, sizeof(out->title));
		if (bctl_dict_lookup(&it, "Artist", &field))
			bctl_read_str(&field, out->artist, sizeof(out->artist));
		if (bctl_dict_lookup(&it, "Album", &field))
			bctl_read_str(&field, out->album, sizeof(out->album));
		if (bctl_dict_lookup(&it, "Genre", &field))
			bctl_read_str(&field, out->genre, sizeof(out->genre));
		if (bctl_dict_lookup(&it, "Duration", &field))
			out->duration = (unsigned int)bctl_read_num(&field, 0);
	}
}

static void find_player_cb(const char *path, const char *iface,
			   DBusMessageIter *props, void *user)
{
	struct bctl_find_player *f = user;

	if (f->found || strcmp(iface, IFACE_MEDIA_PLAYER1))
		return;
	if (f->prefix) {
		size_t prefix_len = strlen(f->prefix);
		size_t path_len = strlen(path);

		if (path_len < prefix_len || strncmp(path, f->prefix, prefix_len) ||
		    (path[prefix_len] != '/' && path[prefix_len] != '\0'))
			return;
	}
	bctl_strscpy(f->path, path, sizeof(f->path));
	player_fill(props, &f->player);
	bctl_mac_from_path(path, f->player.device, sizeof(f->player.device));
	f->found = 1;
}

static int resolve_player(struct bluectl_ctx *c, const char *device,
			  char *out, size_t len)
{
	struct bctl_find_player f;
	char dev_path[BLUECTL_PATH_LEN];
	const char *prefix = NULL;
	int rv;

	if (!device || !device[0]) {
		bctl_set_err(c, BLUECTL_ERR_INVALID_ARG, "device is required");
		return BLUECTL_ERR_INVALID_ARG;
	}
	if (!out || !len) {
		bctl_set_err(c, BLUECTL_ERR_INVALID_ARG, "invalid output buffer");
		return BLUECTL_ERR_INVALID_ARG;
	}
	if (device[0] == '/' && strlen(device) >= len) {
		bctl_set_err(c, BLUECTL_ERR_INVALID_ARG, "player path is too long");
		return BLUECTL_ERR_INVALID_ARG;
	}
	if (device[0] != '/') {
		/* MAC 地址: 先解析设备对象路径, 再限定其下查找 player */
		rv = bctl_device_path(c, device, dev_path, sizeof(dev_path));
		if (rv < 0)
			return rv;
		prefix = dev_path;
	}
	/* device 直接是 player 路径时也走枚举(统一校验 MediaPlayer1 存在) */
	memset(&f, 0, sizeof(f));
	f.prefix = prefix;
	if (device[0] == '/')
		f.prefix = device;
	rv = bctl_foreach_object(c, find_player_cb, &f);
	if (rv < 0)
		return rv;
	if (!f.found) {
		bctl_set_err(c, BLUECTL_ERR_NOT_FOUND,
			     "media player for '%s' not found", device);
		return BLUECTL_ERR_NOT_FOUND;
	}
	bctl_strscpy(out, f.path, len);
	return BLUECTL_OK;
}

int bluectl_media_player_get(const char *device, bluectl_media_player_t *out)
{
	struct bluectl_ctx *c = &g_bluectl;
	char path[BLUECTL_PATH_LEN];
	DBusMessage *reply;
	DBusMessageIter root;
	int rv;

	if (!out) {
		bctl_set_err(c, BLUECTL_ERR_INVALID_ARG, "output buffer is required");
		return BLUECTL_ERR_INVALID_ARG;
	}
	rv = resolve_player(c, device, path, sizeof(path));
	if (rv < 0)
		return rv;
	reply = bctl_prop_get_all(c, path, IFACE_MEDIA_PLAYER1);
	if (!reply)
		return c->err_code;
	if (!dbus_message_iter_init(reply, &root)) {
		dbus_message_unref(reply);
		bctl_set_err(c, BLUECTL_ERR, "GetAll: unexpected reply");
		return BLUECTL_ERR;
	}
	player_fill(&root, out);
	bctl_strscpy(out->path, path, sizeof(out->path));
	bctl_mac_from_path(path, out->device, sizeof(out->device));
	dbus_message_unref(reply);
	return BLUECTL_OK;
}

int bluectl_media_cmd(const char *device, const char *cmd)
{
	struct bluectl_ctx *c = &g_bluectl;
	static const struct {
		const char *cmd;
		const char *method;
	} table[] = {
		{ "play",        "Play" },
		{ "pause",       "Pause" },
		{ "stop",        "Stop" },
		{ "next",        "Next" },
		{ "previous",    "Previous" },
		{ "fastforward", "FastForward" },
		{ "rewind",      "Rewind" },
	};
	char path[BLUECTL_PATH_LEN];
	DBusMessage *reply;
	size_t i;

	if (!cmd || !cmd[0]) {
		bctl_set_err(c, BLUECTL_ERR_INVALID_ARG, "cmd is required");
		return BLUECTL_ERR_INVALID_ARG;
	}
	for (i = 0; i < sizeof(table) / sizeof(table[0]); i++)
		if (!strcmp(table[i].cmd, cmd))
			break;
	if (i >= sizeof(table) / sizeof(table[0])) {
		bctl_set_err(c, BLUECTL_ERR_INVALID_ARG,
			     "unknown media cmd '%s'", cmd);
		return BLUECTL_ERR_INVALID_ARG;
	}
	if (resolve_player(c, device, path, sizeof(path)) < 0)
		return c->err_code;
	reply = bctl_call(c, path, IFACE_MEDIA_PLAYER1, table[i].method, -1);
	if (!reply)
		return c->err_code;
	dbus_message_unref(reply);
	return BLUECTL_OK;
}
