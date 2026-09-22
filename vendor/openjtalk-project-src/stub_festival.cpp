/*
 *  TOPPERS/FI4 Kernel
 *      Toyohashi Open Platform for Embedded Real-Time Systems/
 *      Fullset uItron4 Kernel
 * 
 *  Copyright (C) 2003-2004 by Monami software Limited Partnership, JAPAN
 * 
 *  上記著作権者は，以下の (1)～(4) の条件か，Free Software Foundation 
 *  によって公表されている GNU General Public License の Version 2 に記
 *  述されている条件を満たす場合に限り，本ソフトウェア（本ソフトウェア
 *  を改変したものを含む．以下同じ）を使用・複製・改変・再配布（以下，
 *  利用と呼ぶ）することを無償で許諾する．
 *  (1) 本ソフトウェアをソースコードの形で利用する場合には，上記の著作
 *      権表示，この利用条件および下記の無保証規定が，そのままの形でソー
 *      スコード中に含まれていること．
 *  (2) 本ソフトウェアを，ライブラリ形式など，他のソフトウェア開発に使
 *      用できる形で再配布する場合には，再配布に伴うドキュメント（利用
 *      者マニュアルなど）に，上記の著作権表示，この利用条件および下記
 *      の無保証規定を掲載すること．
 *  (3) 本ソフトウェアを，機器に組み込むなど，他のソフトウェア開発に使
 *      用できない形で再配布する場合には，次のいずれかの条件を満たすこ
 *      と．
 *    (a) 再配布に伴うドキュメント（利用者マニュアルなど）に，上記の著
 *        作権表示，この利用条件および下記の無保証規定を掲載すること．
 *    (b) 再配布の形態を，別に定める方法によって，TOPPERSプロジェクトに
 *        報告すること．
 *  (4) 本ソフトウェアの利用により直接的または間接的に生じるいかなる損
 *      害からも，上記著作権者およびTOPPERSプロジェクトを免責すること．
 * 
 *  本ソフトウェアは，無保証で提供されているものである．上記著作権者お
 *  よびTOPPERSプロジェクトは，本ソフトウェアに関して，その適用可能性も
 *  含めて，いかなる保証も行わない．また，本ソフトウェアの利用により直
 *  接的または間接的に生じたいかなる損害に関しても，その責任を負わない．
 * 
 *  @(#) $Id: kmem.c 9217 2017-11-10 04:17:48Z nagasima $
 */

/*
 *  カーネルメモリアロケータ
 */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>

#define Inline static
#include <assert.h>
#include "queue.h"
#ifdef MPL_DEBUG
#include "windows.h"
#endif


typedef struct hts_memory_control_block {
	QUEUE queue;
	uint32_t magic;
	bool used;
	size_t size;
#ifdef MPL_DEBUG
	const char *filename;
	int linenumber;
	const char *free_filename;
	int free_linenumber;
#endif
} HTSMEMB;

bool htsmem_setup(void *buf, size_t bufsz, HTSMEMB **p_htsmemb);
bool htsmem_allocate(HTSMEMB *htsmemb, size_t size, void **p_blk
#ifdef MPL_DEBUG
	, const char *filename, int linenumber
#endif
);
bool htsmem_release(HTSMEMB *htsmemb, void *buf
#ifdef MPL_DEBUG
	, const char *filename, int linenumber
#endif
);

uint8_t htsmem[16 * 1024 * 1024];

HTSMEMB *p_htsmemb;

extern "C"
void wmem_init()
{
	htsmem_setup(htsmem, sizeof(htsmem), &p_htsmemb);
}

extern "C"
void *safe_wcalloc(size_t size
#ifdef MPL_DEBUG
	, const char *filename, int linenumber
#endif
)
{
	void *result = NULL;

	if (!htsmem_allocate(p_htsmemb, size, &result
#ifdef MPL_DEBUG
		, filename, linenumber
#endif
	))
		return NULL;

	return result;
}

