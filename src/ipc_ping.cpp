// $Id$
// ipc_ping.cpp - ipc lock test
// Takes a single arg which is a string to send to console
// See ipc_pingpong.sh for suggested usage.
// This is basically a testbed program for using shared memory
// with semaphores for coordination amongst multiple processes.
// It can be built with USE_PTHREAD_MUTEX which uses an interthread
// mutex mechanism in shared memory, but sometimes (regularly)
// causes one or more processes to block permanently while trying
// to acquire a lock, and also (frequently) triggers the 1 second
// timeout waiting for a turn to appear, which should never happen
// with a cumulative total delay of <= 100ms.
// Use of pthread_mutex_lock() is therefore a bad idea for interprocess
// control, and is questionable for interthread control. The semaphore
// locking mechanism used here seems to hold up well, which was the
// point of putting together this test app.

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h> 
#include <errno.h>
#include <stdarg.h>
#include <string.h>
#include <sys/shm.h>
#include <sys/time.h>
#include <signal.h>
#include <limits.h>

// pthread_mutex_lock creates blocking conditions and doesn't work well for tight IPC
//#define USE_PTHREAD_MUTEX 1

#ifdef USE_PTHREAD_MUTEX

//#define __USE_GNU
#include <pthread.h>
//#undef __USE_GNU

#else
// Use semaphore to control access to shared memory block
#include <sys/ipc.h>
#include <sys/sem.h>
#endif

// Mark code wedged in for Chumby debugging
#define CHUMBY_DBG(sol,fmt,args...)

// Uncomment for verbose debugging
#define CHUMBY_VERBOSE_DEBUG

#ifdef CHUMBY_VERBOSE_DEBUG
#undef CHUMBY_DBG
extern void _chumby_verbose_dbg( int, const char *, ... );
#define CHUMBY_DBG(sol,fmt,args...) _chumby_verbose_dbg(sol,fmt,args)
#endif

#ifdef USE_PTHREAD_MUTEX
// Static pthread_mutex_t used to initialize shared memory at runtime
static pthread_mutex_t __init_mutex = PTHREAD_ERRORCHECK_MUTEX_INITIALIZER_NP;
#endif

#include <sys/types.h>
#include <unistd.h>

void _chumby_verbose_dbg(int startofline, const char *fmt, ...)
{
	va_list ap;
	static FILE *f = NULL;
	va_start( ap, fmt );
	if (!f)
	{
		//f = stdout;
		///****
		char logpath[64];
		sprintf( logpath, "/tmp/ipc-dbg.%u", getpid() );
		f = fopen( logpath, "a" );
		if (!f) return;
		//****/
	}
	if (startofline)
	{
		struct timeval t;
		gettimeofday( &t, NULL );
		fprintf( f, "[%lu.%06lu] ", t.tv_sec, t.tv_usec );
	}
	vfprintf( f, fmt, ap );
	va_end( ap );
	// Flush any line containing a LF
	if (strchr( fmt, '\n' ))
	{
		fflush( f );
	}
}

// Structure used for process slots
typedef struct tagPID_slot
{
	pid_t pid; // nonzero if a valid process
} PID_SLOT;

// Structure used for shared memory
typedef struct tagIPC_mutex
{
#ifdef USE_PTHREAD_MUTEX
	pthread_mutex_t mutex;
//#else
// No shared resource needed for semaphores
#endif
	pid_t last_locker;
	unsigned long joined_pids;
	unsigned long pid_index; // Index of pid for next turn
	PID_SLOT slots[50];
	unsigned long unused[8];
} IPC_MUTEX;

static key_t ipcKey = 0x7337a1a5;
static IPC_MUTEX *ipc_pi = NULL;

#ifndef USE_PTHREAD_MUTEX
static int sem_handle = -1;
#endif

