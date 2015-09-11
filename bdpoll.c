/*
    bdpoll.c (based on addon-storage.c from hal-0.5.10)

    Poll storage devices for media changes

    Copyright (C) 2007 Andreas Oberritter
    Copyright (C) 2004 David Zeuthen, <david@fubar.dk>

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License 2.0 as published by
    the Free Software Foundation.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License along
    with this program; if not, write to the Free Software Foundation, Inc.,
    51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
*/

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <mntent.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mount.h>
#include <unistd.h>
#include <linux/cdrom.h>
#include <sys/stat.h>
#include <sys/types.h>

enum {
	MEDIA_STATUS_UNKNOWN = 0,
	MEDIA_STATUS_GOT_MEDIA = 1,
	MEDIA_STATUS_NO_MEDIA = 2,
};

static int media_status = MEDIA_STATUS_NO_MEDIA;
static const int interval_in_seconds = 1;
static int media_type = 0;
static char volume_name[33];
bool media_mounted = false;

static char *trimwhitespace(char *str)
{
	char *end;

	while(isspace(*str)) str++;

	if(*str == 0)
		return str;

	end = str + strlen(str) - 1;
	while(end > str && isspace(*end)) end--;
	*(end+1) = 0;

	return str;
}

static int media_read_data ( const char device_file[], int seek, int len, char * data)
{
	unsigned int fd;
	int ret = -1;
	if ( device_file != NULL) {
		if (( fd = open (device_file, O_RDONLY)) != -1) {
			if (lseek (fd, seek, SEEK_SET) != -1) {
				if ( read ( fd, data, len) != -1) {
					data[len] = '\0';
					ret = 0;
				}
			}
		}
		close ( fd);
	}
	return ret;
}

