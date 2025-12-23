/*-------------------------------------------------------------------------
 * Copyright (c) Microsoft Corporation.  All rights reserved.
 *
 * src/distribution/distributed_index_operations.c
 *
 * Implementation of index operations for a distributed execution.
 *-------------------------------------------------------------------------
 * 分布式索引操作实现
 *
 * 本文件实现了 DocumentDB 在分布式环境下的索引操作功能。
 *
 * 主要功能：
 * 1. 索引元数据更新：在所有分片上更新索引的元数据（如就绪状态）
 * 2. 分布式索引更新：协调在各个工作节点上执行索引更新操作
 * 3. 工作节点函数：在工作节点上执行的索引更新函数
 *
 * 核心概念：
 * - 索引元数据：存储在 collection_indexes 表中的索引状态信息
 * - 就绪状态（ready）：标记索引是否可用于查询
 * - 稀疏索引（sparse）：标记索引是否只包含具有索引字段的文档
 * - TTL 索引：带有过期时间的索引
 * - 分布式协调：通过 run_command 在所有工作节点上执行操作
 *
 * 工作流程：
 * 1. UpdateDistributedPostgresIndex 在协调器上调用
 * 2. 构建包含参数的 BSON 文档
 * 3. 通过 ExecutePerNodeCommand 将命令发送到所有工作节点
 * 4. 每个工作节点执行 update_postgres_index_worker 函数
 * 5. worker 函数调用 UpdatePostgresIndexCore 更新本地索引元数据
 *-------------------------------------------------------------------------
 */

#include <postgres.h>

#include "io/bson_core.h"
#include "metadata/collection.h"
#include "utils/documentdb_errors.h"
#include "node_distributed_operations.h"
#include "distributed_index_operations.h"
#include "commands/coll_mod.h"
#include "parser/parse_func.h"
#include "metadata/metadata_cache.h"

/* 获取 update_postgres_index_worker 函数的 OID */
static Oid UpdatePostgresIndexWorkerFunctionOid(void);

extern char *ApiDataSchemaName;
extern char *ApiDistributedSchemaNameV2;

PG_FUNCTION_INFO_V1(documentdb_update_postgres_index_worker);


/*
 * documentdb_update_postgres_index_worker
 * -----
 * 工作节点索引更新函数
 *
 * 此函数在工作节点上执行，用于更新本地分片的索引元数据。
 * 由 UpdateDistributedPostgresIndex 通过分布式命令调用。
 *
 * 参数（BSON 格式）：
 * - collectionId (int64): 集合 ID
 * - indexId (int32): 索引 ID
 * - operation (int32): 操作类型（枚举 IndexMetadataUpdateOperation）
 * - value (bool): 要设置的值
 *
 * 操作类型：
 * - INDEX_METADATA_UPDATE_OPERATION_READY: 更新索引就绪状态
 * - INDEX_METADATA_UPDATE_OPERATION_SPARSE: 更新稀疏索引标记
 * - INDEX_METADATA_UPDATE_OPERATION_TTL: 更新 TTL 索引标记
 *
 * 返回值：空的 BSON 文档
 *
 * 注意：此函数使用 ignoreMissingShards = true，这意味着如果某个分片不存在，
 *       不会报错。这适用于添加节点的场景。
 */
