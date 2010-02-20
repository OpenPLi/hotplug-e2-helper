#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <sys/un.h>
#include <netdb.h>
#include <fcntl.h>
#include <string.h>

int main(int argc, char *argv[])
{
	int sd = -1;
	struct sockaddr_un serv_addr_un;
	memset(&serv_addr_un, 0, sizeof(serv_addr_un));
	serv_addr_un.sun_family = AF_LOCAL;
	strcpy(serv_addr_un.sun_path, "/tmp/hotplug.socket");
	sd = socket(AF_LOCAL, SOCK_STREAM, 0);
	if (sd >= 0)
	{
		if (connect(sd, (const struct sockaddr*)&serv_addr_un, sizeof(serv_addr_un)) >= 0)
		{
			char *val = NULL;
			char data[1024];
			val = getenv("ACTION");
			if (val)
			{
				snprintf(data, sizeof(data) - 1, "ACTION=%s", val);
				data[sizeof(data) - 1] = 0;
				send(sd, data, strlen(data) + 1, 0);
				val = getenv("MDEV");
				if (!val) val = "-";
				snprintf(data, sizeof(data) - 1, "DEVPATH=%s", val);
				data[sizeof(data) - 1] = 0;
				send(sd, data, strlen(data) + 1, 0);
				val = getenv("PHYSDEVPATH");
				if (!val) val = "-";
				snprintf(data, sizeof(data) - 1, "PHYSDEVPATH=%s", val);
				data[sizeof(data) - 1] = 0;
				send(sd, data, strlen(data) + 1, 0);
			}
		}
		close(sd);
	}
}