// Lock interprocess mutex. Returns pthread_mutex_lock() return values
// (0 if success, else errno)
int ipc_lock_mutex(int trylock = 0)
{
	static int shmid = -1;
	int shm_created = 0;
	int ret;

	// Attach to shared memory or create if it doesn't exist
	if (-1 == shmid)
	{
		shmid = shmget( ipcKey, sizeof(IPC_MUTEX), 0666 );
		if (-1 == shmid)
		{
			shmid = shmget( ipcKey, sizeof(IPC_MUTEX), IPC_CREAT|0666 );
			if (-1 == shmid)
			{
				CHUMBY_DBG( 1, "%s() failed to create errno=%d\n", __FUNCTION__, errno );
				return errno;
			}
			CHUMBY_DBG( 1, "%s() created shmid=%d\n", __FUNCTION__, shmid );
			shm_created = 1;
		}
	}

	if (!ipc_pi)
	{
		ipc_pi = (IPC_MUTEX *)shmat( shmid, NULL, 0 );
		if (NULL == ipc_pi)
		{
			CHUMBY_DBG( 1, "%s() shmat() failed, errno=%d\n", __FUNCTION__, errno );
			return errno;
		}
		CHUMBY_DBG( 1, "%s() shmat() returned 0x%08lx, old pid=%u, joined_pids=%lu, index=%lu\n",
			__FUNCTION__, (unsigned long)ipc_pi, ipc_pi->last_locker, ipc_pi->joined_pids, ipc_pi->pid_index );
	}

	// Initialize if shared memory has just been created
	if (shm_created)
	{
#ifdef USE_PTHREAD_MUTEX
		int err;
		memcpy( &ipc_pi->mutex, &__init_mutex, sizeof(__init_mutex) );
#endif
		ipc_pi->joined_pids = 0;
		ipc_pi->pid_index = 0;
		memset( &ipc_pi->slots, 0, sizeof(ipc_pi->slots) );
#ifdef USE_PTHREAD_MUTEX
		err = pthread_mutex_init( &ipc_pi->mutex, NULL );
		CHUMBY_DBG( 1, "%s() pthread_mutex_init() returned %d\n",
			__FUNCTION__, err );
		if (err < 0)
		{
			//shmdt( ipc_pi );
			//ipc_pi = NULL;
			return err;
		}
#endif
	}

#ifndef USE_PTHREAD_MUTEX
	if (-1 == sem_handle)
	{
		// To detect whether it already exists, try to open
		// with exclusive-create flag
		sem_handle = semget( ipcKey, 1, IPC_CREAT | IPC_EXCL | 0666 );
		// If it already exists, open normally
		if (sem_handle < 0)
		{
			sem_handle = semget( ipcKey, 1, 0666 );
		}
		// Otherwise set initial value
		else
		{
			union semun {
				int              val;    /* Value for SETVAL */
				struct semid_ds *buf;    /* Buffer for IPC_STAT, IPC_SET */
				unsigned short  *array;  /* Array for GETALL, SETALL */
				struct seminfo  *__buf;  /* Buffer for IPC_INFO
                                           (Linux-specific) */
			} init_arg;
			init_arg.val = 1;
			int init_result = semctl( sem_handle, 0, SETVAL, init_arg );
			if (init_result < 0)
			{
				CHUMBY_DBG( 1, "semctl() returned %d, errno=%d\n", init_result, errno );
			}
		}
		if (sem_handle < 0)
		{
			CHUMBY_DBG( 1, "semget() returned %d, errno=%d\n", sem_handle, errno );
			return errno;
		}
	}

	// Acquire the semaphore
	struct sembuf sb[1];
	sb[0].sem_num = 0;
	sb[0].sem_op = -1;
	sb[0].sem_flg = trylock ? IPC_NOWAIT : 0;
	ret = semop( sem_handle, sb, 1 );
	if (ret < 0 && errno != -EAGAIN)
	{
		CHUMBY_DBG( 1, "semop() returned %d, errno=%d\n", ret, errno );
	}
#endif

#ifdef USE_PTHREAD_MUTEX
	// Attempt the lock
	if (trylock)
	{
		ret = pthread_mutex_trylock( &ipc_pi->mutex );
	}
	else
	{
		ret = pthread_mutex_lock( &ipc_pi->mutex );
	}
#endif

	//ret = 0;
	if (!ret)
	{
		ipc_pi->last_locker = getpid();
	}
	return ret;
}

// Unlock interprocess mutex. Returns 0 if successful or errno
// from pthread_mutex_unlock()
int ipc_unlock_mutex()
{
	if (!ipc_pi)
	{
		return -EINVAL;
	}

#ifdef USE_PTHREAD_MUTEX
	// Attempt the unlock
	return pthread_mutex_unlock( &ipc_pi->mutex );
#else
	// Release the semaphore (set it back to 1)
	struct sembuf sb[1];
	sb[0].sem_num = 0;
	sb[0].sem_op = 0; // Check to make sure it's 0
	sb[0].sem_flg = IPC_NOWAIT;
	int ret = semop( sem_handle, sb, 1 );
	if (ret < 0 && errno != -EAGAIN)
	{
		CHUMBY_DBG( 1, "semop() returned %d, errno=%d\n", ret, errno );
	}
	if (!ret)
	{
		// Set back to 1
		sb[0].sem_num = 0;
		sb[0].sem_op = 1; // Add this to semaphore value
		sb[0].sem_flg = 0;
		ret = semop( sem_handle, sb, 1 );
	}
	return ret;
#endif
	//return 0;
}

