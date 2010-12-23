// $Id$
// usbreset.cpp - use ioctl to reset USB device
// Also attempt suspend / resume
// Based on code found here: http://marc.info/?l=linux-usb-users&m=116827193506484&w=2
// To suspend:
// echo -n 2 >/sys/bus/usb/devices/.../power/state
// Use 0 to unsuspend.

/* usbreset -- send a USB port reset to a USB device */

#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <dirent.h>
#include <string.h>

#include <linux/usbdevice_fs.h>

// Supported mfgr:prod ids
unsigned int supported_ids[] = {
	0x148f2573,	// ralink
	0x148f2671,	// ralink (2)
	0x07b8b21d,	// Abocom
};
#define NUM_SUPPORTED_IDS (sizeof(supported_ids)/sizeof(supported_ids[0]))

int main(int argc, char **argv)
{
	int fd;
	int rc;
	char deviceName[256] = {0};
	char deviceDir[256];

	// Look for the first supported mfgr:id combo starting with usbdev1.3
	DIR *usbdevs = opendir( "/sys/class/usb_device" );
	if (!usbdevs)
	{
		fprintf( stderr, "%s: could not open /sys/class/usb_device as a dir\n", argv[0] );
		return -1;
	}

	struct dirent *usbdev;
	printf( "checking /sys/class/usb_device: " );
	while (usbdev = readdir( usbdevs ))
	{
		if (!usbdev->d_name)
		{
			continue;
		}
		if (strncmp( usbdev->d_name, "usbdev", 6 ))
		{
			continue;
		}
		// Get bus and slot numbers
		int busNum, slotNum;
		if (sscanf( &usbdev->d_name[6], "%d.%d", &busNum, &slotNum ) != 2)
		{
			printf( "\nFailed to scan bus and slot number from %s\n", &usbdev->d_name[6] );
			continue;
		}
		// Get contents of idVendor and idProduct
		sprintf( deviceDir, "/sys/class/usb_device/%s/device/", usbdev->d_name );
		unsigned int idVendor, idProduct;
		int dirLen = strlen( deviceDir );
		strcpy( &deviceDir[dirLen], "idVendor" );
		FILE *fid = fopen( deviceDir, "r" );
		if (!fid)
		{
			continue;
		}
		idVendor = 0;
		fscanf( fid, "%x", &idVendor );
		fclose( fid );
		if (idVendor == 0)
		{
			continue;
		}
		strcpy( &deviceDir[dirLen], "idProduct" );
		fid = fopen( deviceDir, "r" );
		if (!fid)
		{
			continue;
		}
		idProduct = 0;
		fscanf( fid, "%x", &idProduct );
		fclose( fid );
		if (idProduct == 0)
		{
			continue;
		}
		printf( "%04x:%04x - ", idVendor, idProduct );
		// Check for match
		unsigned int u = (idVendor * 0x10000) + idProduct;
		int n;
		bool found = false;
		for (n = 0; n < NUM_SUPPORTED_IDS && !found; n++)
		{
			found = (u == supported_ids[n]);
		}
		if (!found)
		{
			printf( "no match; " );
			continue;
		}
		printf( "MATCHED" );
		sprintf( deviceName, "/proc/bus/usb/%03d/%03d", busNum, slotNum );
		break;
	}

	closedir( usbdevs );

	if (!deviceName[0])
	{
		fprintf( stderr, "%s: unable to find a match\n", argv[0] );
		return -1;
	}

	fd = open(deviceName, O_WRONLY);
	if (fd < 0) {
		fprintf( stderr, "Error opening %s: %s (errno=%d)", deviceName, strerror(errno), errno );
		return 1;
	}

	printf( "\nResetting USB device %s: ", deviceName );
	rc = ioctl(fd, USBDEVFS_RESET, 0);
	if (rc < 0) {
		perror("Error in ioctl");
		return 1;
	}
	printf("reset successful\n");

	close(fd);
	return 0;
}


