/*-------------------------------------------------------------------------
 * Copyright (c) Microsoft Corporation.  All rights reserved.
 *
 * include/commands/retryable_writes.h
 *
 * 可重写操作的通用声明
 * 定义了与可重试写操作相关的函数和数据结构
 *
 *-------------------------------------------------------------------------
 */

#ifndef RETRYABLE_WRITES_H
#define RETRYABLE_WRITES_H


/*
 * 可重试写入结果结构体
 * 存储可重试写入操作的结果信息，以便在重试时返回相同的结果
 */
typedef struct RetryableWriteResult
{
	/* 被插入、更新或删除的对象 ID */
	pgbson *objectId;

	/* 写操作是否影响了行 */
	bool rowsAffected;

	/* 写入操作的分片键值 */
	int64 shardKeyValue;

	/*
	 * （可能已被投影的）旧文档或新文档的值，下次试验应该报告这个值
	 * （如果命令不适用或命令无法匹配任何文档，则为 NULL）
	 */
	pgbson *resultDocument;
} RetryableWriteResult;


/* 在任何分片中查找重试记录的函数
 * collectionId: 集合 ID
 * transactionId: 事务 ID
 * writeResult: 写入结果结构体（用于返回查询结果）
 * 返回：是否找到重试记录
 */
bool FindRetryRecordInAnyShard(uint64 collectionId, text *transactionId,
							   RetryableWriteResult *writeResult);
/* 删除重试记录的函数
 * collectionId: 集合 ID
 * shardKeyValue: 分片键值
 * transactionId: 事务 ID
 * writeResult: 写入结果结构体（用于返回删除的记录）
 * 返回：删除是否成功
 */
bool DeleteRetryRecord(uint64 collectionId, int64 shardKeyValue,
					   text *transactionId, RetryableWriteResult *writeResult);
/* 插入重试记录的函数
 * collectionId: 集合 ID
 * shardKeyValue: 分片键值
 * transactionId: 事务 ID
 * objectId: 对象 ID
 * rowsAffected: 是否影响了行
 * resultDocument: 结果文档
 */
void InsertRetryRecord(uint64 collectionId, int64 shardKeyValue,
					   text *transactionId, pgbson *objectId, bool rowsAffected,
					   pgbson *resultDocument);

#endif
