// $Id$
// linuxrc.cpp - tiny /linuxrc surrogate
// Copyright (C) 2008-9 Chumby Industries, Inc.
// henry@chumby.com

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <signal.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <fcntl.h>
#include <dirent.h>
#include <alloca.h>

#define VER_STR "0.42 $Rev$"
#define CONFIG_PATH "/etc/init.d/linuxrc.config"

int g_childExited = 0;
int g_verbose = 0;
int g_filesCopied = 0;
int g_symlinksCreated = 0;
int g_dirsCreated = 0;
extern char **environ;

// Class which takes a non-empty non-comment line from /etc/init.d/linuxrc.config
// and parses it
class ScriptAction;
class ScriptAction
{
public:
	ScriptAction( const char *src )
	{
		// Skip leading space
		src += strspn( src, " \t" );
		m_cmdLen = strcspn( src, " \t#\r\n" );
		m_cmd = (char *)malloc( m_cmdLen + 1 );
		strncpy( m_cmd, src, m_cmdLen );
		m_cmd[m_cmdLen] = '\0';
		m_buff = strdup( src );
		m_next = NULL;
	};
	~ScriptAction()
	{
		if (m_next)
		{
			ScriptAction *next = m_next;
			m_next = NULL;
			delete next;
		}
		if (m_buff)
		{
			free( m_buff );
			m_buff = NULL;
		}
		if (m_cmd)
		{
			free( m_cmd );
			m_cmd = NULL;
		}
	};
	// true if this is a setenv cmd
	bool IsSetenv()
	{
		return (strcmp( m_cmd, "setenv" ) == 0);
	};
	// Add to end of chain and return new end
	ScriptAction * Add( ScriptAction *next )
	{
		if (m_next != NULL)
		{
			return m_next->Add( next );
		}
		m_next = next;
		return (next != NULL) ? next : this;
	};
	// Execute scripted action. Return is negative if error
	int Execute();
	ScriptAction *Next()
	{
		return m_next;
	};
	// Get everything after cmd, skipping space
	const char *PostCmd() const
	{
		const char *s = &m_buff[m_cmdLen];
		return s + strspn( s, " \t" );
	};
protected:
	char *m_buff;
	char *m_cmd;
	int m_cmdLen;
	ScriptAction *m_next;
};

void sighandler( int signum )
{
	switch (signum)
	{
		case SIGCHLD:
			g_childExited = 1;
			break;
		default:
			printf( "unhandled signal %d\n", signum );
			break;
	}
}

// Copy a file, along with permissions and ownership. Returns bytes copied or -1 if error
int copy_file( const char *srcpath, const char *destpath )
{
	int src = open( srcpath, O_RDONLY );
	if (src < 0)
	{
		return -1;
	}
	// Get permissions
	struct stat fs;
	if (fstat( src, &fs ) < 0)
	{
		int save_errno = errno;
		close( src );
		errno = save_errno;
		return -1;
	}
	// Open output
	int dest = creat( destpath, fs.st_mode );
	if (dest < 0)
	{
		int save_errno = errno;
		close( src );
		errno = save_errno;
		return -1;
	}
	char buff[16384];
	ssize_t bytesRead;
	int totalCopied = 0;
	while (bytesRead = read( src, buff, sizeof(buff) ))
	{
		if (bytesRead < 0)
		{
			int save_errno = errno;
			close( src );
			close( dest );
			errno = save_errno;
			return -1;
		}
		if (write( dest, buff, bytesRead ) != bytesRead)
		{
			int save_errno = errno;
			close( src );
			close( dest );
			errno = save_errno;
			return -1;
		}
		totalCopied += bytesRead;
	}
	close( src );
	// Set ownership - ignore error
	if (fchown( dest, fs.st_uid, fs.st_gid ) < 0)
	{
		printf( "Warning: could not set owner for %s, errno=%d (%s)\n", destpath, errno, strerror(errno) );
	}
	close( dest );
	g_filesCopied++;
	return totalCopied;
}

