/* repro_om.c - 复现 GetManagedObjects 深层嵌套解析(不随组件发布) */
#include <dbus/dbus.h>
#include <stdio.h>
#include <string.h>

int main(void)
{
	DBusConnection *conn;
	DBusError err;
	DBusMessage *call, *reply;
	DBusMessageIter root, objs, entry, ifaces, ientry, props;
	int depth_probe;

	dbus_error_init(&err);
	conn = dbus_bus_get(DBUS_BUS_SYSTEM, &err);
	if (!conn) {
		printf("connect failed: %s\n", err.message);
		return 1;
	}

	call = dbus_message_new_method_call("org.bluez", "/org/bluez",
					    "org.freedesktop.DBus.ObjectManager",
					    "GetManagedObjects");
	reply = dbus_connection_send_with_reply_and_block(conn, call, 5000, &err);
	dbus_message_unref(call);
	if (!reply) {
		printf("call failed: %s\n", err.message);
		return 1;
	}
	printf("reply sig=%s\n", dbus_message_get_signature(reply));

	dbus_message_iter_init(reply, &root);
	printf("root type=%c\n", dbus_message_iter_get_arg_type(&root));
	dbus_message_iter_recurse(&root, &objs);
	while (dbus_message_iter_get_arg_type(&objs) == DBUS_TYPE_DICT_ENTRY) {
		const char *path = NULL;

		dbus_message_iter_recurse(&objs, &entry);
		dbus_message_iter_get_basic(&entry, &path);
		printf("obj path=%s\n", path);
		dbus_message_iter_next(&entry);
		if (dbus_message_iter_get_arg_type(&entry) == DBUS_TYPE_ARRAY) {
			dbus_message_iter_recurse(&entry, &ifaces);
			while (dbus_message_iter_get_arg_type(&ifaces) == DBUS_TYPE_DICT_ENTRY) {
				const char *iface = NULL;

				dbus_message_iter_recurse(&ifaces, &ientry);
				dbus_message_iter_get_basic(&ientry, &iface);
				dbus_message_iter_next(&ientry);
				printf("  iface=%s valtype=%c\n", iface,
				       dbus_message_iter_get_arg_type(&ientry));
				if (dbus_message_iter_get_arg_type(&ientry) == DBUS_TYPE_ARRAY) {
					dbus_message_iter_recurse(&ientry, &props);
					printf("    props type=%c\n",
					       dbus_message_iter_get_arg_type(&props));
					/* 遍历 props 的 dict entry */
					{
						DBusMessageIter entries;

						dbus_message_iter_recurse(&props, &entries);
						while (dbus_message_iter_get_arg_type(&entries) == DBUS_TYPE_DICT_ENTRY) {
							DBusMessageIter e2;
							const char *key = NULL;

							dbus_message_iter_recurse(&entries, &e2);
							dbus_message_iter_get_basic(&e2, &key);
							dbus_message_iter_next(&e2);
							printf("      key=%s valtype=%c\n", key ? key : "(null)",
							       dbus_message_iter_get_arg_type(&e2));
							if (dbus_message_iter_get_arg_type(&e2) == DBUS_TYPE_VARIANT) {
								DBusMessageIter val;

								dbus_message_iter_recurse(&e2, &val);
								printf("        variant content type=%c\n",
								       dbus_message_iter_get_arg_type(&val));
								if (dbus_message_iter_get_arg_type(&val) == DBUS_TYPE_STRING) {
									const char *s = NULL;

									dbus_message_iter_get_basic(&val, &s);
									printf("        str=%s\n", s ? s : "(null)");
								}
							}
							if (!dbus_message_iter_next(&entries))
								break;
						}
					}
				}
				if (!dbus_message_iter_next(&ifaces))
					break;
			}
		}
		if (!dbus_message_iter_next(&objs))
			break;
	}
	(void)depth_probe;
	printf("done\n");
	return 0;
}