Datum
documentdb_update_postgres_index_worker(PG_FUNCTION_ARGS)
{
	pgbson *argBson = PG_GETARG_PGBSON(0);

	IndexMetadataUpdateOperation operation = INDEX_METADATA_UPDATE_OPERATION_UNKNOWN;
	uint64_t collectionId = 0;
	int indexId = 0;
	bool value = false;
	bool hasValue = false;
	bson_iter_t argIter;
	PgbsonInitIterator(argBson, &argIter);
	while (bson_iter_next(&argIter))
	{
		const char *key = bson_iter_key(&argIter);
		if (strcmp(key, "collectionId") == 0)
		{
			collectionId = bson_iter_as_int64(&argIter);
		}
		else if (strcmp(key, "indexId") == 0)
		{
			indexId = bson_iter_int32(&argIter);
		}
		else if (strcmp(key, "operation") == 0)
		{
			operation = (IndexMetadataUpdateOperation) bson_iter_int32(&argIter);
		}
		else if (strcmp(key, "value") == 0)
		{
			value = bson_iter_as_bool(&argIter);
			hasValue = true;
		}
		else
		{
			ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_INTERNALERROR),
							errmsg(
								"Unexpected argument to update_postgres_index_worker: %s",
								key)));
		}
	}

	if (collectionId == 0 || indexId == 0 || !hasValue ||
		operation == INDEX_METADATA_UPDATE_OPERATION_UNKNOWN)
	{
		ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_INTERNALERROR),
						errmsg("Missing argument to update_postgres_index_worker")));
	}

	/* 忽略缺失的分片，适用于添加节点的场景 */
	bool ignoreMissingShards = true;
	UpdatePostgresIndexCore(collectionId, indexId, operation, value, ignoreMissingShards);

	PG_RETURN_POINTER(PgbsonInitEmpty());
}


/*
 * UpdateDistributedPostgresIndex
 * -----
 * 更新分布式索引元数据
 *
 * 此函数在协调器上调用，负责在所有工作节点上更新索引的元数据。
 * 通过 ExecutePerNodeCommand 将更新命令发送到托管集合分片的所有节点。
 *
 * 参数：
 * - collectionId: 要更新的集合 ID
 * - indexId: 要更新的索引 ID
 * - operation: 操作类型（就绪/稀疏/TTL）
 * - value: 要设置的值
 *
 * 工作流程：
 * 1. 构建包含参数的 BSON 文档
 * 2. 查找集合的表名
 * 3. 调用 ExecutePerNodeCommand 在所有相关节点上执行 worker 函数
 * 4. backfillCoordinator = true 确保协调器上的分片也被更新
 */
void
UpdateDistributedPostgresIndex(uint64_t collectionId, int indexId, int operation,
							   bool value)
{
	pgbson_writer writer;
	PgbsonWriterInit(&writer);
	PgbsonWriterAppendInt64(&writer, "collectionId", 12, collectionId);
	PgbsonWriterAppendInt32(&writer, "indexId", 7, indexId);
	PgbsonWriterAppendInt32(&writer, "operation", 9, operation);
	PgbsonWriterAppendBool(&writer, "value", 5, value);

	MongoCollection *collection = GetMongoCollectionByColId(collectionId, NoLock);
	if (collection == NULL)
	{
		ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_INVALIDNAMESPACE),
						errmsg("Failed to find collection for index update")));
	}

	char fullyQualifiedTableName[NAMEDATALEN * 2 + 2] = { 0 };
	pg_sprintf(fullyQualifiedTableName, "%s.%s", ApiDataSchemaName,
			   collection->tableName);

	bool backfillCoordinator = true;
	ExecutePerNodeCommand(UpdatePostgresIndexWorkerFunctionOid(), PgbsonWriterGetPgbson(
							  &writer),
						  false, fullyQualifiedTableName, backfillCoordinator);
}


/*
 * Returns the OID of the update_postgres_index_worker function.
 * it isn't really worth caching this since it's only used in the diagnostic path.
 * If that changes, this can be put into an OID cache of sorts.
 * -----
 * 获取 update_postgres_index_worker 函数的 OID
 *
 * 查找并返回 worker 函数的 OID，用于 ExecutePerNodeCommand。
 *
 * 注意：由于此函数仅在诊断路径中使用，不需要缓存 OID。
 *       如果使用频率增加，可以考虑添加 OID 缓存。
 */
static Oid
UpdatePostgresIndexWorkerFunctionOid(void)
{
	List *functionNameList = list_make2(makeString(ApiDistributedSchemaNameV2),
										makeString("update_postgres_index_worker"));
	Oid paramOids[1] = { DocumentDBCoreBsonTypeId() };
	bool missingOK = false;

	return LookupFuncName(functionNameList, 1, paramOids, missingOK);
}