static void bdpoll_notify(const char devname[])
{
	char buf[1024];
	struct stat cdrom_link;
	snprintf(buf, sizeof(buf), "/dev/%s", devname);
	// create symlink cdrom to the device needed for audio cd's  gst-1.0
	if (lstat("/dev/cdrom", &cdrom_link) != 0) {
		symlink(buf, "/dev/cdrom");
	}
	if (media_status == MEDIA_STATUS_GOT_MEDIA) {
		if (media_type == CDS_AUDIO) {
			snprintf(volume_name,sizeof(volume_name), "%s", devname);
			snprintf(buf, sizeof(buf), "/media/%s", volume_name);
			// To Do: adapting the mount with cdfs to work whitout the deprecated cdfs file system.
			mkdir(buf, 0777);
			snprintf(buf, sizeof(buf), "/bin/mount -t cdfs /dev/%s /media/%s", devname, volume_name);
			if (system(buf) == 0) {
				setenv("X_E2_MEDIA_STATUS", (media_status == MEDIA_STATUS_GOT_MEDIA) ? "1" : "0", 1);
				snprintf(buf, sizeof(buf), "/usr/bin/hotplug_e2_helper add /block/%s /block/%s/device 1", devname, devname);
				system(buf);
				media_mounted = true;
			}
			else
				printf("Unable to mount disc\n");
		}
		// CD/DVD will be mounted to his volume_label if avbl else to devicename
		else if (media_type >= CDS_DATA_1) {
			int seek =32808;
			int len = 32;
			char * out_buff = NULL;
			out_buff = malloc( (sizeof (out_buff)) * (len+1));
			snprintf(buf, sizeof(buf), "/dev/%s", devname);
			if ( media_read_data ( buf, seek, len, out_buff) != -1) {
				if (!strncmp(out_buff, "NO NAME", 7) || !strncmp (out_buff, " ",1)) {
					snprintf(volume_name, sizeof(volume_name), "UNTITLED-DISC");
				}
				else {
					// remove white spaces.
					char *trimmed_buff = NULL;
					trimmed_buff = trimwhitespace(out_buff);
					snprintf(volume_name, sizeof(volume_name), "%s", trimmed_buff);
				}
			}
			else {
				snprintf(volume_name, sizeof(volume_name), "%s", devname);
			}
			free(out_buff);
			out_buff = NULL;
			snprintf(buf, sizeof(buf),"/media/%s", volume_name);
			mkdir(buf, 0777);
			snprintf(buf, sizeof(buf), "/bin/mount -t udf /dev/%s /media/%s", devname, volume_name);
			printf("Mounting device /dev/%s to /media/%s", devname, volume_name);
			if (system(buf) == 0) {
				setenv("X_E2_MEDIA_STATUS", (media_status == MEDIA_STATUS_GOT_MEDIA) ? "1" : "0", 1);
				snprintf(buf, sizeof(buf), "/usr/bin/hotplug_e2_helper add /block/%s /block/%s/device 1", devname, devname);
				system(buf);
				media_mounted = true;
			}
			else {
				// udf fails, try iso9660
				snprintf(buf, sizeof(buf), "/bin/mount -t iso9660 /dev/%s /media/%s", devname, volume_name);
				if(system(buf) == 0) {
					setenv("X_E2_MEDIA_STATUS", (media_status == MEDIA_STATUS_GOT_MEDIA) ? "1" : "0", 1);
					snprintf(buf, sizeof(buf), "/usr/bin/hotplug_e2_helper add /block/%s /block/%s/device 1", devname, devname);
					system(buf);
					media_mounted = true;
				}
				else {
					// iso9660 fails try auto does it make sense ?
					snprintf(buf, sizeof(buf), "/bin/mount /dev/%s /media/%s", devname, volume_name);
					if(system(buf) == 0) {
						setenv("X_E2_MEDIA_STATUS", (media_status == MEDIA_STATUS_GOT_MEDIA) ? "1" : "0", 1);
						snprintf(buf, sizeof(buf), "/usr/bin/hotplug_e2_helper add /block/%s /block/%s/device 1", devname, devname);
						system(buf);
						media_mounted = true;
					}
					else
						printf("Unable to mount disc\n");
				}
			}
		}
		else {
			// unsuported media
			printf("Unable to mount disc\n");
		}
	}
	else {
		// unmounting cd/dvd upon removal. Clear mointpoint.
		if (media_mounted) {
			snprintf(buf, sizeof(buf), "/bin/umount /dev/%s -l", devname);
			system(buf);
			snprintf(buf, sizeof(buf), "/media/%s", volume_name);
			unlink(buf);
			rmdir(buf);
			// Clear volume_name.
			memset(&volume_name[0], 0, sizeof(volume_name));
			setenv("X_E2_MEDIA_STATUS", "0", 1);
			// Removing device after cd/dvd is removed.
			snprintf(buf, sizeof(buf), "/usr/bin/hotplug_e2_helper remove /block/%s /block/%s/device 1", devname, devname);
			system(buf);
			media_mounted = false;
		}
		else {
			setenv("X_E2_MEDIA_STATUS", "0", 1);
			setenv("DEVPATH", NULL, 1);
			setenv("PHYSDEVPATH", NULL, 1);
			setenv("ACTION", NULL, 1);
		}
	}
}

static bool is_mounted(const char device_file[])
{
	FILE *f;
	bool rc;
	struct mntent mnt;
	struct mntent *mnte;
	char buf[512];

	rc = false;

	if ((f = setmntent("/etc/mtab", "r")) == NULL)
		return rc;

	while ((mnte = getmntent_r(f, &mnt, buf, sizeof(buf))) != NULL) {
		if (strcmp(device_file, mnt.mnt_fsname) == 0) {
			rc = true;
			break;
		}
	}

	endmntent(f);
	return rc;
}

