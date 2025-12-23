/*-------------------------------------------------------------------------
 * Copyright (c) Microsoft Corporation.  All rights reserved.
 *
 * include/commands/commands_common.h
 *
 * Common declarations of commands.
 * 命令的通用声明
 *
 *-------------------------------------------------------------------------
 */

#ifndef COMMANDS_COMMON_H
#define COMMANDS_COMMON_H

#include <utils/elog.h>
#include <metadata/collection.h>
#include <io/bson_core.h>
#include <utils/documentdb_errors.h>
#include <access/xact.h>
#include <access/xlog.h>

/*
 * Maximum size of a output bson document is 16MB.
 * 输出 BSON 文档的最大大小为 16MB
 * 这是 MongoDB BSON 文档的标准大小限制
 */
#define BSON_MAX_ALLOWED_SIZE (16 * 1024 * 1024)

/*
 * Maximum size of a document produced by an intermediate stage of an aggregation pipeline.
 * 聚合管道中间阶段产生的文档的最大大小
 * For example, in a pipeline like [$facet, $unwind], $facet is allowed to generate a document
 * 例如，在 [$facet, $unwind] 管道中，$facet 被允许生成一个
 * larger than 16MB, since $unwind can break it into smaller documents. However, $facet cannot
 * 大于 16MB 的文档，因为 $unwind 可以将其分解为较小的文档。但是，$facet 不能
 * generate a document larger than 100MB.
 * 生成大于 100MB 的文档
 */
#define BSON_MAX_ALLOWED_SIZE_INTERMEDIATE (100 * 1024 * 1024)

/* StringView that represents the _id field */
/* 表示 _id 字段的 StringView */
extern PGDLLIMPORT const StringView IdFieldStringView;
// 全局常量：表示 _id 字段名的字符串视图


/*
 * ApiGucPrefix.enable_create_collection_on_insert GUC determines whether
 * GUC（Grand Unified Configuration，PostgreSQL 的配置系统）决定
 * an insert into a non-existent collection should create a collection.
 * 插入到不存在的集合时是否应创建集合
 */
extern bool EnableCreateCollectionOnInsert;

/*
 * Whether or not write operations are inlined or if they are dispatched
 * 写操作是否内联执行或分派
 * to a remote shard. For single node scenarios like DocumentDB that don't need
 * 到远程分片。对于像 DocumentDB 这样不需要的单一节点场景
 * distributed dispatch. Reset in scenarios that need distributed dispatch.
 * 分布式分派。在需要分布式分派的场景中重置
 */
extern bool DefaultInlineWriteOperations;
extern int BatchWriteSubTransactionCount;
extern int MaxWriteBatchSize;
// 全局配置变量：控制批量写入的行为

/*
 * WriteError can be part of the response of a batch write operation.
 * WriteError 可以作为批量写操作响应的一部分
 * 当批量写操作中部分操作失败时，会返回包含 WriteError 的响应
 */
typedef struct WriteError
{
	/* Index specified within a write operation batch */
	/* 写操作批次中指定的索引 */
	int index;

	/* error code */
	/* 错误代码 */
	int code;

	/* description of the error */
	/* 错误描述 */
	char *errmsg;
} WriteError;


// 查找文档 ID 的分片键值
// 在分布式场景中，根据文档的 _id 确定其所属的分片
bool FindShardKeyValueForDocumentId(MongoCollection *collection, const
									bson_value_t *queryDoc,
									bson_value_t *objectId,
									bool isIdValueCollationAware,
									bool queryHasNonIdFilters,
									int64 *shardKeyValue,
									const bson_value_t *variableSpec,
									const char *collationString);

// 判断字段是否为通用规范中被忽略的字段
bool IsCommonSpecIgnoredField(const char *fieldName);

// 从错误数据中获取写错误
WriteError * GetWriteErrorFromErrorData(ErrorData *errorData, int writeErrorIdx);

// 尝试从错误数据中获取错误消息和代码
bool TryGetErrorMessageAndCode(ErrorData *errorData, int *code, char **errmessage);

// 从查询文档值中获取 ObjectId 过滤器
pgbson * GetObjectIdFilterFromQueryDocumentValue(const bson_value_t *queryDoc,
												 bool *hasNonIdFields,
												 bool *isObjectIdFilter);

// 从查询文档中获取 ObjectId 过滤器
pgbson * GetObjectIdFilterFromQueryDocument(pgbson *queryDoc, bool *hasNonIdFields,
											bool *isIdValueCollationAware);


// 重写文档，添加 ObjectId
pgbson * RewriteDocumentAddObjectId(pgbson *document);

// 重写文档值，添加 ObjectId
pgbson * RewriteDocumentValueAddObjectId(const bson_value_t *value);

// 使用自定义 ObjectId 重写文档
pgbson * RewriteDocumentWithCustomObjectId(pgbson *document,
										   pgbson *objectIdToWrite);

// 验证 _id 字段的有效性
void ValidateIdField(const bson_value_t *idValue);

// 设置显式语句超时
void SetExplicitStatementTimeout(int timeoutMilliseconds);

// 提交写入过程并重新获取集合锁
void CommitWriteProcedureAndReacquireCollectionLock(MongoCollection *collection,
													Oid shardTableOid,
													bool setSnapshot);

extern bool SimulateRecoveryState;
extern bool DocumentDBPGReadOnlyForDiskFull;

// 如果服务器或事务为只读，则抛出错误
inline static void
ThrowIfServerOrTransactionReadOnly(void)
{
	if (!XactReadOnly)
	{
		return;
	}

	if (RecoveryInProgress() || SimulateRecoveryState)
	{
		/*
		 * Skip these checks in recovery mode - let the system throw the appropriate
		 * 在恢复模式中跳过这些检查 - 让系统抛出适当的
		 * error.
		 * 错误
		 */
		return;
	}

	if (DocumentDBPGReadOnlyForDiskFull)
	{
		ereport(ERROR, (errcode(ERRCODE_DISK_FULL), errmsg(
							"Can't execute write operation, The database disk is full")));
	}

	/* Error is coming because the server has been put in a read-only state, but we're a writable node (primary) */
	/* 错误是由于服务器已置于只读状态，但我们是可写节点（主节点） */
	if (DefaultXactReadOnly)
	{
		ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_NOTWRITABLEPRIMARY),
						errmsg(
							"Write operations cannot be performed because the server is currently operating in a read-only mode."),
						errdetail("the default transaction is read-only"),
						errdetail_log(
							"cannot execute write operations when default_transaction_read_only is set to true")));
	}

	/* Error is coming because the transaction has been in a readonly state */
	/* 错误是由于事务处于只读状态 */
	ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_OPERATIONNOTSUPPORTEDINTRANSACTION),
					errmsg(
						"cannot execute write operation when the transaction is in a read-only state."),
					errdetail("the current transaction is read-only")));
}


#endif