// Sync source to destination dir, preserving symlinks
void sync_dir( const char *srcdir, const char *destdir )
{
	DIR *src = opendir( srcdir );
	if (src == NULL)
	{
		printf( "Failed to open %s (errno=%d): %s\n", srcdir, errno, strerror(errno) );
		return;
	}
	struct dirent *e;
	char *srcpath = (char *)alloca( strlen( srcdir ) + 1 + 1024 );
	char *destpath = (char *)alloca( strlen( destdir ) + 1 + 1024 );
	sprintf( srcpath, "%s/", srcdir );
	sprintf( destpath, "%s/", destdir );
	int srcpath_base = strlen( srcpath );
	int destpath_base = strlen( destpath );
	// Make sure dest dir exists
	if (access( destdir, 0 ) != 0)
	{
		struct stat fs;
		stat( srcdir, &fs );
		if (g_verbose) printf( "[MKDIR] %s 0%o\n", destdir, fs.st_mode );
		// FIXME if no owner write, we'll need to set that later, otherwise copy will fail
		if (mkdir( destdir, fs.st_mode ) < 0)
		{
			printf( "error: mkdir(%s,0%o) failed, errno=%d (%s)\n", destdir, fs.st_mode, errno, strerror(errno) );
		}
		else
		{
			g_dirsCreated++;
		}
	}
	while (e = readdir( src ))
	{
		if (strlen( e->d_name ) > 1023)
		{
			printf( "Name too long: %s\n", e->d_name );
			break;
		}
		strcpy( &srcpath[srcpath_base], e->d_name );
		strcpy( &destpath[destpath_base], e->d_name );
		switch (e->d_type)
		{
			case DT_DIR:
				if (e->d_name[0] != '.' ||
					(strcmp( e->d_name, "." ) != 0 && strcmp( e->d_name, ".." ) != 0))
				{
					// Recurse
					sync_dir( srcpath, destpath );
				}
				break;
			case DT_LNK:
				if (access( destpath, 0 ) != 0)
				{
					if (g_verbose) printf( "[SYMLINK] %s -> %s\n", srcpath, destpath );
					char link_buf[1024];
					ssize_t link_len = readlink( srcpath, link_buf, sizeof(link_buf) );
					if (link_len <= 0)
					{
						printf( "error on %s: could not get link (errno=%d): %s\n", srcpath, errno, strerror(errno) );
					}
					else
					{
						link_buf[link_len] = '\0';
						if (symlink( link_buf, destpath ) == -1)
						{
							printf( "error: create link %s failed (errno=%d): %s\n", destpath, errno, strerror(errno) );
						}
						else
						{
							g_symlinksCreated++;
						}
					}
				}
				break;
			case DT_REG:
				if (access( destpath, 0 ) != 0)
				{
					if (g_verbose) printf( "[FILE] %s -> %s\n", srcpath, destpath );
					// Copy file contents
					// Set destination mode and ownership
					if (copy_file( srcpath, destpath ) < 0)
					{
						printf( "error: copy(%s,%s) failed (errno=%d): %s\n", srcpath, destpath, errno, strerror(errno) );
					}
				}
				break;
			default:
				printf( "Unhandled type %d for %s\n", e->d_type, srcpath );
				break;
		}
	}
	closedir( src );
}

// Cat a file
void cat( const char *src )
{
	printf( "[CAT] %s\n", src );
	FILE *f = fopen( src, "r" );
	if (!f)
	{
		printf( "Errno=%d (%s)\n", errno, strerror(errno) );
		return;
	}
	char buff[1024];
	while( fgets( buff, sizeof(buff), f ))
	{
		printf( "%s", buff );
	}
	fclose( f );
}