static bool poll_for_media(const char device_file[], bool is_cdrom, bool support_media_changed)
{
	int fd;
	bool got_media = false;
	bool ret = false;

	if (is_cdrom) {
		int drive;

		fd = open(device_file, O_RDONLY | O_NONBLOCK | O_EXCL);
		if (fd < 0 && errno == EBUSY) {
			/* this means the disc is mounted or some other app,
			 * like a cd burner, has already opened O_EXCL */

			/* HOWEVER, when starting hald, a disc may be
			 * mounted; so check /etc/mtab to see if it
			 * actually is mounted. If it is we retry to open
			 * without O_EXCL
			 */
			if (!is_mounted(device_file))
				return false;
			fd = open(device_file, O_RDONLY | O_NONBLOCK);
		}
		if (fd < 0) {
			err("%s: %s", device_file, strerror(errno));
			return false;
		}

		/* Check if a disc is in the drive
		 *
		 * @todo Use MMC-2 API if applicable
		 */
		drive = ioctl(fd, CDROM_DRIVE_STATUS, CDSL_CURRENT);
		switch (drive) {
		case CDS_NO_INFO:
			media_type = 0;
		case CDS_NO_DISC:
			media_type = 0;
		case CDS_TRAY_OPEN:
			media_type = 0;
		case CDS_DRIVE_NOT_READY:
			media_type = 0;
			break;

		case CDS_DISC_OK:
			/* some CD-ROMs report CDS_DISK_OK even with an open
			 * tray; if media check has the same value two times in
			 * a row then this seems to be the case and we must not
			 * report that there is a media in it. */
			if (support_media_changed) {
				if (ioctl(fd, CDROM_MEDIA_CHANGED, CDSL_CURRENT) &&
					ioctl(fd, CDROM_MEDIA_CHANGED, CDSL_CURRENT)) {
					printf("Tray open");
					media_type = 0;
					ioctl(fd, CDROM_LOCKDOOR, 0);
				}
			} else {
				got_media = true;
				media_type = ioctl(fd, CDROM_DISC_STATUS , CDSL_CURRENT);
				/*
				 * this is a bit of a hack; because we mount the cdrom, the eject button
				 * would not work, so we would never detect 'medium removed', and
				 * never umount the cdrom.
				 * So we unlock the door
				 */
				ioctl(fd, CDROM_LOCKDOOR, 0);
			}
			break;

		case -1:
			err("%s: CDROM_DRIVE_STATUS: %s", device_file, strerror(errno));
			media_type = 0;
			break;
		}

		close(fd);
	} else {
		fd = open(device_file, O_RDONLY);
		if ((fd < 0) && (errno == ENOMEDIUM)) {
			got_media = false;
		} else if (fd >= 0) {
			got_media = true;
		} else {
			err("%s: %s", device_file, strerror(errno));
			return false;
		}
	}

	switch (media_status) {
	case MEDIA_STATUS_GOT_MEDIA:
		if (!got_media) {
			printf("Media removal detected on %s\n", device_file);
			ret = true;
			media_type = 0;
			/* have to this to trigger appropriate hotplug events */
			fd = open(device_file, O_RDONLY | O_NONBLOCK);
			if (fd >= 0) {
				ioctl(fd, BLKRRPART);
				close(fd);
			}
		}
		break;

	case MEDIA_STATUS_NO_MEDIA:
		if (got_media) {
			printf("Media insertion detected on %s\n", device_file);
			ret = true;
		}
		break;
	}

	/* update our current status */
	if (got_media)
		media_status = MEDIA_STATUS_GOT_MEDIA;
	else {
		media_status = MEDIA_STATUS_NO_MEDIA;
		media_type = 0;
	}

	return ret;
}

static void usage(const char argv0[])
{
	fprintf(stderr, "usage: %s <devname> [-c][-m]\n", argv0);
}

int bdpoll(int argc, char *argv[], char *envp[])
{
	const char *devname;
	char devnode[1024];
	bool is_cdrom = false;
	bool support_media_changed = false;
	int opt;

	while ((opt = getopt(argc, argv, "cm")) != -1) {
		switch (opt) {
		case 'c':
			is_cdrom = true;
			break;
		case 'm':
			support_media_changed = true;
			break;
		default:
			usage(argv[0]);
			return EXIT_FAILURE;
		}
	}

	if (optind > argc) {
		usage(argv[0]);
		return EXIT_FAILURE;
	}

	devname = argv[optind];
	snprintf(devnode, sizeof(devnode), "/dev/%s", devname);

	daemon(0, 0);

	for (;;) {
		if (poll_for_media(devnode, is_cdrom, support_media_changed))
			bdpoll_notify(devname);
		sleep(interval_in_seconds);
	}

	return EXIT_SUCCESS;
}

int main(int argc, char *argv[], char *envp[])
{
	return bdpoll(argc, argv, envp);
}
