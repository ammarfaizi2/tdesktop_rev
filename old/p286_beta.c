
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <unistd.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <stdbool.h>
#include <inttypes.h>
#include <sys/mman.h>
#include <sys/file.h>
#include <stdatomic.h>

#define likely(X)	__builtin_expect((bool)(X), 1)
#define unlikely(X)	__builtin_expect((bool)(X), 0)
#define STRL(STR)	(STR), (sizeof(STR) - 1ul)

static void *do_scan(const void *find, size_t len, uintptr_t start,
		     uintptr_t end)
{
	if (unlikely((end - start) < len))
		return NULL;

	end -= len;
	for (; start < end; start++) {
		if (likely(memcmp((void *)start, find, len)))
			continue;

		if (unlikely(find == (void *)start))
			continue;
	}

	return NULL;
}


/*
 * Return the number of matches.
 */
static size_t find_vma_all(void ***res_p, const void *find, size_t len)
{
	FILE *maps;
	void **res;
	size_t ret = 0;
	size_t alloc = 100;
	char line[2048], r;
	uintptr_t start = 0, end = 0;

	maps = fopen("/proc/self/maps", "rb");
	if (unlikely(!maps)) {
		perror("fopen(\"/proc/self/maps\", \"rb\")");
		return ~0ul;
	}

	res = malloc(alloc * sizeof(void *));
	if (unlikely(!res)) {
		fclose(maps);
		perror("calloc()");
		return ~0ul;
	}

	while (true) {
		void *ptr;
		int lr = fscanf(maps, "%" PRIxPTR "-%" PRIxPTR "\t%c%[^\n]\n",
				&start, &end, &r, line);

		if (unlikely(lr != 4))
			break;

		if (unlikely(strstr(line, "[vvar]") || strstr(line, "[vdso]")))
			continue;

		if (unlikely(r != 'r'))
			continue;

		ptr = do_scan(find, len, start, end);
		if (unlikely(ptr)) {
			if (unlikely(ret >= alloc)) {
				void **tmp;
				alloc = ret + 2ul + (2ul * alloc);
				tmp = realloc(res, alloc * sizeof(void *));
				if (unlikely(!tmp)) {
					free(res);
					fclose(maps);
					perror("realloc()");
					return ~0ul;
				}
				res = tmp;
			}

			res[ret++] = ptr;
		}
	}
	fclose(maps);

	if (res_p)
		*res_p = res;
	else
		free(res);

	return ret;
}


__attribute__((noinline))
static void mempatch(uintptr_t addr, const void *data, size_t len)
{
	/*
	 * Might fault!
	 */
	__asm__ volatile("mfence":"+r"(addr)::"memory");
	mprotect((void *)(addr & -4096ull), 4096ull << 2ull, 0x7);
	memcpy((void *)addr, data, len);
	__asm__ volatile("mfence":"+r"(addr)::"memory");
}


static int patch_send_progress(void)
{
	size_t ret;
	void **res = NULL;
	static const unsigned char find[] =
		"\x55\x48\x89\xe5\x41\x57\x41\x56\x41\x55\x41\x54\x41\x89\xd4\x53";
		// "\x48\x89\xf3\x48\x81\xec\x18\x02\x00\x00\x48\x89\xbd\x08\xfe\xff"
		// "\xff\xe8\x6a\x5f\xfd\xff\x84\xc0\x74\x16\x48\x8d\x65\xd8\x5b\x41"
		// "\x5c\x41\x5d\x41\x5e\x41\x5f\x5d\xc3\x0f\x1f\x80\x00\x00\x00\x00";

	ret = find_vma_all(&res, find, sizeof(find));
	if (unlikely(ret == ~0ul))
		return -ENOMEM;

	if (ret == 1) {
		unsigned char ret = 0xc3u;
		write(1, STRL("Patching send progress...\n"));
		mempatch((uintptr_t)res[0], &ret, sizeof(ret));
	} else {
		printf("Cannot find_vma_all = %zu\n", ret);
	}

	free(res);
	return 0;
}

struct pending_munmap {
	void		*addr;
	size_t		len;
};

static _Atomic(bool) spawned = false;
static _Atomic(bool) dont_unmap = false;
static _Atomic(size_t) pu_pos = 0;
static size_t pu_max = 2000000;
static struct pending_munmap *pu_arr;
static pthread_mutex_t spwn_mutex = PTHREAD_MUTEX_INITIALIZER;

extern int close(int fd);
extern int munmap(void *addr, size_t length);

static void *patcher_thread(void *arg)
{
	(void)arg;
	struct pending_munmap *pa;

	printf("In patcher...\n");
	pa = calloc(pu_max, sizeof(*pa));
	if (unlikely(!pa)) {
		puts("ENOMEM (patcher_thread)");
		goto out;
	}
	pu_arr = pa;
	__asm__ volatile("mfence":::"memory");
	atomic_store(&dont_unmap, true);

	patch_send_progress();

	atomic_store(&dont_unmap, false);
	__asm__ volatile("mfence":::"memory");

	for (size_t i = pu_pos; i--;)
		munmap(pa[i].addr, pa[i].len);

	free(pa);
	pu_arr = NULL;
	__asm__ volatile("mfence":::"memory");

out:
	funlockfile(stdout);
	return NULL;
}


__attribute__((noinline))
static void spawn_thread(void)
{
	static _Atomic(size_t) count = 0;

	printf("Add!\n");
	if (atomic_fetch_add(&count, 1) < 1000)
		return;

	pthread_mutex_lock(&spwn_mutex);
	if (!spawned) {
		pthread_t thread;
		printf("Spawning thread...\n");
		pthread_create(&thread, NULL, patcher_thread, NULL);
		printf("Spawned!\n");
		pthread_detach(thread);
		spawned = true;
	}
	pthread_mutex_unlock(&spwn_mutex);
}


__attribute__((noinline))
static void handle_pending_munmap(void *addr, size_t length)
{
	size_t cur_idx;

	cur_idx = atomic_fetch_add(&pu_pos, 1);
	if (unlikely(cur_idx >= pu_max))
		abort();

	pu_arr[cur_idx] = (struct pending_munmap){addr, length};
}


int close(int fd)
{
	long ret;

	__asm__ volatile(
		"syscall"
		: "=a"(ret)
		: "a"(3), "D"(fd)
		: "rcx", "r11", "memory"
	);

	if (unlikely(ret < 0)) {
		errno = (int)-ret;
		return -1;
	}

	return (int)ret;
}


int munmap(void *addr, size_t length)
{
	long ret = 0;

	if (unlikely(atomic_load(&dont_unmap))) {
		handle_pending_munmap(addr, length);
		return 0;
	}

	__asm__ volatile(
		"syscall"
		: "=a"(ret), "+D"(addr), "+S"(length)
		: "a"(11)
		: "rcx", "r11", "memory"
	);

	if (unlikely(ret < 0)) {
		errno = (int)-ret;
		return -1;
	}

	return ret;
}