// Recursively list a single dir
void lsdir( const char *dir, bool recursive )
{
	DIR *src = opendir( dir );
	if (src == NULL)
	{
		printf( "Failed to open %s (errno=%d): %s\n", dir, errno, strerror(errno) );
		return;
	}
	struct dirent *e;
	char *srcpath = (char *)alloca( strlen( dir ) + 1 + 1024 );
	sprintf( srcpath, "%s/", dir );
	int srcpath_base = strlen( srcpath );
	while (e = readdir( src ))
	{
		if (strlen( e->d_name ) > 1023)
		{
			printf( "Name too long: %s\n", e->d_name );
			break;
		}
		strcpy( &srcpath[srcpath_base], e->d_name );
		struct stat fs;
		switch (e->d_type)
		{
			case DT_DIR:
				stat( srcpath, &fs );
				printf( "d%6o %s\n", fs.st_mode, srcpath );
				if ((e->d_name[0] != '.' ||
					(strcmp( e->d_name, "." ) != 0 && strcmp( e->d_name, ".." ) != 0)) && recursive)
				{
					// Recurse
					lsdir( srcpath, recursive );
				}
				break;
			case DT_LNK:
				{
					char link_buf[1024];
					ssize_t link_len = readlink( srcpath, link_buf, sizeof(link_buf) );
					if (link_len <= 0)
					{
						printf( "error on %s: could not get link (errno=%d): %s\n", srcpath, errno, strerror(errno) );
					}
					else
					{
						link_buf[link_len] = '\0';
						printf( "         %s -> %s\n", srcpath, link_buf );
					}
				}
				break;
			case DT_REG:
				stat( srcpath, &fs );
				printf( "-%6o %s %ld\n", fs.st_mode, srcpath, fs.st_size );
				break;
			case DT_BLK:
			case DT_CHR:
				stat( srcpath, &fs );
				printf( "%c%6o %s %d, %d 0x%04x\n", e->d_type == DT_BLK ? 'b' : 'c', fs.st_mode,
					srcpath,
					(int)((fs.st_rdev & 0xff00) >> 8), (int)(fs.st_rdev & 0xff),
					(unsigned int)fs.st_rdev );
				break;
			default:
				printf( "Unhandled type %d for %s\n", e->d_type, srcpath );
				break;
		}
	}
	closedir( src );
}

// Perform equivalent of ls -l
void ls( const char *dir, bool recursive = false )
{
	printf( "[LS%s] %s\n", recursive ? " -r" : "", dir );
	lsdir( dir, recursive );
}

// Execute scripted action. Return is negative if error
int ScriptAction::Execute()
{
	if (!strcmp( m_cmd, "lsdir" ))
	{
		char *cmd = strtok( m_buff, " \t#\r\n" );
		char *source = strtok( NULL, " \t#\r\n" );
		if (source == NULL)
		{
			printf( "missing required args for lsdir\n" );
			return -1;
		}
		bool recursive = false;
		if (*source == '-')
		{
			char *opts = source;
			source = strtok( NULL, " \t#\r\n" );
			if (source == NULL)
			{
				printf( "lsdir still needs a target in addition to options\n" );
				return -1;
			}
			switch (opts[1])
			{
				case 'r':
					recursive = true;
					break;
				default:
					printf( "Unknown option %c\n", opts[1] );
					return -1;
			}
		}
		ls( source, recursive );
		return 0;
	}
	if (!strcmp( m_cmd, "syncdir" ))
	{
		char *cmd = strtok( m_buff, " \t#\r\n" );
		char *source = strtok( NULL, " \t#\r\n" );
		char *dest = strtok( NULL, " \t#\r\n" );
		if (dest == NULL)
		{
			printf( "Missing required arguments for syncdir\n" );
			return -1;
		}
		printf( "[SYNCDIR] %s -> %s\n", source, dest );
		g_filesCopied = g_symlinksCreated = g_dirsCreated = 0;
		sync_dir( source, dest );
		printf( "%d files and %d symlinks in %d directories copied to %s\n", g_filesCopied, g_symlinksCreated, g_dirsCreated, dest );
		return g_filesCopied;
	}
	if (!strcmp( m_cmd, "cp" ))
	{
		char *cmd = strtok( m_buff, " \t#\r\n" );
		char *source = strtok( NULL, " \t#\r\n" );
		char *dest = strtok( NULL, " \t#\r\n" );
		if (dest == NULL)
		{
			printf( "missing required src and dest args for cp\n" );
			return -1;
		}
		return copy_file( source, dest );
	}
	if (!strcmp( m_cmd, "cat" ))
	{
		char *cmd = strtok( m_buff, " \t#\r\n" );
		char *source = strtok( NULL, " \t#\r\n" );
		if (source == NULL)
		{
			printf( "missing required arg for cat\n" );
			return -1;
		}
		cat( source );
		return 0;
	}
	if (!strcmp( m_cmd, "mount" ))
	{
		char *cmd = strtok( m_buff, " \t#\r\n" );
		char *source = strtok( NULL, " \t#\r\n" );
		char *mountPoint = strtok( NULL, " \t#\r\n" );
		char *fsType = strtok( NULL, " \t#\r\n" );
		char *mountFlags = strtok( NULL, " \t#\r\n" );
		char *mountOptions = strtok( NULL, " \t#\r\n" );
		int dMountFlags = (mountFlags == NULL) ? 0 : atoi(mountFlags);
		printf( "[MOUNT] %s (type %s) on %s", source, fsType, mountPoint );
		if (mountOptions == NULL)
		{
			printf( "\n" );
		}
		else
		{
			printf( " (%s)\n", mountOptions );
		}
		return mount( source, mountPoint, fsType, dMountFlags, mountOptions != NULL ? mountOptions : "" );
	}
	if (!strcmp( m_cmd, "setenv" ))
	{
		// This is normally done in the caller's context
		printf( "WARNING: %s is to be handled by caller - no action taken.\n", m_buff );
		return 0;
	}
	printf( "Unknown command %s in %s\n", m_cmd, m_buff );
	return -1;
}

