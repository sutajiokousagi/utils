// $Id$
// esd_config_area.h - definition of eSD config area in partition 0
// Copyright (C) 2009 Chumby Industries. All rights reserved

#ifndef _ESD_CONFIG_AREA_H_INCLUDED_
#define _ESD_CONFIG_AREA_H_INCLUDED_

// Current config area version in array format
#define ESD_CONFIG_AREA_VER	1,0,0,1

// Fixed offset within partition 1 for start of config area
#define ESD_CONFIG_AREA_PART1_OFFSET	0xc000

// Length of config area
#define ESD_CONFIG_AREA_LENGTH			0x4000

// First unused area in config - used as a temporary storage in RAM for boot-time debugging
#define ESD_CONFIG_UNUSED	unused2

// pragma pack() not supported on all platforms, so we make everything dword-aligned using arrays
// WARNING: we're being lazy here and assuming that the platform any utility using this
// is running on little-endian! Otherwise you'll need to convert u32 values
typedef union {
	char name[4];
	unsigned int uname;
} block_def_name;

typedef struct _block_def {
	unsigned int offset;		// Offset from start of partition 1; if 0xffffffff, end of block table
	unsigned int length;		// Length of block in bytes
	unsigned char block_ver[4];	// Version of this block data, e.g. 1,0,0,0
	block_def_name n;		// Name of block, e.g. "krnA" (not NULL-terminated, a-z, A-Z, 0-9 and non-escape symbols allowed)
} block_def;

typedef struct _config_area {
	char sig[4];	// 'C','f','g','*'
	unsigned char area_version[4];	// 1,0,0,0
	unsigned char active_index[4];	// element 0 is 0 if krnA active, 1 if krnB; elements 1-3 are padding
	unsigned char updating[4];	// element 0 is 1 if update in progress; elements 1-3 are padding
	char last_update[16];		// NULL-terminated version of last successful update, e.g. "1.7.1892"
	unsigned int p1_offset;		// Offset in bytes from start of device to start of partition 1
	char factory_data[220];		// Data recorded in manufacturing in format KEY=VALUE<newline>...
	char configname[128];		// NULL-terminated CONFIGNAME of current build, e.g. "silvermoon_sd"
	unsigned char unused2[128];
	unsigned char mbr_backup[512];	// Backup copy of MBR
	block_def block_table[64];	// Block table entries ending with offset==0xffffffff
	unsigned char unused3[0];
} config_area;

#endif