extern "C"
void wfree(void *mem
#ifdef MPL_DEBUG
	, const char *filename, int linenumber
#endif
)
{
	if (mem == NULL)
		return;

	htsmem_release(p_htsmemb, mem
#ifdef MPL_DEBUG
		, filename, linenumber
#endif
	);
}

extern "C"
char *wstrdup(const char *string
#ifdef MPL_DEBUG
	, const char *filename, int linenumber
#endif
)
{
   char *buff = (char *)safe_wcalloc((strlen(string) + 1) * sizeof(char)
#ifdef MPL_DEBUG
	   , filename, linenumber
#endif
   );
   strcpy(buff, string);
   return buff;
}

#define HTSMEM_MAGIC (0xCAFEBABE)
#define HTSMEM_DELTA 40

typedef union {
	long l;
#ifdef _int64_
	_int64_ i64;
#endif
#if (__STDC_VERSION__ >= 199901L)
	long long ll;
#endif
	//double d;
	//long double ld;
} __dummy_for_align;

static const int maxalign = alignof(__dummy_for_align);

bool
htsmem_setup(void *buf, size_t bufsz, HTSMEMB **p_htsmemb
)
{
	HTSMEMB *top;
	HTSMEMB *htsmemb;

	if (bufsz < sizeof(HTSMEMB) * 2 ||
	    (size_t)buf % alignof(__dummy_for_align) != 0) {
		return false;
	}

	top = (HTSMEMB *)buf;
	top->magic = 0;
	top->used = true;
	top->size = 0;
#ifdef MPL_DEBUG
	top->filename = __FILE__;
	top->linenumber = __LINE__;
#endif
	queue_initialize(&(top->queue));

	htsmemb = top + 1;
	htsmemb->magic = HTSMEM_MAGIC;
	htsmemb->used = false;
	htsmemb->size = bufsz - sizeof(HTSMEMB);
	queue_insert_prev((QUEUE *)top, (QUEUE *)htsmemb);

	*p_htsmemb = top;

	return true;
}


bool htsmem_allocatable(HTSMEMB *htsmemb, size_t size)
{
	HTSMEMB *ptr;

	/* alignmentをとっておく。 */
	size = ((size + sizeof(HTSMEMB)) + sizeof(__dummy_for_align) - 1) & - sizeof(__dummy_for_align);

	for (ptr = htsmemb; ptr != htsmemb; ptr = (HTSMEMB *)(ptr->queue.p_next)) {
		if (ptr->magic != HTSMEM_MAGIC) {
			return false;
		}

		if (ptr->used == false && ptr->size >= size) {
			/* 見つかった */
			return true;
		}
	}

	return false;
}

bool htsmem_allocate(HTSMEMB *htsmemb, size_t size, void **p_blk
#ifdef MPL_DEBUG
	, const char *filename, int linenumber
#endif
)
{
	HTSMEMB *ptr;

	/* alignmentをとっておく。 */
	size = ((size + sizeof(HTSMEMB)) + sizeof(__dummy_for_align) - 1) & - sizeof(__dummy_for_align);

	/* 空領域の探索 */
	for (ptr = (HTSMEMB *)(htsmemb->queue.p_next); ptr != htsmemb; ptr = (HTSMEMB *)(ptr->queue.p_next)) {
		if (ptr->magic != HTSMEM_MAGIC) {
			return false;
		}

		if (ptr->used == false && ptr->size >= size) {
			/* 見つかった */

			if (ptr->size - size > HTSMEM_DELTA) {
				/* 空領域に十分な余裕があるので、分割する。*/
				HTSMEMB *s;
				s = (HTSMEMB *)((uint8_t *)ptr + size);
				s->size = ptr->size - size;
				s->magic = ptr->magic = HTSMEM_MAGIC;
				s->used = false;

				ptr->size = size;
				ptr->used = true;
#ifdef MPL_DEBUG
				ptr->filename = filename;
				ptr->linenumber = linenumber;
#endif

				queue_insert_prev((QUEUE *)ptr, (QUEUE *)s);

			} else {
				/* 空領域に十分な余裕が無いので、全てを占有する。*/
				ptr->used = true;
#ifdef MPL_DEBUG
				ptr->filename = filename;
				ptr->linenumber = linenumber;
#endif
			}

			/* ユーザがつかえるのは、ヘッダの直後から。*/
			*p_blk = (void *)(ptr + 1);
			return true;
		}
	}

	return false;
}


