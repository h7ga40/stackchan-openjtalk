/*
 *  TOPPERS Software
 *      Toyohashi Open Platform for Embedded Real-Time Systems
 * 
 *  Copyright (C) 2000 by Embedded and Real-Time Systems Laboratory
 *                              Toyohashi Univ. of Technology, JAPAN
 *  Copyright (C) 2006-2011 by Embedded and Real-Time Systems Laboratory
 *              Graduate School of Information Science, Nagoya Univ., JAPAN
 * 
 *  上記著作権者は，以下の(1)～(4)の条件を満たす場合に限り，本ソフトウェ
 *  ア（本ソフトウェアを改変したものを含む．以下同じ）を使用・複製・改
 *  変・再配布（以下，利用と呼ぶ）することを無償で許諾する．
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
 *      また，本ソフトウェアのユーザまたはエンドユーザからのいかなる理
 *      由に基づく請求からも，上記著作権者およびTOPPERSプロジェクトを
 *      免責すること．
 * 
 *  本ソフトウェアは，無保証で提供されているものである．上記著作権者お
 *  よびTOPPERSプロジェクトは，本ソフトウェアに関して，特定の使用目的
 *  に対する適合性も含めて，いかなる保証も行わない．また，本ソフトウェ
 *  アの利用により直接的または間接的に生じたいかなる損害に関しても，そ
 *  の責任を負わない．
 * 
 *  @(#) $Id: queue.h 9106 2017-10-10 04:00:17Z nagasima $
 */

/*
 *		キュー操作ライブラリ
 *
 *  このキュー操作ライブラリでは，キューヘッダを含むリング構造のダブル
 *  リンクキューを扱う．具体的には，キューヘッダの次エントリはキューの
 *  先頭のエントリ，前エントリはキューの末尾のエントリとする．また，キ
 *  ューの先頭のエントリの前エントリと，キューの末尾のエントリの次エン
 *  トリは，キューヘッダとする．空のキューは，次エントリ，前エントリと
 *  も自分自身を指すキューヘッダであらわす．
 */

#ifndef	TOPPERS_QUEUE_H
#define	TOPPERS_QUEUE_H

#ifdef __cplusplus
extern "C" {
#endif

/*
 *  キューのデータ構造の定義
 */
typedef struct queue {
	struct queue *p_next;		/* 次エントリへのポインタ */
	struct queue *p_prev;		/* 前エントリへのポインタ */
} QUEUE;

/*
 *  キューの初期化
 *
 *  p_queueにはキューヘッダを指定する．
 */
Inline void
queue_initialize(QUEUE *p_queue)
{
	p_queue->p_prev = p_queue;
	p_queue->p_next = p_queue;
}

/*
 *  キューの前エントリへの挿入
 *
 *  p_queueの前にp_entryを挿入する．p_queueにキューヘッダを指定した場
 *  合には，キューの末尾にp_entryを挿入することになる．
 */
Inline void
queue_insert_prev(QUEUE *p_queue, QUEUE *p_entry)
{
	p_entry->p_prev = p_queue->p_prev;
	p_entry->p_next = p_queue;
	p_queue->p_prev->p_next = p_entry;
	p_queue->p_prev = p_entry;
}

/*
 *  キューの次エントリへの挿入
 *
 *  p_queueの次にp_entryを挿入する．p_queueにキューヘッダを指定した場
 *  合には，キューの先頭にp_entryを挿入することになる．
 */
Inline void
queue_insert_next(QUEUE *p_queue, QUEUE *p_entry)
{
	p_entry->p_prev = p_queue;
	p_entry->p_next = p_queue->p_next;
	p_queue->p_next->p_prev = p_entry;
	p_queue->p_next = p_entry;
}

/*
 *  エントリの削除
 *
 *  p_entryをキューから削除する．
 */
Inline void
queue_delete(QUEUE *p_entry)
{
	p_entry->p_prev->p_next = p_entry->p_next;
	p_entry->p_next->p_prev = p_entry->p_prev;
}

/*
 *  キューの列挙
 *
 *  引数p_topをキューとするエントリを先頭から列挙する。
 *  最初の呼出しでは、p_entryにNULLを渡す。返値はp_topの次のエントリ(つまり先頭)である。
 *  次回以降は前回に得たエントリをp_entryに渡す。返値はp_entryの次のエントリとなる。
 *  p_entryの次のエントリがp_topだったとき、NULLを返値として、キューの終端をあらわす。
 *  p_topのNULLチェックやp_entryの妥当性検証は行っていない。呼出し側の責任で行うこと。
 *  また、言うまでもないが、スレッドセーフにはなりえない。列挙ループ中は排他は
 *  呼出し側の責任で行うこと。
 */
Inline QUEUE *
queue_enumerate(QUEUE *p_top, QUEUE *p_entry)
{
	QUEUE *p_result;
	if (p_entry == NULL) {
		p_result = p_top->p_next;
	} else {
		p_result = p_entry->p_next;
	}
	if (p_result == p_top) {
		return NULL;
	}
	return p_result;
}

/*
 *  キューの次エントリの取出し
 *
 *  p_queueの次エントリをキューから削除し，削除したエントリを返す．
 *  p_queueにキューヘッダを指定した場合には，キューの先頭のエントリを
 *  取り出すことになる．p_queueに空のキューを指定して呼び出してはなら
 *  ない．
 */
Inline QUEUE *
queue_delete_next(QUEUE *p_queue)
{
	QUEUE	*p_entry;

	assert(p_queue->p_next != p_queue);
	p_entry = p_queue->p_next;
	p_queue->p_next = p_entry->p_next;
	p_entry->p_next->p_prev = p_queue;
	return(p_entry);
}

/*
 *  キューの次エントリのポインタを取得
 *
 *  p_queue の次エントリを返す．p_queue にキューヘッダを指定した場合には，
 *  キューの先頭のエントリを取り出すことになる．p_queue に空のキューを
 *  指定して呼び出してはならない．
 */
Inline QUEUE *
queue_peek_next(QUEUE *p_queue)
{
	assert(p_queue->p_next != p_queue);

	return(p_queue->p_next);
}

/*
 *  キューが空かどうかのチェック
 *
 *  p_queueにはキューヘッダを指定する．
 */
Inline bool
queue_empty(QUEUE *p_queue)
{
	if (p_queue->p_next == p_queue) {
		assert(p_queue->p_prev == p_queue);
		return(true);
	}
	return(false);
}

#ifdef __cplusplus
}
#endif

#endif /* TOPPERS_QUEUE_H */