int in_critical_section = 0;
int g_abort = 0;

// Signal handler
void sighandler( int signum );
void sighandler( int signum )
{
	CHUMBY_DBG( 1, "got signal %d, in critical section = %d\n", signum, in_critical_section );
	// Assume we're only trapping SIGINT, SIGTERM, SIGABRT, etc
	g_abort++;
	if (g_abort == 1)
	{
		signal( signum, sighandler );
	}
	else
	{
		// Attempt forced unlock
		if (in_critical_section)
		{
			CHUMBY_DBG( 1, "second signal(%d), attempting forced unlock\n", signum );
			ipc_unlock_mutex();
			exit( -1 );
		}
	}
}

// True if process appears valid
bool is_process_valid( pid_t p )
{
	char procdir[64];
	sprintf( procdir, "/proc/%u/stat", p );
	return (0 == access( procdir, 0 ));
}

int main( int argc, char *argv[] )
{
	printf( "%s v0.13 pid (%u)\n", argv[0], getpid() );
	if (argc < 2)
	{
		printf( "Syntax: %s string [delay] [turns]\n", argv[0] );
		return -1;
	}
	unsigned long delay = 5;
	unsigned long turns = 100;
	if (argc > 2)
	{
		delay = atol( argv[2] );
	}
	if (argc > 3)
	{
		turns = atol( argv[3] );
	}
	printf( "(%u) Delay time = %lums, turns = %lu\n", getpid(), delay, turns );
	int attempts;
	int lock;

	// Set signal handler
	signal( SIGINT, sighandler );
	signal( SIGTERM, sighandler );
	signal( SIGABRT, sighandler );

	// Total timeout is 50ms
	for (attempts = 0; attempts < 100; attempts++)
	{
		lock = ipc_lock_mutex(1);
		if (!lock) break;
		usleep( 500 );
	}
	if (lock < 0 && ipc_pi)
	{
		unsigned long locker_pid = ipc_pi->last_locker;
		if (!is_process_valid( locker_pid ))
		{
			printf( "Process %lu appears defunct, attempting forced unlock\n",
				locker_pid );
			lock = ipc_unlock_mutex();
			printf( "Unlock returned %d - attempting lock one more time...\n", lock );
			lock = ipc_lock_mutex(1);
		}
	}
	if (lock < 0)
	{
		printf( "Lock failed, returned %d (errno=%d)\n", lock, errno );
		return -1;
	}
	in_critical_section = 1;
	unsigned long newCount = ipc_pi->joined_pids;
	unsigned long myIndex = 0xffffffff;
	ipc_pi->last_locker = getpid();
	// Check for defunct processes
	int n;
	for (n = 0; n < ipc_pi->joined_pids; n++)
	{
		if (ipc_pi->slots[n].pid == 0 || !is_process_valid( ipc_pi->slots[n].pid ))
		{
			if (ipc_pi->slots[n].pid)
			{
				printf( "(%u) Process %u appears defunct, freeing slot #%d\n",
					getpid(), ipc_pi->slots[n].pid, n );
				ipc_pi->slots[n].pid = 0;
			}
			if (myIndex == 0xffffffff)
			{
				// Reuse this entry
				printf( "(%u) Reusing slot #%d\n", getpid(), n );
				myIndex = n;
				continue;
			}
			if (ipc_pi->pid_index == n)
			{
				ipc_pi->pid_index = (ipc_pi->pid_index + 1) % ipc_pi->joined_pids;
				printf( "(%u) Advancing index past defunct entry to #%d\n",
					getpid(), ipc_pi->pid_index );
			}
		}
		else
		{
			printf( "(%u) process %u in slot #%d appears valid\n", 
				getpid(), ipc_pi->slots[n].pid, n );
		}
	}
	// If no free slots found, add one
	if (myIndex == 0xffffffff)
	{
		if (newCount >= 50)
		{
			in_critical_section = 0;
			ipc_unlock_mutex();
			printf( "(%u) No free process slots\n", getpid() );
			return -1;
		}
		myIndex = newCount;
		newCount++;
		ipc_pi->joined_pids = newCount;
	}
	ipc_pi->slots[myIndex].pid = getpid();

	// Lop off trailing empty slots
	while (ipc_pi->joined_pids && ipc_pi->slots[ipc_pi->joined_pids-1].pid == 0)
		ipc_pi->joined_pids--;

	if (ipc_pi->joined_pids != newCount)
	{
		printf( "(%u) Reduced pid count from %lu to %lu after removing empty trailing process slots\n",
			getpid(), newCount, ipc_pi->joined_pids );
		newCount = ipc_pi->joined_pids;
	}

	in_critical_section = 0;
	lock = ipc_unlock_mutex();
	printf( "(%u) New pid count = %lu, unlock result = %d\n", getpid(), newCount, lock );
	CHUMBY_DBG( 1, "(%u) pid count = %lu, unlock = %d\n", getpid(), newCount, lock );
	struct timeval t;
	struct timeval prelock_t;
	gettimeofday( &t, NULL );
	unsigned long long lastTurn = t.tv_sec * 1000000LL + t.tv_usec;
	bool taking_turns = true;
	unsigned long loop_counter = 0;
	while (taking_turns && !g_abort)
	{
		loop_counter++;
		gettimeofday( &prelock_t, NULL );
		lock = ipc_lock_mutex();
		if (lock < 0)
		{
			printf( "(%u) lock failed, returned %d (errno=%d)\n", getpid(), lock, errno );
			// Remove ourselves anyway
			ipc_pi->slots[myIndex].pid = 0;
			return -1;
		}
		in_critical_section = 1;
		if (ipc_pi->joined_pids == 0)
		{
			printf( "(%u) pid count is 0, exiting\n", getpid() );
			ipc_pi->slots[myIndex].pid = 0;
			in_critical_section = 0;
			ipc_unlock_mutex();
			return -1;
		}
		CHUMBY_DBG( 1, "lock=%d prelock=%lu.%06lu\n", lock, prelock_t.tv_sec, prelock_t.tv_usec );
		gettimeofday( &t, NULL );
		unsigned long long now = t.tv_sec * 1000000LL + t.tv_usec;
		if (ipc_pi->pid_index == myIndex)
		{
			lastTurn = now;
			ipc_pi->pid_index = (ipc_pi->pid_index + 1) % ipc_pi->joined_pids;
			// Skip defunct entries
			while (ipc_pi->pid_index != myIndex && ipc_pi->slots[ipc_pi->pid_index].pid == 0)
			{
				ipc_pi->pid_index = (ipc_pi->pid_index + 1) % ipc_pi->joined_pids;
			}
			CHUMBY_DBG( 1, "HIT %s new lastTurn=%llu idx=%lu\n", argv[1], lastTurn, ipc_pi->pid_index );
			printf( "<%lu.%06lu> [%lu/%lu] (%u) #%lu next:%lu %s\n", 
				t.tv_sec, t.tv_usec,
				myIndex, ipc_pi->joined_pids, 
				getpid(), turns, ipc_pi->pid_index, argv[1] );
			fflush( stdout );
			if (turns)
			{
				turns--;
				if (!turns)
				{
					taking_turns = false;
					ipc_pi->slots[myIndex].pid = 0;
				}
			}
		}
		else
		{
			unsigned long long elapsed = now - lastTurn;
			CHUMBY_DBG( 1, "miss %lu!=%lu elapsed %llu now %llu last %llu\n",
				ipc_pi->pid_index, myIndex, elapsed, now, lastTurn );
			if (elapsed > 1000000LL)
			{
				printf( "(%u) Timeout 1 (%llums since last turn; now=%llu, prev=%llu) - resetting index (was %lu now %lu)\n", 
					getpid(),
					elapsed / 1000LL, now / 1000LL, lastTurn / 1000LL,
					ipc_pi->pid_index, myIndex  );
				unsigned long lock_secs = t.tv_sec - prelock_t.tv_sec;
				long lock_usecs = t.tv_usec - prelock_t.tv_usec;
				if (lock_usecs < 0 && lock_secs > 0)
				{
					lock_secs--;
					lock_usecs += 1000000L;
				}
				CHUMBY_DBG( 1, "timeout1 loop %lu\n", loop_counter );
				printf( "(%u) loop %lu lock took %ld.%06ld to acquire\n",
					getpid(), loop_counter, lock_secs, lock_usecs );
				ipc_pi->pid_index = myIndex;
			}
			else if (elapsed > 2000000LL)
			{
				printf( "(%u) Timeout occurred (%llums since last turn; now=%llu, prev=%llu)\n",
					getpid(),
					elapsed / 1000LL, now / 1000LL, lastTurn / 1000LL );
				ipc_pi->slots[myIndex].pid = 0;
				in_critical_section = 0;
				ipc_unlock_mutex();
				return -1;
			}
		}
		in_critical_section = 0;
		lock = ipc_unlock_mutex();
		CHUMBY_DBG( 1, "unlock returned %d\n", lock );
		if (taking_turns && delay)
		{
			usleep( delay * 1000L );
			CHUMBY_DBG( 1, "delay=%lums\n", delay );
		}
	}
	printf( "(%lu) turns completed, %s\n", getpid(), g_abort ? "abort requested" : "exiting" );
	return 0;
}

