// SPDX-License-Identifier: GPL-2.0
/*
 * @author Ammar Faizi <ammarfaizi2@gmail.com> https://www.facebook.com/ammarfaizi2
 */
#define _GNU_SOURCE
#include <dlfcn.h>
#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdint.h>
#include <pthread.h>
#include <sys/mman.h>
#include <stdatomic.h>


struct pending_unmap_t {
	void	*addr;
	size_t	len;
};


static char spawned = 0;
static _Atomic(char) dont_unmap = 0;
static pthread_mutex_t spwn_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t umap_mutex = PTHREAD_MUTEX_INITIALIZER;
static void spawn_thread(void);
static struct pending_unmap_t *pend_unmap;
static size_t pu_pos = 0, pu_max = 2000000;
static FILE *ori_stdout = NULL;
static FILE *ori_stderr = NULL;
static FILE *null_out = NULL;

#define FPRINT(...)			\
do {					\
	fprintf(__VA_ARGS__);		\
	fflush(ori_stdout);		\
} while (0)


int munmap(void *addr, size_t length)
{
	long ret;

	if (__builtin_expect(spawned == 0, 0))
		spawn_thread();

	if (__builtin_expect(atomic_load(&dont_unmap), 0)) {
		pthread_mutex_lock(&umap_mutex);
		if (pu_pos >= pu_max)
			abort();
		pend_unmap[pu_pos++] = (struct pending_unmap_t){addr, length};
		pthread_mutex_unlock(&umap_mutex);
		return 0;
	}

	__asm__ volatile(
		"syscall"
		: "=a"(ret), "+D"(addr), "+S"(length)
		: "a"(11)
		: "memory"
	);

	if (__builtin_expect(ret < 0, 0)) {
		errno = (int)-ret;
		return -1;
	}
	return ret;
}


static void *my_thread(void *arg)
{
	void *so;
	size_t len;
	void (*func)(void);
	char so_file[1024];
	struct pending_unmap_t *pend;

	pend = calloc(pu_max, sizeof(void *));
	if (!pend) {
		puts("ENOMEM!");
		abort();
	}
	pend_unmap = pend;

	null_out = fopen("/dev/null", "wb");
	if (!null_out) {
		puts("Cannot open /dev/null");
		abort();
	}

	ori_stdout = stdout;
	ori_stderr = stderr;
	stdout = null_out;
	stderr = null_out;
	FPRINT(ori_stdout, "Starting...\n");
	/* Wait a bit, let the app show up. */
	sleep(3);
again:
	pu_pos = 0;
	FPRINT(ori_stdout, "Enter (.so) file name to be injected: ");
	if (!fgets(so_file, sizeof(so_file), stdin))
		goto out;

	/* Cut the newline */
	len = strlen(so_file);
	if (so_file[len - 1] == '\n')
		so_file[len - 1] = '\0';

	if (len <= 1)
		goto again;


	errno = ENOENT;
	so = dlopen(so_file, RTLD_NOW);
	if (!so) {
		FPRINT(ori_stdout, "Failed to open \"%s\" (%s)\n", so_file,
			strerror(errno));
		goto again;
	}

	errno = ENOENT;
	func = dlsym(so, "gnu_inject");
	if (!func) {
		FPRINT(ori_stdout, "Cannot find \"gnu_inject\" symbol (%s)\n",
			strerror(errno));
		goto again;
	}

	atomic_store(&dont_unmap, 1);
	__asm__ volatile("mfence":::"memory");
	FPRINT(ori_stdout, "Syncing...\n");

	// Must be enough to wait for munmap (don't make race!)
	sync();
	sleep(2);

	// Might fault!
	func();

	__asm__ volatile("mfence":::"memory");
	atomic_store(&dont_unmap, 0);
	dlclose(so);

	FPRINT(ori_stdout, "Unmapping pending area(s) (pending_count=%zu)...\n",
	       pu_pos);

	while (pu_pos--)
		munmap(pend[pu_pos].addr, pend[pu_pos].len);

	FPRINT(ori_stdout, "Finished!\n");
	goto again;
out:
	free(pend);
	pend_unmap = NULL;
	FPRINT(ori_stdout, "Closing injector...\n");
	stdout = ori_stdout;
	stderr = ori_stderr;
	pend_unmap = NULL;
	fclose(null_out);
	(void)arg;
	return NULL;
}


static void spawn_thread(void)
{
	pthread_mutex_lock(&spwn_mutex);
	if (!spawned) {
		pthread_t thread;
		pthread_create(&thread, NULL, my_thread, NULL);
		pthread_detach(thread);
		spawned = 1;
	}
	pthread_mutex_unlock(&spwn_mutex);
}