// Copy a file or node from source to dest
int main( int argc, char *argv[] )
{
  // We're running directly from kernel. Device nodes may not be in place
  printf( "%s v" VER_STR " pid=%d\n", argv[0], getpid() );
  /*******
  FILE *fbcons = fopen( "/dev/ttyS00", "w" );
  if (fbcons)
  {
  	fprintf( fbcons, "%s v" VER_STR "\n", argv[0] );
  	fclose( fbcons );
  }
  *********/
  ScriptAction *actionRoot = NULL;
  ScriptAction *actionTail = NULL;
  int env_additions = 0;
  FILE *config = fopen( CONFIG_PATH, "r" );
  if (config)
  {
  	printf( "Parsing %s\n", CONFIG_PATH );
  	char buff[4096];
  	while (fgets( buff, sizeof(buff)-1, config))
  	{
  		buff[sizeof(buff)-1] = '\0';
  		char *buffStart = buff + strspn( buff, " \t\r\n" );
  		if (!*buffStart)
  		{
  			continue;
  		}
  		if (*buffStart == '#')
  		{
  			continue;
  		}
  		ScriptAction *newAction = new ScriptAction( buff );
  		if (newAction->IsSetenv())
  		{
  			env_additions++;
  		}
  		if (actionRoot == NULL)
  		{
  			actionRoot = actionTail = newAction;
  		}
  		else
  		{
  			actionTail = actionTail->Add( newAction );
  		}
  	}
  	fclose( config );
  }
  else
  {
  	printf( "Warning: could not open %s (errno=%d: %s)\n", CONFIG_PATH, errno, strerror(errno) );
  }
  if (actionRoot)
  {
  	printf( "Executing scripted actions (including %d setenv statements)\n", env_additions );
  	int scriptCount = 0;
  	int failureCount = 0;
  	for (actionTail = actionRoot; actionTail != NULL; actionTail = actionTail->Next())
  	{
  		// Skip setenv cmds for now
  		if (actionTail->IsSetenv())
  		{
  			continue;
  		}
  		int returnValue = actionTail->Execute();
  		if (returnValue < 0)
  		{
  			printf( "[%d] FAILED return value %d\n", scriptCount, returnValue );
  			failureCount++;
  		}
  		scriptCount++;
  	}
  	printf( "Scripted execution complete, %d actions with %d failures\n", scriptCount, failureCount );
  }
  else
  {
  	printf( "Falling back to default actions...\n" );
	printf( "mounting /etc as ramfs\n" );
	if (mount( "ramfs", "/etc", "ramfs", 0, "" ) == -1)
	{
	printf( "failed to mount /etc, errno = %d\n", errno );
	}
	printf( "mounting /tmp\n" );
	if (mount( "tmpfs", "/tmp", "tmpfs", 0, "mode=755" ) == -1)
	{
	printf( "failed to mount /tmp, errno = %d\n", errno );
	}
	printf( "mounting /proc\n" );
	if (mount( "proc", "/proc", "proc", 0, "" ) == -1)
	{
	printf( "failed to mount /proc, errno = %d\n", errno );
	}
	printf( "mounting /sys\n" );
	if (mount( "sysfs", "/sys", "sysfs", 0, "" ) == -1)
	{
	printf( "failed to mount /sys, errno = %d\n", errno );
	}
	printf( "Copying from /mnt/etc to /etc\n" );
	/*********
	None of this works - /bin/cp doesn't know who he is
	pid_t child;
	signal( SIGCHLD, sighandler );
	if (child = fork())
	{
	printf( "waiting for copy to complete\n" );
	fflush( stdout );
	int loop;
	for (loop = 0; loop < 10 && !g_childExited; loop++)
	{
		sleep( 1 );
	}
	}
	else
	{
	execl( "/bin/cp", "cp", "-v", "-a", "/mnt/etc/*", "/etc" );
	}
	**********/
	sync_dir( "/mnt/etc", "/etc" );
	printf( "%d files and %d symlinks in %d directories copied to /etc\n", g_filesCopied, g_symlinksCreated, g_dirsCreated );
	//cat( "/proc/mounts" );
	//cat( "/proc/devices" );
	//ls( "/dev", false );
	//ls( "/etc", true );
	//cat( "/proc/modules" );
	//ls( "/proc/self", true );
  }
  int envcount;
  for (envcount = 0; environ && environ[envcount]; envcount++)
  {
  	printf( "environ[%d]: %s\n", envcount, environ[envcount] );
  }
  // Build environment list, with an extra entry for PATH and CONFIGNAME plus terminating NULL
  char **passed_env = (char **)alloca( (envcount + 2 + env_additions) * sizeof(char*) );
  int n;
  static const char add_path[] = ":/usr/chumby/scripts";
  static const char default_path[] = "/psp/bin:/usr/bin:/bin:/usr/sbin:/sbin";
  bool foundPath = false;
  for (n = 0; n < envcount; n++)
  {
  	if (!strncmp( environ[n], "PATH=", 5 ))
  	{
  		// Append :/usr/chumby/scripts at end
  		passed_env[n] = (char*)alloca( strlen(environ[n]) + sizeof(add_path) + 1 );
  		strcpy( passed_env[n], environ[n] );
  		strcat( passed_env[n], add_path );
  		printf( "Substituting %s -> %s\n", environ[n], passed_env[n] );
  		foundPath = true;
  		continue;
  	}
  	passed_env[n] = strdupa( environ[n] );
  }
  if (!foundPath)
  {
  	passed_env[envcount] = (char *)alloca( sizeof(default_path) + sizeof(add_path) + 1 );
  	sprintf( passed_env[envcount], "%s%s", default_path, add_path );
  	printf( "Adding %s\n", passed_env[envcount] );
  	envcount++;
  }
	// Process setenv cmds
	for (actionTail = actionRoot; actionTail != NULL; actionTail = actionTail->Next())
	{
  		// Process only setenv cmds
  		if (!actionTail->IsSetenv())
  		{
  			continue;
  		}
  		const char *s = actionTail->PostCmd();
  		passed_env[envcount] = strdupa( s );
  		char *eol = strrchr( passed_env[envcount], '\n' );
  		if (eol)
  		{
  			*eol = '\0';
  		}
  		printf( "[SETENV] %s\n", passed_env[envcount] );
  		envcount++;
  	}
  passed_env[envcount] = NULL;
  char cwd[256];
  getcwd( cwd, sizeof(cwd) );
  /**********
  printf( "forking bash\n" );
  if (fork() == 0)
  {
  	pid_t child = getpid();
  	printf( "attempting execl of bash in pid %u\n", child );
  	execl( "/bin/bash", "bash" );
  	printf( "execl() failed, errno=%d\n", errno );
  }
  *************/
  printf( "Starting /sbin/init (cwd=%s) (envcount=%d)\n", cwd, envcount );
#ifndef CNPLATFORM_falconwing
  sleep( 1 );
#endif
  execle( "/sbin/init", "init", NULL, passed_env );
  //execl( "/sbin/init", "init" );
  printf( "/sbin/init failed to load! errno=%d (%s)\n", errno, strerror(errno) );
  return -1;
/*****
echo "Starting linuxrc" > /dev/ttyS00
echo "mount /etc as ramfs"
/bin/mount -n -t ramfs ramfs /etc
/bin/mount -n -t tmpfs tmpfs /tmp
/bin/cp -a /mnt/etc/* /etc

echo "etc mounted r/w" >> /tmp/boot.log

echo "Starting /sbin/init"
echo "Starting /sbin/init" >> /tmp/boot.log
exec /sbin/init

*******/
}