bool htsmem_release(HTSMEMB *htsmemb, void *buf
#ifdef MPL_DEBUG
	, const char *filename, int linenumber
#endif
)
{
	HTSMEMB *hdr;
	HTSMEMB *check_htsmemb;

	if ((HTSMEMB *)buf < htsmemb) {
#ifdef MPL_DEBUG
		DebugBreak();
#endif
		/*
		 *  この条件はあり得ない。
		 */
		return false;
	}
	hdr = (HTSMEMB *)buf - 1;

	if (hdr->magic != HTSMEM_MAGIC) {
#ifdef MPL_DEBUG
		DebugBreak();
#endif
		/*
		 * マジックナンバが異れば、
		 * 不正なアドレスを渡して来たとみなす。
		 */
		return false;
	}

	hdr->used = false;
/* Toppers porting F.Okazaki Delete start　magicを消すと二度と使用できないよ */
/*	hdr->magic = 0; */
#ifdef MPL_DEBUG
	hdr->free_filename = filename;
	hdr->free_linenumber = linenumber;
#endif
/* Toppers porting F.Okazaki Delete end */

/* Toppers porting F.Okazaki Delete,Append start　下のと逆です*/
/*	check_htsmemb = (HTSMEMB *)(hdr->queue.p_next);*/
	check_htsmemb = (HTSMEMB *)(hdr->queue.p_prev);
/* Toppers porting F.Okazaki Delete,Append end */
	if (check_htsmemb->used == false) {
		hdr->size += check_htsmemb->size;
		check_htsmemb->magic = 0;
		check_htsmemb->queue.p_next->p_prev = check_htsmemb->queue.p_prev;
		check_htsmemb->queue.p_prev->p_next = check_htsmemb->queue.p_next;
	}

	/*
	 *  直前も空領域なら、合併する。
	 */
/* Toppers porting F.Okazaki Delete,Append start　上のと逆です*/
/*	check_htsmemb = (HTSMEMB *)(hdr->queue.p_prev);*/
	check_htsmemb = (HTSMEMB *)(hdr->queue.p_next);
/* Toppers porting F.Okazaki Delete,Append end */
	if (check_htsmemb->used == false) {
		check_htsmemb->size += hdr->size;
		hdr->magic = 0;
		hdr->queue.p_next->p_prev = hdr->queue.p_prev;
		hdr->queue.p_prev->p_next = hdr->queue.p_next;
	}

	return true;
}

bool
htsmem_status(HTSMEMB *htsmemb, size_t *remain, size_t *maxblk)
{
	HTSMEMB *blk;
	QUEUE *entry = NULL;
	size_t maxblksize = 0;

	if (htsmemb->magic != 0) {
		/*
		 * マジックナンバが異れば、
		 * 不正なアドレスを渡して来たとみなす。
		 */
		return false;
	}

	*remain = 0;
	while((entry = queue_enumerate((QUEUE *)htsmemb, entry)) != NULL) {
		blk = (HTSMEMB *)entry;

		if (blk->used == false) {
			*remain += blk->size;
			if (maxblksize < blk->size) {
				maxblksize = blk->size;
			}
		}
	}

	/*
	 * ref_mpl の仕様に合わせた。
	 * 「すぐに獲得できる最大のメモリブロックサイズがsize_t型で表わせる
	 * 最大値よりも大きい場合には、size_t型で表わせる最大値を…。」
	 */
	if (maxblksize >= UINT_MAX) {
		*maxblk = UINT_MAX;
	} else {
		*maxblk = (size_t)maxblksize;
	}

	return true;
}
