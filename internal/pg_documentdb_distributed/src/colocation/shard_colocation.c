/*-------------------------------------------------------------------------
 * Copyright (c) Microsoft Corporation.  All rights reserved.
 *
 * src/colocation/shard_colocation.c
 *
 * Implementation of colocation and distributed placement for the extension.
 *-------------------------------------------------------------------------
 * 分片共置和分布式放置的实现
 *
 * 本文件实现了 DocumentDB 在分布式环境（基于 Citus）下的分片共置和分布式放置功能。
 *
 * 主要功能：
 * 1. 分片映射管理：获取集群中的节点信息，生成 MongoDB 兼容的分片映射
 * 2. 集合共置配置：控制不同集合的数据是否放置在相同的物理分片上
 * 3. 分片移动：将集合从一个分片移动到另一个分片
 * 4. 分布式查询重写：将 MongoDB 的元数据查询转换为 Citus 分布式查询
 *
 * 核心概念：
 * - 共置（Colocation）：多个集合共享相同的物理分片，减少跨节点查询
 * - 分片（Shard）：数据分片的基本单位，每个分片对应一个 groupId
 * - 节点（Node）：集群中的物理服务器，每个节点可以托管多个分片
 *-------------------------------------------------------------------------
 */

#include <postgres.h>
#include <utils/builtins.h>
#include <parser/parse_node.h>
#include <parser/parse_relation.h>
#include <catalog/namespace.h>
#include <nodes/makefuncs.h>
#include <catalog/pg_collation.h>
#include <utils/fmgroids.h>
#include <utils/version_utils.h>
#include <utils/inval.h>
#include <parser/parse_func.h>

#include "io/bson_core.h"
#include "utils/documentdb_errors.h"
#include "utils/query_utils.h"
#include "sharding/sharding.h"
#include "commands/commands_common.h"
#include "commands/parse_error.h"

#include "metadata/metadata_cache.h"
#include "metadata/collection.h"
#include "shard_colocation.h"
#include "api_hooks_def.h"
#include "aggregation/bson_aggregation_pipeline.h"
#include "aggregation/bson_aggregation_pipeline_private.h"


extern bool EnableMoveCollection;

PG_FUNCTION_INFO_V1(command_get_shard_map);
PG_FUNCTION_INFO_V1(command_list_shards);
PG_FUNCTION_INFO_V1(documentdb_command_move_collection);


/*
 * Metadata collected about Citus nodes.
 * This is the output that we care about collected from
 * Citus's pg_dist_node.
 * -----
 * 从 Citus 节点收集的元数据结构
 * 这些信息从 Citus 的 pg_dist_node 系统表中收集而来
 * 用于构建 MongoDB 兼容的分片映射
 */
typedef struct NodeInfo
{
	/*
	 * The groupId for the node. This has 1 per
	 * server (Coordinator, Worker1, Worker2)
	 * -----
	 * 节点的组 ID，每个服务器（协调器、工作节点1、工作节点2）都有一个唯一的 groupId
	 * 同一个 groupId 下的节点托管相同的数据分片
	 */
	int32_t groupId;

	/*
	 * An id for the specific node
	 * -----
	 * 特定节点的唯一标识符
	 */
	int32_t nodeId;

	/*
	 * The citus role for the node, be it
	 * Primary or secondary (for replicas)
	 * -----
	 * 节点的 Citus 角色，可能是 primary（主节点）或 secondary（副本节点）
	 */
	const char *nodeRole;

	/*
	 * The name of the cluster: Default for writes
	 * and the clustername for reads.
	 * -----
	 * 集群名称：写入操作使用 "Default"，读取操作使用实际的集群名称
	 */
	const char *nodeCluster;

	/*
	 * Whether or not th enode is active (false in
	 * the case it's in the middle of an addnode)
	 * -----
	 * 节点是否处于活跃状态
	 * 在添加节点的过程中，节点可能处于非活跃状态
	 */
	bool isactive;

	/*
	 * The formatted node name
	 * uses node_<clusterName>_<nodeId>
	 * -----
	 * 格式化的节点名称，格式为 node_<集群名称>_<节点ID>
	 * 这是 MongoDB 兼容的节点名称格式
	 */
	const char *mongoNodeName;

	/*
	 * The logical shard for the node "shard_<groupId>"
	 * -----
	 * 节点的逻辑分片名称，格式为 shard_<groupId>
	 * 每个 groupId 对应一个逻辑分片
	 */
	const char *mongoShardName;
} NodeInfo;


/*
 * Static function declarations
 * -----
 * 静态函数声明
 */

/* 将两个未分片的 Citus 表进行共置 */
static const char * ColocateUnshardedCitusTables(const char *sourceTableName,
												 const char *colocateWithTableName);
/* 获取分布式表的分片数量 */
static int GetShardCountForDistributedTable(Oid relationId);

/* 获取表的共置 ID */
static int GetColocationForTable(Oid tableOid, const char *collectionName,
								 const char *tableName);

/* 将已分片的 Citus 表与 "none" 进行共置（移除共置） */
static void ColocateShardedCitusTablesWithNone(const char *sourceTableName);
/* 将未分片的 Citus 表与 "none" 进行共置（移除共置） */
static void ColocateUnshardedCitusTablesWithNone(const char *sourceTableName);
/* 将单个分片移动到目标分布式表 */
static void MoveShardToDistributedTable(const char *postgresTableToMove, const
										char *targetShardTable);
/* 取消表的分布式配置并重新配置 */
static void UndistributeAndRedistributeTable(const char *postgresTable, const
											 char *colocateWith,
											 const char *shardKeyValue);

/* Handle a colocation scenario for collMod */
/* 处理 collMod 命令中的共置配置场景 */
static void HandleDistributedColocation(MongoCollection *collection,
										const bson_value_t *colocationValue);
/* 重写 listCollections 查询以支持分布式环境 */
static Query * RewriteListCollectionsQueryForDistribution(Query *source);
/* 重写 config.shards 查询以支持分布式环境 */
static Query * RewriteConfigShardsQueryForDistribution(Query *source);
/* 重写 config.chunks 查询以支持分布式环境 */
static Query * RewriteConfigChunksQueryForDistribution(Query *source);

/* 获取集群中的分片映射节点列表 */
static List * GetShardMapNodes(void);
/* 将分片映射信息写入 BSON writer */
static void WriteShardMap(pgbson_writer *writer, List *groupNodes);
/* 将分片列表信息写入 BSON writer */
static void WriteShardList(pgbson_writer *writer, List *groupNodes);

/*
 * Implements the getShardMap command
 * -----
 * 实现 getShardMap 命令
 *
 * 此函数实现 MongoDB 的 getShardMap 命令，返回集群的分片映射信息。
 * 返回格式包含：
 * - map: 分片到节点的映射
 * - hosts: 主机列表
 * - nodes: 节点详细信息
 *
 * 与 MongoDB 兼容的返回格式：
 * {
 *   "map": {
 *     "shard_0": "node_default_0,node_default_1",
 *     "shard_1": "node_default_2"
 *   },
 *   "hosts": {
 *     "node_default_0": "shard_0",
 *     "node_default_1": "shard_0"
 *   },
 *   "nodes": {
 *     "node_default_0": { "role": "primary", "active": true, "cluster": "default" }
 *   }
 * }
 */
Datum
command_get_shard_map(PG_FUNCTION_ARGS)
{
	/* First query pg_dist_node to get the set of nodes in the cluster */
	/* 首先查询 pg_dist_node 获取集群中的节点集合 */
	pgbson_writer writer;
	PgbsonWriterInit(&writer);

	List *groupNodes = GetShardMapNodes();
	if (groupNodes != NIL)
	{
		WriteShardMap(&writer, groupNodes);
	}


	PgbsonWriterAppendDouble(&writer, "ok", 2, 1);
	PG_RETURN_POINTER(PgbsonWriterGetPgbson(&writer));
}


/*
 * Implements the listShards command
 * -----
 * 实现 listShards 命令
 *
 * 此函数返回集群中的分片列表，包含每个分片的节点信息。
 * 返回格式：
 * {
 *   "shards": [
 *     { "_id": "shard_0", "nodes": "node_default_0,node_default_1" }
 *   ]
 * }
 */
Datum
command_list_shards(PG_FUNCTION_ARGS)
{
	pgbson_writer writer;
	PgbsonWriterInit(&writer);

	List *groupNodes = GetShardMapNodes();
	if (groupNodes != NIL)
	{
		WriteShardList(&writer, groupNodes);
	}


	PgbsonWriterAppendDouble(&writer, "ok", 2, 1);
	PG_RETURN_POINTER(PgbsonWriterGetPgbson(&writer));
}


/*
 * Implements the moveCollection command
 * -----
 * 实现 moveCollection 命令
 *
 * 此函数实现 MongoDB 的 moveCollection 命令，将一个未分片的集合从一个分片移动到另一个分片。
 *
 * 参数：
 * - moveCollection: 要移动的集合命名空间（db.collection）
 * - toShard: 目标分片名称（格式：shard_<groupId>）
 * - useLogicalReplication: 是否使用逻辑复制（可选，默认使用 block_writes 模式）
 *
 * 流程：
 * 1. 验证目标分片的有效性
 * 2. 检查集合是否为未分片集合
 * 3. 解除集合的当前共置关系
 * 4. 将重试表与主表重新共置
 * 5. 调用 Citus 的 move_shard_placement 函数移动分片
 *
 * 限制：
 * - 只能移动未分片的集合
 * - 需要通过 EnableMoveCollection 配置启用
 */
Datum
documentdb_command_move_collection(PG_FUNCTION_ARGS)
{
	pgbson *moveSpec = PG_GETARG_PGBSON(0);

	if (!EnableMoveCollection)
	{
		ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_COMMANDNOTSUPPORTED),
						errmsg("moveCollection is not supported yet")));
	}

	bson_iter_t moveSpecIter;
	PgbsonInitIterator(moveSpec, &moveSpecIter);

	char *moveCollectionNamespace = NULL;
	char *toShard = NULL;
	bool useLogicalReplication = false;
	while (bson_iter_next(&moveSpecIter))
	{
		const char *key = bson_iter_key(&moveSpecIter);
		if (strcmp(key, "moveCollection") == 0)
		{
			EnsureTopLevelFieldType("moveCollection", &moveSpecIter, BSON_TYPE_UTF8);
			const bson_value_t *value = bson_iter_value(&moveSpecIter);
			moveCollectionNamespace = pnstrdup(value->value.v_utf8.str,
											   value->value.v_utf8.len);
		}
		else if (strcmp(key, "toShard") == 0)
		{
			EnsureTopLevelFieldType("toShard", &moveSpecIter, BSON_TYPE_UTF8);
			const bson_value_t *value = bson_iter_value(&moveSpecIter);
			toShard = pnstrdup(value->value.v_utf8.str, value->value.v_utf8.len);
		}
		else if (strcmp(key, "useLogicalReplication") == 0)
		{
			EnsureTopLevelFieldIsBooleanLike("useLogicalReplication", &moveSpecIter);
			useLogicalReplication = bson_iter_as_bool(&moveSpecIter);
		}
		else if (!IsCommonSpecIgnoredField(key))
		{
			ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_FAILEDTOPARSE),
							errmsg("Unknown top level field %s in moveCollection spec",
								   key)));
		}
	}

	if (moveCollectionNamespace == NULL || toShard == NULL)
	{
		ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_FAILEDTOPARSE),
						errmsg(
							"Required fields moveCollection and toShard not specified")));
	}

	/* First validate shardId: We do this so we do up front validations before we start checking
	 * the collection.
	 */
	if (strncmp(toShard, "shard_", 6) != 0 || strlen(toShard) < 7)
	{
		ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_INVALIDOPTIONS),
						errmsg("Invalid shard provided %s", toShard)));
	}

	char *startOfGroupId = &toShard[6];
	char *endPointer = NULL;
	int32_t groupId = strtol(startOfGroupId, &endPointer, 10);

	if (endPointer == startOfGroupId)
	{
		/* No valid conversion of the integer */
		ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_INVALIDOPTIONS),
						errmsg("Invalid shard provided %s", toShard)));
	}

	/* Once we have the groupId ensure that the groupId formatted our way ends up to the original string */
	char *formattedShard = psprintf("shard_%d", groupId);
	if (strcmp(formattedShard, toShard) != 0)
	{
		ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_INVALIDOPTIONS),
						errmsg("Invalid shard provided %s", toShard)));
	}

	/* Now query pg_dist_node to ensure it's a valid group id */
	int numArgs = 1;
	Oid argOids[1] = { INT4OID };
	Datum argDatums[1] = { Int32GetDatum(groupId) };

	Datum resultDatums[3] = { 0 };
	bool resultNulls[3] = { 0 };
	ExtensionExecuteMultiValueQueryWithArgsViaSPI(
		"SELECT nodename, nodeport FROM pg_dist_node WHERE noderole = 'primary' AND isactive AND groupid = $1",
		numArgs, argOids, argDatums, NULL, true, SPI_OK_SELECT, resultDatums, resultNulls,
		2);
	if (resultNulls[0] || resultNulls[1])
	{
		ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_INVALIDOPTIONS),
						errmsg("Cound not find shard provided in metadata: %s",
							   toShard)));
	}

	const char *toShardName = TextDatumGetCString(resultDatums[0]);
	int toNodePort = DatumGetInt32(resultDatums[1]);

	char *databaseName = NULL;
	char *collectionName = NULL;
	ParseNamespaceName(moveCollectionNamespace, &databaseName, &collectionName);

	MongoCollection *collection = GetMongoCollectionByNameDatum(
		CStringGetTextDatum(databaseName), CStringGetTextDatum(collectionName),
		NoLock);
	if (collection == NULL)
	{
		ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_NAMESPACENOTFOUND),
						errmsg("Namespace %s not found", moveCollectionNamespace)));
	}

	if (collection->shardKey != NULL)
	{
		ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_INVALIDOPTIONS),
						errmsg("Cannot call moveCollection on a sharded collection")));
	}

	/* So now we know this collection is valid and is unsharded - check the shardId */
	argOids[0] = OIDOID;
	argDatums[0] = ObjectIdGetDatum(collection->relationId);
	ExtensionExecuteMultiValueQueryWithArgsViaSPI(
		"SELECT nodename, nodeport, ps.shardid FROM pg_dist_shard ps JOIN pg_dist_shard_placement pp ON ps.shardid = pp.shardid "
		" WHERE logicalrelid = $1 AND shardminvalue IS NULL AND shardmaxvalue IS NULL",
		numArgs, argOids, argDatums, NULL, true, SPI_OK_SELECT, resultDatums, resultNulls,
		3);
	if (resultNulls[0] || resultNulls[1] || resultNulls[2])
	{
		ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_INVALIDOPTIONS),
						errmsg(
							"Cound not find shard information for collection in metadata: %s",
							moveCollectionNamespace)));
	}

	const char *fromShardName = TextDatumGetCString(resultDatums[0]);
	int fromNodePort = DatumGetInt32(resultDatums[1]);
	int shardId = DatumGetInt32(resultDatums[2]);

	/* First break any colocation on this table */
	StringInfoData tableNameData;
	initStringInfo(&tableNameData);
	appendStringInfo(&tableNameData, "%s.%s", ApiDataSchemaName, collection->tableName);
	const char *updateColocationQuery =
		"SELECT update_distributed_table_colocation($1, colocate_with => 'none')";
	bool resultNullIgnore = false;
	argOids[0] = TEXTOID;
	argDatums[0] = CStringGetTextDatum(tableNameData.data);
	ExtensionExecuteQueryWithArgsViaSPI(updateColocationQuery, 1,
										argOids, argDatums, NULL, false, SPI_OK_SELECT,
										&resultNullIgnore);

	/* Next re-colocate the retry table with the main table */
	const char *retryTableUpdateColocationQuery =
		"SELECT update_distributed_table_colocation($1, colocate_with => $2)";
	StringInfoData retryTableNameData;
	initStringInfo(&retryTableNameData);
	appendStringInfo(&retryTableNameData, "%s.retry_%ld", ApiDataSchemaName,
					 collection->collectionId);

	Oid retryArgOid[2] = { TEXTOID, TEXTOID };
	Datum retryArgDatums[2] = {
		CStringGetTextDatum(retryTableNameData.data), CStringGetTextDatum(
			tableNameData.data)
	};
	ExtensionExecuteQueryWithArgsViaSPI(retryTableUpdateColocationQuery, 2,
										retryArgOid, retryArgDatums, NULL, false,
										SPI_OK_SELECT,
										&resultNullIgnore);

	/* Now move the shard to the target group */
	const char *shardTransferMode = useLogicalReplication ? "force_logical" :
									"block_writes";
	elog(LOG, "Moving collection from %s:%d to %s:%d", fromShardName, fromNodePort,
		 toShardName, toNodePort);
	int moveShardNumArgs = 6;
	const char *moveShardQuery =
		"SELECT citus_move_shard_placement(shard_id => $1, source_node_name => $2, source_node_port => $3, target_node_name => $4, target_node_port => $5, shard_transfer_mode => $6::citus.shard_transfer_mode)";
	Oid moveShardArgOids[6] = { INT4OID, TEXTOID, INT4OID, TEXTOID, INT4OID, TEXTOID };
	Datum moveShardArgs[6] =
	{
		Int32GetDatum(shardId), CStringGetTextDatum(fromShardName), Int32GetDatum(
			fromNodePort),
		CStringGetTextDatum(toShardName), Int32GetDatum(toNodePort), CStringGetTextDatum(
			shardTransferMode)
	};
	ExtensionExecuteQueryWithArgsViaSPI(moveShardQuery, moveShardNumArgs,
										moveShardArgOids, moveShardArgs, NULL, false,
										SPI_OK_SELECT,
										&resultNullIgnore);

	pgbsonelement okElement = { 0 };
	okElement.path = "ok";
	okElement.pathLength = 2;
	okElement.bsonValue.value_type = BSON_TYPE_DOUBLE;
	okElement.bsonValue.value.v_double = 1;

	PG_RETURN_POINTER(PgbsonElementToPgbson(&okElement));
}


/*
 * override hooks related to colocation.
 * -----
 * 注册与共置相关的钩子函数
 *
 * 此函数在模块初始化时调用，将自定义的处理函数注册到钩子中，
 * 以便在分布式环境下拦截和处理相关操作。
 */
void
UpdateColocationHooks(void)
{
	/* 注册共置处理钩子 */
	handle_colocation_hook = HandleDistributedColocation;
	/* 注册 listCollections 查询重写钩子 */
	rewrite_list_collections_query_hook = RewriteListCollectionsQueryForDistribution;
	/* 注册 config.shards 查询重写钩子 */
	rewrite_config_shards_query_hook = RewriteConfigShardsQueryForDistribution;
	/* 注册 config.chunks 查询重写钩子 */
	rewrite_config_chunks_query_hook = RewriteConfigChunksQueryForDistribution;
}


/*
 * Process colocation options for a distributed DocumentDB deployment.
 * -----
 * 处理分布式 DocumentDB 部署的共置选项
 *
 * 此函数处理 createCollection 或 collMod 命令中的共置配置。
 *
 * 共置选项格式：
 * {
 *   "colocation": {
 *     "collection": "other_collection"  // 与另一个集合共置
 *     // 或
 *     "collection": null  // 取消共置
 *   }
 * }
 *
 * 参数：
 * - collection: 要配置共置的集合
 * - colocationValue: 共置配置文档
 *
 * 共置规则：
 * 1. 已分片的集合只能与 null 共置（取消共置）
 * 2. 未分片的集合可以与另一个未分片的集合共置
 * 3. 与 changes 表共置的集合需要先取消共置
 * 4. 目标集合必须只有一个分片
 */
static void
HandleDistributedColocation(MongoCollection *collection, const
							bson_value_t *colocationValue)
{
	if (collection == NULL)
	{
		ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_INTERNALERROR),
						errmsg("unexpected - collection for colocation was null")));
	}

	if (colocationValue->value_type != BSON_TYPE_DOCUMENT)
	{
		ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_FAILEDTOPARSE),
						errmsg("Colocation options must be provided as a document.")));
	}

	char *tableWithNamespace = psprintf("%s.%s", ApiDataSchemaName,
										collection->tableName);
	bson_iter_t colocationIter;
	BsonValueInitIterator(colocationValue, &colocationIter);

	StringView collectionName = { 0 };
	bool colocateWithNull = false;
	while (bson_iter_next(&colocationIter))
	{
		const char *key = bson_iter_key(&colocationIter);

		if (strcmp(key, "collection") == 0)
		{
			if (BSON_ITER_HOLDS_UTF8(&colocationIter))
			{
				collectionName.string = bson_iter_utf8(&colocationIter,
													   &collectionName.length);
			}
			else if (BSON_ITER_HOLDS_NULL(&colocationIter))
			{
				colocateWithNull = true;
			}
			else
			{
				ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_BADVALUE),
								errmsg(
									"colocation.collection must be a string or null. not %s",
									BsonTypeName(bson_iter_type(&colocationIter))),
								errdetail_log(
									"colocation.collection must be a string or null. not %s",
									BsonTypeName(bson_iter_type(&colocationIter)))));
			}
		}
		else
		{
			ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_FAILEDTOPARSE),
							errmsg("Unrecognized field in colocation.%s", key),
							errdetail_log("Unrecognized field in colocation.%s", key)));
		}
	}

	if (collectionName.length == 0 && !colocateWithNull)
	{
		ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_INVALIDOPTIONS),
						errmsg("Must specify collection for colocation")));
	}

	/* For sharded collections, can only colocate with null: We do this to fix up old tables */
	bool isSharded = collection->shardKey != NULL;
	if (isSharded && !colocateWithNull)
	{
		ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_INVALIDOPTIONS),
						errmsg("Cannot colocate a collection that is already sharded.")));
	}

	const char *retryTableShardKeyValue = NULL;
	if (colocateWithNull)
	{
		if (isSharded)
		{
			retryTableShardKeyValue = "shard_key_value";
			ColocateShardedCitusTablesWithNone(tableWithNamespace);
		}
		else
		{
			ColocateUnshardedCitusTablesWithNone(tableWithNamespace);
		}
	}
	else
	{
		const char *targetCollectionName = CreateStringFromStringView(&collectionName);
		if (strcmp(collection->name.collectionName, targetCollectionName) == 0)
		{
			ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_INVALIDNAMESPACE),
							errmsg(
								"Source and target cannot be the same for colocation")));
		}

		MongoCollection *targetCollection = GetMongoCollectionByNameDatum(
			CStringGetTextDatum(collection->name.databaseName),
			CStringGetTextDatum(targetCollectionName),
			AccessShareLock);
		if (targetCollection == NULL)
		{
			ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_INVALIDNAMESPACE),
							errmsg("Namespace %s.%s cannot be found",
								   collection->name.databaseName,
								   targetCollectionName),
							errdetail_log("Namespace %s.%s cannot be found",
										  collection->name.databaseName,
										  targetCollectionName)));
		}

		/* Validate that targetCollection is not colocated with ApiDataSchemaName.changes
		 * (if it is, we fail until it is colocated with null) - this is a back-compat
		 * cleanup decision.
		 */
		char *targetWithNamespace = psprintf("%s.%s", ApiDataSchemaName,
											 targetCollection->tableName);
		int colocationId = GetColocationForTable(targetCollection->relationId,
												 targetCollectionName,
												 targetWithNamespace);

		/* Get the colocationId of the changes table */
		char *documentdbDataWithNamespace = psprintf("%s.changes", ApiDataSchemaName);
		RangeVar *rangeVar = makeRangeVar(ApiDataSchemaName, "changes", -1);
		Oid changesRelId = RangeVarGetRelid(rangeVar, AccessShareLock, false);

		int colocationIdOfChangesTable = GetColocationForTable(changesRelId, "changes",
															   documentdbDataWithNamespace);
		if (colocationId == colocationIdOfChangesTable)
		{
			ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_COMMANDNOTSUPPORTED),
							errmsg(
								"Colocation for this collection in the current configuration is not supported. "
								"Please first colocate %s with colocation: null",
								targetCollectionName)),
					(errdetail_log(
						 "Colocation for this table in the current configuration is not supported - legacy table")));
		}

		if (targetCollection->shardKey != NULL)
		{
			ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_COMMANDNOTSUPPORTED),
							errmsg(
								"Current collection cannot be colocated with any sharded collection.")));
		}

		/* Also check if the colocated source has only 1 shard (otherwise require colocate=null explicitly) */
		int shardCount = GetShardCountForDistributedTable(targetCollection->relationId);
		if (shardCount != 1)
		{
			ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_COMMANDNOTSUPPORTED),
							errmsg(
								"Colocation for this collection in the current configuration is not supported. "
								"Please first colocate %s with colocation: null",
								targetCollectionName)),
					(errdetail_log(
						 "Colocation for this table in the current configuration is not supported - shard count is not 1: %d",
						 shardCount)));
		}

		retryTableShardKeyValue = ColocateUnshardedCitusTables(tableWithNamespace,
															   targetWithNamespace);
	}

	/* Colocate retry with the original table */
	char *retryTableWithNamespace = psprintf("%s.retry_%ld", ApiDataSchemaName,
											 collection->collectionId);
	UndistributeAndRedistributeTable(retryTableWithNamespace, tableWithNamespace,
									 retryTableShardKeyValue);
}


/*
 * Rewrite/Update the metadata query for distributed information.
 * -----
 * 重写 listCollections 查询以支持分布式环境
 *
 * 原始查询（单机模式）：
 * SELECT bson_dollar_project(row_get_bson(collections),
 *   '{ "ns": { "$concat": [ "$database_name", ".", "$collection_name" ]}, ... }')
 * FROM ApiCatalogSchema.collections
 * WHERE database_name = 'db';
 *
 * 重写后的查询（分布式模式）：
 * SELECT bson_dollar_addfields(
 *   bson_dollar_project(row_get_bson(collections), ...),
 *   bson_repath_and_build(
 *     'shardCount', pg_dist_colocation.shard_count,
 *     'colocationId', pg_dist_partition.colocationid
 *   )
 * )
 * FROM ApiCatalogSchema.collections, pg_dist_partition, pg_dist_colocation
 * WHERE database_name = 'db'
 * AND pg_dist_partition.colocationid = pg_dist_colocation.colocationid
 * AND ('ApiDataSchema.documents_' || collection_id)::regclass = pg_dist_partition.logicalrelid;
 *
 * 作用：
 * 1. 将集合查询与 Citus 分布式元数据表关联
 * 2. 添加分片数量和共置 ID 信息到返回结果
 */
static Query *
RewriteListCollectionsQueryForDistribution(Query *source)
{
	if (list_length(source->rtable) != 1)
	{
		ereport(ERROR, (errmsg("Unexpected error - source query has more than 1 rte")));
	}

	RangeTblEntry *partitionRte = makeNode(RangeTblEntry);

	/* add pg_dist_partition to the RTEs */
	List *partitionColNames = list_concat(list_make3(makeString("logicalrelid"),
													 makeString("partmethod"), makeString(
														 "partkey")),
										  list_make3(makeString("colocationid"),
													 makeString("repmodel"), makeString(
														 "autoconverted")));

	partitionRte->rtekind = RTE_RELATION;
	partitionRte->alias = partitionRte->eref = makeAlias("partition", partitionColNames);
	partitionRte->lateral = false;
	partitionRte->inFromCl = true;
	partitionRte->relkind = RELKIND_RELATION;
	partitionRte->functions = NIL;
	partitionRte->inh = true;
	partitionRte->rellockmode = AccessShareLock;

	RangeVar *rangeVar = makeRangeVar("pg_catalog", "pg_dist_partition", -1);
	partitionRte->relid = RangeVarGetRelid(rangeVar, AccessShareLock, false);


#if PG_VERSION_NUM >= 160000
	RTEPermissionInfo *permInfo = addRTEPermissionInfo(&source->rteperminfos,
													   partitionRte);
	permInfo->requiredPerms = ACL_SELECT;
#else
	partitionRte->requiredPerms = ACL_SELECT;
#endif
	source->rtable = lappend(source->rtable, partitionRte);

	/* Add pg_dist_colocation to the RTEs */
	RangeTblEntry *colocationRte = makeNode(RangeTblEntry);
	List *colocationColNames = list_concat(list_make3(makeString("colocationid"),
													  makeString("shardcount"),
													  makeString("replicationfactor")),
										   list_make2(makeString(
														  "distributioncolumntype"),
													  makeString(
														  "distributioncolumncollation")));

	colocationRte->rtekind = RTE_RELATION;
	colocationRte->alias = colocationRte->eref = makeAlias("colocation",
														   colocationColNames);
	colocationRte->lateral = false;
	colocationRte->inFromCl = true;
	colocationRte->relkind = RELKIND_RELATION;
	colocationRte->functions = NIL;
	colocationRte->inh = true;
	colocationRte->rellockmode = AccessShareLock;

	rangeVar = makeRangeVar("pg_catalog", "pg_dist_colocation", -1);
	colocationRte->relid = RangeVarGetRelid(rangeVar, AccessShareLock, false);

#if PG_VERSION_NUM >= 160000
	permInfo = addRTEPermissionInfo(&source->rteperminfos,
									colocationRte);
	permInfo->requiredPerms = ACL_SELECT;
#else
	colocationRte->requiredPerms = ACL_SELECT;
#endif

	source->rtable = lappend(source->rtable, colocationRte);

	RangeTblRef *secondRef = makeNode(RangeTblRef);
	secondRef->rtindex = 2;
	RangeTblRef *thirdRef = makeNode(RangeTblRef);
	thirdRef->rtindex = 3;
	FromExpr *currentTree = source->jointree;
	currentTree->fromlist = lappend(currentTree->fromlist, secondRef);
	currentTree->fromlist = lappend(currentTree->fromlist, thirdRef);

	/* now add the "join" to the quals */
	List *existingQuals = make_ands_implicit((Expr *) currentTree->quals);

	/* On the collections_table we take the collection_id */
	Var *collectionIdVar = makeVar(1, 3, INT8OID, -1, InvalidOid, 0);

	char *tablePrefixString = psprintf("%s.%s", ApiDataSchemaName,
									   DOCUMENT_DATA_TABLE_NAME_PREFIX);
	Const *tablePrefix = makeConst(TEXTOID, -1, InvalidOid, -1, CStringGetTextDatum(
									   tablePrefixString), false, false);

	/* Construct the string */
	FuncExpr *concatExpr = makeFuncExpr(F_TEXTANYCAT, TEXTOID,
										list_make2(tablePrefix, collectionIdVar),
										InvalidOid, DEFAULT_COLLATION_OID,
										COERCE_EXPLICIT_CALL);

	FuncExpr *castConcatExpr = makeFuncExpr(F_TO_REGCLASS, OIDOID, list_make1(concatExpr),
											DEFAULT_COLLATION_OID, DEFAULT_COLLATION_OID,
											COERCE_EXPLICIT_CALL);

	/* Get the regclass of the join */
	Var *regclassVar = makeVar(2, 1, OIDOID, -1, InvalidOid, 0);
	FuncExpr *oidEqualFunc = makeFuncExpr(F_OIDEQ, BOOLOID, list_make2(regclassVar,
																	   castConcatExpr),
										  DEFAULT_COLLATION_OID, DEFAULT_COLLATION_OID,
										  COERCE_EXPLICIT_CALL);
	existingQuals = lappend(existingQuals, oidEqualFunc);

	List *secondJoinArgs = list_make2(
		makeVar(2, 4, INT4OID, -1, InvalidOid, 0),
		makeVar(3, 1, INT4OID, -1, InvalidOid, 0)
		);
	FuncExpr *secondJoin = makeFuncExpr(F_OIDEQ, BOOLOID, secondJoinArgs, InvalidOid,
										InvalidOid, COERCE_EXPLICIT_CALL);
	existingQuals = lappend(existingQuals, secondJoin);
	currentTree->quals = (Node *) make_ands_explicit(existingQuals);

	List *repathArgs = list_make4(
		makeConst(TEXTOID, -1, DEFAULT_COLLATION_OID, -1, CStringGetTextDatum(
					  "colocationId"), false, false),
		makeVar(2, 4, INT4OID, -1, InvalidOid, 0),
		makeConst(TEXTOID, -1, DEFAULT_COLLATION_OID, -1, CStringGetTextDatum(
					  "shardCount"), false, false),
		makeVar(3, 2, INT4OID, -1, InvalidOid, 0));
	FuncExpr *colocationArgs = makeFuncExpr(BsonRepathAndBuildFunctionOid(), BsonTypeId(),
											repathArgs, InvalidOid, InvalidOid,
											COERCE_EXPLICIT_CALL);

	/* Since no dotted paths in projection no need to override */
	bool overrideArray = false;
	Oid addFieldsOid = BsonDollaMergeDocumentsFunctionOid();
	TargetEntry *firstEntry = linitial(source->targetList);
	FuncExpr *addFields = makeFuncExpr(addFieldsOid, BsonTypeId(),
									   list_make3(firstEntry->expr, colocationArgs,
												  MakeBoolValueConst(overrideArray)),
									   InvalidOid, InvalidOid, COERCE_EXPLICIT_CALL);
	firstEntry->expr = (Expr *) addFields;

	return source;
}


/*
 * The config.shards query in a distributed query scenario
 * -----
 * 重写 config.shards 查询以支持分布式环境
 *
 * 在分布式查询场景中，此函数查询 pg_dist_node 表来获取分片列表，
 * 并以 MongoDB 兼容的格式输出。
 *
 * 返回格式：
 * {
 *   "document": [
 *     { "_id": "shard_0", "host": "node_default_0", "state": "..." },
 *     { "_id": "shard_1", "host": "node_default_1", "state": "..." }
 *   ]
 * }
 */
static Query *
RewriteConfigShardsQueryForDistribution(Query *baseQuery)
{
	Query *query = makeNode(Query);
	query->commandType = CMD_SELECT;
	query->querySource = QSRC_ORIGINAL;
	query->canSetTag = true;

	List *valuesList = NIL;

	pgbson_writer writer;
	PgbsonWriterInit(&writer);

	List *groupNodes = GetShardMapNodes();
	if (groupNodes != NIL)
	{
		WriteShardList(&writer, groupNodes);
	}

	pgbson *shardList = PgbsonWriterGetPgbson(&writer);

	pgbsonelement element;
	PgbsonToSinglePgbsonElement(shardList, &element);

	bson_iter_t values;
	BsonValueInitIterator(&element.bsonValue, &values);

	while (bson_iter_next(&values))
	{
		pgbson *bsonValue = PgbsonInitFromDocumentBsonValue(bson_iter_value(&values));
		valuesList = lappend(valuesList, list_make1(makeConst(BsonTypeId(), -1,
															  InvalidOid, -1,
															  PointerGetDatum(bsonValue),
															  false, false)));
	}

	RangeTblEntry *valuesRte = makeNode(RangeTblEntry);
	valuesRte->rtekind = RTE_VALUES;
	valuesRte->alias = valuesRte->eref = makeAlias("values", list_make1(makeString(
																			"document")));
	valuesRte->lateral = false;
	valuesRte->values_lists = valuesList;
	valuesRte->inh = false;
	valuesRte->inFromCl = true;

	valuesRte->coltypes = list_make1_oid(INT8OID);
	valuesRte->coltypmods = list_make1_int(-1);
	valuesRte->colcollations = list_make1_oid(InvalidOid);
	query->rtable = list_make1(valuesRte);

	query->jointree = makeNode(FromExpr);
	RangeTblRef *valuesRteRef = makeNode(RangeTblRef);
	valuesRteRef->rtindex = 1;
	query->jointree->fromlist = list_make1(valuesRteRef);

	/* Point to the values RTE */
	Var *documentEntry = makeVar(1, 1, BsonTypeId(), -1, InvalidOid, 0);
	TargetEntry *baseTargetEntry = makeTargetEntry((Expr *) documentEntry, 1, "document",
												   false);
	query->targetList = list_make1(baseTargetEntry);

	return query;
}


/* 获取 Citus 的 citus_shard_sizes 函数的 OID */
static Oid
GetCitusShardSizesFunctionOid(void)
{
	List *functionNameList = list_make2(makeString("pg_catalog"),
										makeString("citus_shard_sizes"));
	bool missingOK = false;

	return LookupFuncName(functionNameList, 0, NULL, missingOK);
}


/*
 * Provides the output for the config.chunks query after consulting with Citus.
 * -----
 * 重写 config.chunks 查询以支持分布式环境
 *
 * 查询 Citus 分布式元数据表，获取每个集合的分片信息。
 *
 * 生成的查询：
 * WITH coll AS (
 *   SELECT database_name, collection_name,
 *     ('ApiDataSchemaName.documents_' || collection_id)::regclass AS tableId
 *   FROM ApiCatalogSchema.collections
 * )
 * SELECT database_name, collection_name, shardid, size,
 *        shardminvalue, shardmaxvalue
 * FROM coll
 * JOIN pg_dist_shard dist ON coll.tableId = dist.logicalrelid
 * JOIN citus_shard_sizes() sz ON dist.shardid = sz.shard_id
 * JOIN pg_dist_placement p ON dist.shardid = p.shardid
 * WHERE view_definition IS NOT NULL;
 *
 * 返回格式：
 * {
 *   "_id": shard_id,
 *   "ns": "db.collection",
 *   "min": shard_min_value,
 *   "max": shard_max_value,
 *   "chunkSize": size_bytes,
 *   "shard": "group_id"
 * }
 */
static Query *
RewriteConfigChunksQueryForDistribution(Query *baseQuery)
{
	Query *source = makeNode(Query);
	source->commandType = CMD_SELECT;
	source->querySource = QSRC_ORIGINAL;
	source->canSetTag = true;

	/* Match spec for ApiCatalogSchemaName.collections function */
	RangeTblEntry *rte = makeNode(RangeTblEntry);
	List *colNames = list_concat(list_make3(makeString("database_name"), makeString(
												"collection_name"), makeString(
												"collection_id")),
								 list_make3(makeString("shard_key"), makeString(
												"collection_uuid"), makeString(
												"view_definition")));
	rte->rtekind = RTE_RELATION;
	rte->alias = rte->eref = makeAlias("collection", colNames);
	rte->lateral = false;
	rte->inFromCl = true;
	rte->relkind = RELKIND_RELATION;
	rte->functions = NIL;
	rte->inh = true;
	rte->rellockmode = AccessShareLock;

	RangeVar *rangeVar = makeRangeVar(ApiCatalogSchemaName, "collections", -1);
	rte->relid = RangeVarGetRelid(rangeVar, AccessShareLock, false);

#if PG_VERSION_NUM >= 160000
	RTEPermissionInfo *sourcePermInfo = addRTEPermissionInfo(&source->rteperminfos, rte);
	sourcePermInfo->requiredPerms = ACL_SELECT;
#else
	rte->requiredPerms = ACL_SELECT;
#endif
	source->rtable = list_make1(rte);

	RangeTblEntry *shardsRte = makeNode(RangeTblEntry);

	/* add pg_dist_shard to the RTEs */
	List *shardColNames = list_make5(makeString("logicalrelid"), makeString("shardid"),
									 makeString("shardstorage"),
									 makeString("shardminvalue"), makeString(
										 "shardmaxvalue"));

	shardsRte->rtekind = RTE_RELATION;
	shardsRte->alias = shardsRte->eref = makeAlias("shards", shardColNames);
	shardsRte->lateral = false;
	shardsRte->inFromCl = true;
	shardsRte->relkind = RELKIND_RELATION;
	shardsRte->functions = NIL;
	shardsRte->inh = true;
	shardsRte->rellockmode = AccessShareLock;

	RangeVar *shardRangeVar = makeRangeVar("pg_catalog", "pg_dist_shard", -1);
	shardsRte->relid = RangeVarGetRelid(shardRangeVar, AccessShareLock, false);


#if PG_VERSION_NUM >= 160000
	RTEPermissionInfo *shardsPermInfo = addRTEPermissionInfo(&source->rteperminfos,
															 shardsRte);
	shardsPermInfo->requiredPerms = ACL_SELECT;
#else
	shardsRte->requiredPerms = ACL_SELECT;
#endif
	source->rtable = lappend(source->rtable, shardsRte);

	/* Add citus_shard_sizes() to the RTEs */
	RangeTblEntry *shardSizeRte = makeNode(RangeTblEntry);
	List *shardSizesColNames = list_make2(makeString("shard_id"),
										  makeString("size"));

	shardSizeRte->rtekind = RTE_FUNCTION;
	shardSizeRte->alias = shardSizeRte->eref = makeAlias("shard_sizes",
														 shardSizesColNames);
	shardSizeRte->lateral = false;
	shardSizeRte->inFromCl = true;
	shardSizeRte->functions = NIL;
	shardSizeRte->inh = false;
	shardSizeRte->rellockmode = AccessShareLock;

	Oid citusShardsSizesFuncId = GetCitusShardSizesFunctionOid();
	FuncExpr *shardSizesFunc = makeFuncExpr(citusShardsSizesFuncId, RECORDOID, NIL,
											InvalidOid, InvalidOid, COERCE_EXPLICIT_CALL);

	RangeTblFunction *shardSizesFunctions = makeNode(RangeTblFunction);
	shardSizesFunctions->funcexpr = (Node *) shardSizesFunc;
	shardSizesFunctions->funccolcount = 2;
	shardSizesFunctions->funccoltypes = list_make2_oid(INT4OID, INT8OID);
	shardSizesFunctions->funccolcollations = list_make2_oid(InvalidOid, InvalidOid);
	shardSizesFunctions->funccoltypmods = list_make2_int(-1, -1);

	shardSizeRte->functions = list_make1(shardSizesFunctions);

#if PG_VERSION_NUM >= 160000
	shardSizeRte->perminfoindex = 0;
#else
	shardSizeRte->requiredPerms = ACL_SELECT;
#endif

	source->rtable = lappend(source->rtable, shardSizeRte);

	/* Add pg_dist_placement */
	RangeTblEntry *placementRte = makeNode(RangeTblEntry);
	List *placementColNames = list_make5(makeString("placementid"), makeString("shardid"),
										 makeString("shardstate"),
										 makeString("shardlength"), makeString(
											 "groupid"));

	placementRte->rtekind = RTE_RELATION;
	placementRte->alias = placementRte->eref = makeAlias("placement", placementColNames);
	placementRte->lateral = false;
	placementRte->inFromCl = true;
	placementRte->relkind = RELKIND_RELATION;
	placementRte->functions = NIL;
	placementRte->inh = true;
	placementRte->rellockmode = AccessShareLock;

	RangeVar *placementRangeVar = makeRangeVar("pg_catalog", "pg_dist_placement", -1);
	placementRte->relid = RangeVarGetRelid(placementRangeVar, AccessShareLock, false);


#if PG_VERSION_NUM >= 160000
	RTEPermissionInfo *placementPermInfo = addRTEPermissionInfo(&source->rteperminfos,
																placementRte);
	placementPermInfo->requiredPerms = ACL_SELECT;
#else
	placementRte->requiredPerms = ACL_SELECT;
#endif
	source->rtable = lappend(source->rtable, placementRte);


	RangeTblRef *collectionsRef = makeNode(RangeTblRef);
	collectionsRef->rtindex = 1;
	RangeTblRef *shardsRef = makeNode(RangeTblRef);
	shardsRef->rtindex = 2;
	RangeTblRef *shardSizesRef = makeNode(RangeTblRef);
	shardSizesRef->rtindex = 3;
	RangeTblRef *placementRef = makeNode(RangeTblRef);
	placementRef->rtindex = 4;

	List *quals = NIL;

	/* WHERE view_definition IS NOT NULL */
	NullTest *nullTest = makeNode(NullTest);
	nullTest->argisrow = false;
	nullTest->nulltesttype = IS_NULL;
	nullTest->arg = (Expr *) makeVar(collectionsRef->rtindex, 6, BsonTypeId(), -1,
									 InvalidOid, 0);
	quals = lappend(quals, nullTest);

	/* Join collection with shard */
	Var *collectionIdVar = makeVar(collectionsRef->rtindex, 3, INT8OID, -1, InvalidOid,
								   0);

	char *tablePrefixString = psprintf("%s.%s", ApiDataSchemaName,
									   DOCUMENT_DATA_TABLE_NAME_PREFIX);
	Const *tablePrefix = makeConst(TEXTOID, -1, InvalidOid, -1, CStringGetTextDatum(
									   tablePrefixString), false, false);

	/* Construct the string */
	FuncExpr *concatExpr = makeFuncExpr(F_TEXTANYCAT, TEXTOID,
										list_make2(tablePrefix, collectionIdVar),
										InvalidOid, DEFAULT_COLLATION_OID,
										COERCE_EXPLICIT_CALL);

	FuncExpr *castConcatExpr = makeFuncExpr(F_REGCLASS, OIDOID, list_make1(concatExpr),
											DEFAULT_COLLATION_OID, DEFAULT_COLLATION_OID,
											COERCE_EXPLICIT_CALL);

	/* Get the regclass of the join */
	Var *regclassVar = makeVar(shardsRef->rtindex, 1, OIDOID, -1, InvalidOid, 0);
	FuncExpr *oidEqualFunc = makeFuncExpr(F_OIDEQ, BOOLOID, list_make2(regclassVar,
																	   castConcatExpr),
										  DEFAULT_COLLATION_OID, DEFAULT_COLLATION_OID,
										  COERCE_EXPLICIT_CALL);
	quals = lappend(quals, oidEqualFunc);

	/* Join pg_dist_shard with shard_sizes */
	Var *shardIdLeftVar = makeVar(shardsRef->rtindex, 2, INT8OID, -1, -1, 0);
	Var *shardIdRightVar = makeVar(shardSizesRef->rtindex, 1, INT8OID, -1, -1, 0);
	FuncExpr *shardIdEqual = makeFuncExpr(F_INT8EQ, BOOLOID, list_make2(shardIdLeftVar,
																		shardIdRightVar),
										  InvalidOid, InvalidOid, COERCE_EXPLICIT_CALL);
	quals = lappend(quals, shardIdEqual);

	/* Join pg_dist_shard with pg_dist_placement */
	Var *shardIdPlacementVar = makeVar(placementRef->rtindex, 2, INT8OID, -1, -1, 0);
	FuncExpr *shardIdPlacementEqual = makeFuncExpr(F_INT8EQ, BOOLOID, list_make2(
													   shardIdLeftVar,
													   shardIdPlacementVar),
												   InvalidOid, InvalidOid,
												   COERCE_EXPLICIT_CALL);
	quals = lappend(quals, shardIdPlacementEqual);

	source->jointree = makeFromExpr(list_make4(collectionsRef, shardsRef, shardSizesRef,
											   placementRef), (Node *) make_ands_explicit(
										quals));

	Const *groupPrefix = MakeTextConst("shard_", 6);
	FuncExpr *groupIdStr = makeFuncExpr(F_TEXTANYCAT, TEXTOID,
										list_make2(groupPrefix, makeVar(
													   placementRef->rtindex, 5, INT4OID,
													   -1, -1, 0)),
										InvalidOid, DEFAULT_COLLATION_OID,
										COERCE_EXPLICIT_CALL);
	source->targetList = list_make5(
		makeTargetEntry((Expr *) makeVar(collectionsRef->rtindex, 1, TEXTOID, -1, -1, 0),
						1, "database_name", false),
		makeTargetEntry((Expr *) makeVar(collectionsRef->rtindex, 2, TEXTOID, -1, -1, 0),
						2, "collection_name", false),
		makeTargetEntry((Expr *) makeVar(shardsRef->rtindex, 2, INT8OID, -1, -1, 0), 3,
						"shard_id", false),
		makeTargetEntry((Expr *) makeVar(shardSizesRef->rtindex, 2, INT8OID, -1, -1, 0),
						4, "size", false),
		makeTargetEntry((Expr *) groupIdStr, 5, "groupid", false));
	source->targetList = lappend(source->targetList, makeTargetEntry((Expr *) makeVar(
																		 shardsRef->
																		 rtindex, 5,
																		 TEXTOID, -1, -1,
																		 0), 6,
																	 "shard_min", false));
	source->targetList = lappend(source->targetList, makeTargetEntry((Expr *) makeVar(
																		 shardsRef->
																		 rtindex, 6,
																		 TEXTOID, -1, -1,
																		 0), 7,
																	 "shard_max", false));

	RangeTblEntry *subQueryRte = MakeSubQueryRte(source, 1, 0, "config_shards_base",
												 true);

	Var *rowExpr = makeVar(1, 0, RECORDOID, -1, InvalidOid, 0);
	FuncExpr *funcExpr = makeFuncExpr(RowGetBsonFunctionOid(), BsonTypeId(),
									  list_make1(rowExpr), InvalidOid, InvalidOid,
									  COERCE_EXPLICIT_CALL);

	/* Build the projection spec for this query */
	pgbson_writer specWriter;
	PgbsonWriterInit(&specWriter);

	PgbsonWriterAppendUtf8(&specWriter, "_id", 3, "$shard_id");

	/* Write ns: <db.coll> */
	pgbson_writer childWriter;
	PgbsonWriterStartDocument(&specWriter, "ns", 2, &childWriter);

	pgbson_array_writer childArray;
	PgbsonWriterStartArray(&childWriter, "$concat", 7, &childArray);
	PgbsonArrayWriterWriteUtf8(&childArray, "$database_name");
	PgbsonArrayWriterWriteUtf8(&childArray, ".");
	PgbsonArrayWriterWriteUtf8(&childArray, "$collection_name");
	PgbsonWriterEndArray(&childWriter, &childArray);
	PgbsonWriterEndDocument(&specWriter, &childWriter);

	PgbsonWriterAppendUtf8(&specWriter, "min", 3, "$shard_min");
	PgbsonWriterAppendUtf8(&specWriter, "max", 3, "$shard_max");
	PgbsonWriterAppendUtf8(&specWriter, "chunkSize", 9, "$size");
	PgbsonWriterAppendUtf8(&specWriter, "shard", 5, "$groupid");

	Const *specConst = MakeBsonConst(PgbsonWriterGetPgbson(&specWriter));
	funcExpr = makeFuncExpr(BsonDollarProjectFunctionOid(), BsonTypeId(),
							list_make2(funcExpr, specConst), InvalidOid, InvalidOid,
							COERCE_EXPLICIT_CALL);

	TargetEntry *upperEntry = makeTargetEntry((Expr *) funcExpr, 1, "document",
											  false);
	Query *newquery = makeNode(Query);
	newquery->commandType = CMD_SELECT;
	newquery->querySource = source->querySource;
	newquery->canSetTag = true;
	newquery->targetList = list_make1(upperEntry);
	newquery->rtable = list_make1(subQueryRte);

	RangeTblRef *rtr = makeNode(RangeTblRef);
	rtr->rtindex = 1;
	newquery->jointree = makeFromExpr(list_make1(rtr), NULL);

	return newquery;
}


static void
UndistributeAndRedistributeTable(const char *postgresTable, const char *colocateWith,
								 const char *shardKeyValue)
{
	bool readOnly = false;
	bool resultNullIgnore = false;
	Oid tableDetailsArgTypes[3] = { TEXTOID, TEXTOID, TEXTOID };
	Datum tableDetailsArgValues[3] = {
		CStringGetTextDatum(postgresTable), CStringGetTextDatum(colocateWith), (Datum) 0
	};
	char argNulls[3] = { ' ', ' ', 'n' };

	/* This is a distributed table with distributionColumn == shard_key_value */
	const char *undistributeTable = "SELECT undistribute_table($1)";

	/* First undistribute the table.*/
	ExtensionExecuteQueryWithArgsViaSPI(undistributeTable, 1, tableDetailsArgTypes,
										tableDetailsArgValues, argNulls, readOnly,
										SPI_OK_SELECT, &resultNullIgnore);

	/* Then redistribute it as a single shard distributed table */
	if (shardKeyValue != NULL)
	{
		tableDetailsArgValues[2] = CStringGetTextDatum(shardKeyValue);
		argNulls[2] = ' ';
	}

	const char *redistributeTable =
		"SELECT create_distributed_table($1::regclass, distribution_column => $3, colocate_with => $2)";
	ExtensionExecuteQueryWithArgsViaSPI(redistributeTable, 3, tableDetailsArgTypes,
										tableDetailsArgValues, argNulls, readOnly,
										SPI_OK_SELECT, &resultNullIgnore);
}


/*
 * For sharded citus tables, colocates the table with "none".
 * -----
 * 将已分片的 Citus 表与 "none" 进行共置（取消共置）
 *
 * 此函数用于取消已分片表的共置关系，使其不再与其他表共享分片。
 * 调用 Citus 的 alter_distributed_table 函数，将 colocate_with 设置为 'none'。
 */
static void
ColocateShardedCitusTablesWithNone(const char *sourceTableName)
{
	const char *colocateQuery =
		"SELECT alter_distributed_table(table_name => $1, colocate_with => $2, cascade_to_colocated => false)";

	Oid argTypes[2] = { TEXTOID, TEXTOID };
	Datum argValues[2] = {
		CStringGetTextDatum(sourceTableName), CStringGetTextDatum("none")
	};
	char *argNulls = NULL;

	bool isNull = false;
	int colocateArgs = 2;
	ExtensionExecuteQueryWithArgsViaSPI(colocateQuery, colocateArgs, argTypes, argValues,
										argNulls, false, SPI_OK_SELECT, &isNull);
}


/*
 * Gets the distribution details of a given citus table.
 * -----
 * 获取指定 Citus 表的分布式详细信息
 *
 * 从 citus_tables 视图查询表的分布式配置信息。
 *
 * 返回信息：
 * - citus_table_type: 表类型（reference/distributed）
 * - distribution_column: 分布列（分片键列名）
 * - shard_count: 分片数量
 */
static void
GetCitusTableDistributionDetails(const char *sourceTableName, const char **citusTableType,
								 const char **distributionColumn, int64 *shardCount)
{
	const char *tableDetailsQuery =
		"SELECT citus_table_type, distribution_column, shard_count FROM public.citus_tables WHERE table_name = $1::regclass";

	Oid tableDetailsArgTypes[1] = { TEXTOID };
	Datum tableDetailsArgValues[1] = { CStringGetTextDatum(sourceTableName) };
	char *argNulls = NULL;
	bool readOnly = true;

	Datum results[3] = { 0 };
	bool resultNulls[3] = { 0 };
	ExtensionExecuteMultiValueQueryWithArgsViaSPI(
		tableDetailsQuery, 1, tableDetailsArgTypes, tableDetailsArgValues, argNulls,
		readOnly, SPI_OK_SELECT, results, resultNulls, 3);

	if (resultNulls[0] || resultNulls[1] || resultNulls[2])
	{
		ereport(ERROR, (errmsg(
							"Unexpected result found null value for shards query [0]=%d, [1]=%d, [2]=%d",
							resultNulls[0], resultNulls[1], resultNulls[2])));
	}

	*citusTableType = TextDatumGetCString(results[0]);
	*distributionColumn = TextDatumGetCString(results[1]);
	*shardCount = DatumGetInt64(results[2]);
}


/*
 * breaks colocation for unsharded citus tables.
 * -----
 * 解除未分片 Citus 表的共置关系
 *
 * 根据表当前的分发模式，采用不同的策略解除共置：
 * 1. 如果是单分片分布式表（distribution_column = '<none>'）：
 *    直接调用 update_distributed_table_colocation 设置为 'none'
 * 2. 如果是其他类型的分布式表：
 *    先调用 undistribute_table 取消分布式
 *    再调用 create_distributed_table 重新创建为单分片表
 */
static void
ColocateUnshardedCitusTablesWithNone(const char *sourceTableName)
{
	/* First get the current distribution mode/column. */
	const char *citusTableType = NULL;
	const char *distributionColumn = NULL;
	int64 shardCount = 0;
	GetCitusTableDistributionDetails(sourceTableName, &citusTableType,
									 &distributionColumn, &shardCount);

	ereport(NOTICE, (errmsg(
						 "Current table type %s, distribution column %s, shardCount %ld",
						 citusTableType,
						 distributionColumn, shardCount)));

	/* Scenario 1: It's already a single shard distributed table */
	bool readOnly = false;
	char *argNulls = NULL;
	bool resultNullIgnore = false;
	Oid tableDetailsArgTypes[1] = { TEXTOID };
	Datum tableDetailsArgValues[1] = { CStringGetTextDatum(sourceTableName) };
	if (strcmp(distributionColumn, "<none>") == 0)
	{
		const char *updateColocationQuery =
			"SELECT update_distributed_table_colocation($1, colocate_with => 'none')";
		ExtensionExecuteQueryWithArgsViaSPI(updateColocationQuery, 1,
											tableDetailsArgTypes, tableDetailsArgValues,
											argNulls, readOnly, SPI_OK_SELECT,
											&resultNullIgnore);
	}
	else
	{
		const char *colocateWith = "none";
		distributionColumn = NULL;
		UndistributeAndRedistributeTable(sourceTableName, colocateWith,
										 distributionColumn);
	}
}


/*
 * Core logic for colocating 2 unsharded citus tables.
 * -----
 * 共置两个未分片 Citus 表的核心逻辑
 *
 * 将一个未分片表与另一个未分片表进行共置，使它们共享相同的物理分片。
 *
 * 参数：
 * - tableToColocate: 要共置的表
 * - colocateWithTableName: 目标共置表
 *
 * 返回值：
 * - 返回分片键值（"shard_key_value" 或 NULL）
 * - NULL 表示单分片分布式表
 * - "shard_key_value" 表示使用 shard_key_value 作为分布列
 */
static const char *
ColocateUnshardedCitusTables(const char *tableToColocate, const
							 char *colocateWithTableName)
{
	const char *sourceCitusTableType = NULL;
	const char *sourceDistributionColumn = NULL;
	int64 sourceShardCount = 0;
	GetCitusTableDistributionDetails(tableToColocate, &sourceCitusTableType,
									 &sourceDistributionColumn, &sourceShardCount);

	ereport(INFO, (errmsg("Source table type %s, distribution column %s, shardCount %ld",
						  sourceCitusTableType, sourceDistributionColumn,
						  sourceShardCount)));
	if (sourceShardCount != 1)
	{
		ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_COMMANDNOTSUPPORTED),
						errmsg(
							"Cannot colocate collection to source in current state. Please colocate the source collection with colocation: none")));
	}

	bool resultNullIgnore = false;
	char *argNulls = NULL;
	const char *targetCitusTableType = NULL;
	const char *targetDistributionColumn = NULL;
	int64 targetShardCount = 0;
	GetCitusTableDistributionDetails(colocateWithTableName, &targetCitusTableType,
									 &targetDistributionColumn, &targetShardCount);

	ereport(INFO, (errmsg("Target table type %s, distribution column %s, shardCount %ld",
						  targetCitusTableType, targetDistributionColumn,
						  targetShardCount)));
	bool isTableSingleShardDistributed = strcmp(tableToColocate, "<none>") == 0;
	if (strcmp(targetDistributionColumn, "<none>") == 0)
	{
		/* target is a single shard distributed table */
		if (isTableSingleShardDistributed)
		{
			Oid tableDetailsArgTypes[2] = { TEXTOID, TEXTOID };
			Datum tableDetailsArgValues[2] = {
				CStringGetTextDatum(tableToColocate), (Datum) 0
			};

			/* First update colocation data to none */
			const char *updateDistributionToNoneQuery =
				"SELECT update_distributed_table_colocation($1, colocate_with => 'none')";

			bool readOnly = false;
			ExtensionExecuteQueryWithArgsViaSPI(updateDistributionToNoneQuery, 1,
												tableDetailsArgTypes,
												tableDetailsArgValues, argNulls, readOnly,
												SPI_OK_SELECT, &resultNullIgnore);

			/* Then move the shard to match the source table */
			MoveShardToDistributedTable(tableToColocate, colocateWithTableName);


			/* Match the colocation to the other table */
			const char *updateDistributionToTargetTable =
				"SELECT update_distributed_table_colocation($1, colocate_with => $2)";
			tableDetailsArgValues[1] = CStringGetTextDatum(colocateWithTableName);

			ExtensionExecuteQueryWithArgsViaSPI(updateDistributionToTargetTable, 2,
												tableDetailsArgTypes,
												tableDetailsArgValues, argNulls, readOnly,
												SPI_OK_SELECT, &resultNullIgnore);
		}
		else
		{
			/* This is a distributed table with distributionColumn == shard_key_value */
			const char *distributionColumn = NULL;
			UndistributeAndRedistributeTable(tableToColocate, colocateWithTableName,
											 distributionColumn);
		}

		/* Distribution column is none */
		return NULL;
	}
	else
	{
		/* The target is a distributed table with a single shard (legacy tables) */
		if (isTableSingleShardDistributed)
		{
			/* We need to back-convert this to a single shard distributed table. */
			/* This is a distributed table with distributionColumn == shard_key_value */
			UndistributeAndRedistributeTable(tableToColocate, colocateWithTableName,
											 "shard_key_value");
		}
		else
		{
			/* Neither tables are single shard distributed. */
			/* We need to back-convert this to a single shard distributed table. */
			/* This is a distributed table with distributionColumn == shard_key_value */
			Oid tableDetailsArgTypes[2] = { TEXTOID, TEXTOID };
			Datum tableDetailsArgValues[2] = {
				CStringGetTextDatum(tableToColocate), CStringGetTextDatum(
					colocateWithTableName)
			};

			/* Now create distributed table with colocation with the target */
			const char *alterDistributedTableQuery =
				"SELECT alter_distributed_table(table_name => $1, colocate_with => $2)";

			bool readOnly = false;
			ExtensionExecuteQueryWithArgsViaSPI(alterDistributedTableQuery, 2,
												tableDetailsArgTypes,
												tableDetailsArgValues, argNulls, readOnly,
												SPI_OK_SELECT, &resultNullIgnore);
		}

		/* legacy table value */
		return "shard_key_value";
	}
}


/* 获取分布式表的分片数量 */
static int
GetShardCountForDistributedTable(Oid relationId)
{
	const char *shardIdCountQuery =
		"SELECT COUNT(*) FROM pg_dist_shard WHERE logicalrelid = $1";

	Oid shardCountArgTypes[1] = { OIDOID };
	Datum shardCountArgValues[1] = { ObjectIdGetDatum(relationId) };
	char *argNullNone = NULL;
	bool readOnly = true;
	bool isNull = false;
	Datum shardIdDatum = ExtensionExecuteQueryWithArgsViaSPI(shardIdCountQuery, 1,
															 shardCountArgTypes,
															 shardCountArgValues,
															 argNullNone, readOnly,
															 SPI_OK_SELECT,
															 &isNull);
	if (isNull)
	{
		return 0;
	}

	return DatumGetInt64(shardIdDatum);
}


/*
 * Gets the colocationId for a given pg table.
 * -----
 * 获取指定表的共置 ID
 *
 * 从 pg_dist_partition 系统表查询表的共置 ID。
 * 共置 ID 相同的表会共享相同的物理分片。
 */
static int
GetColocationForTable(Oid tableOid, const char *collectionName, const char *tableName)
{
	char *colocationId =
		"SELECT colocationid FROM pg_dist_partition WHERE logicalrelid = $1";

	int numArgs = 1;
	Oid argTypes[1] = { OIDOID };
	Datum argValues[1] = { ObjectIdGetDatum(tableOid) };
	char *argNulls = NULL;

	bool isNull = false;
	Datum result = ExtensionExecuteQueryWithArgsViaSPI(colocationId, numArgs, argTypes,
													   argValues,
													   argNulls, false, SPI_OK_SELECT,
													   &isNull);
	if (isNull)
	{
		ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_INTERNALERROR),
						errmsg(
							"Could not find collection in internal colocation metadata: %s",
							collectionName),
						errdetail_log(
							"Could not find collection in internal colocation metadata %s: %s",
							collectionName, tableName)));
	}

	return DatumGetInt32(result);
}


/*
 * Gets the node level details for a single shard table.
 * -----
 * 获取单分片表的节点详细信息
 *
 * 查询给定表的分片 ID 和托管节点的名称、端口信息。
 *
 * 参数：
 * - postgresTable: PostgreSQL 表名
 * - nodeName: 输出节点名称
 * - nodePort: 输出节点端口
 * - shardId: 输出分片 ID
 */
static void
GetNodeNamePortForPostgresTable(const char *postgresTable, char **nodeName, int *nodePort,
								int64 *shardId)
{
	char *allArgsProvidedNulls = NULL;
	int shardIdQuerynArgs = 1;
	Datum shardIdArgValues[1] = { CStringGetTextDatum(postgresTable) };
	Oid shardIdArgTypes[1] = { TEXTOID };
	bool readOnly = false;
	bool isNull = false;

	const char *shardIdQuery =
		"SELECT shardid FROM pg_dist_shard WHERE logicalrelid = $1::regclass";

	/* While all these queries appear read-only inner Citus code seems to break this assumption
	 * And these queries fail when readOnly is marked as TRUE. Until the underlying issue is fixed
	 * in citus, these are left as read-only false.
	 */
	Datum shardIdValue = ExtensionExecuteQueryWithArgsViaSPI(shardIdQuery,
															 shardIdQuerynArgs,
															 shardIdArgTypes,
															 shardIdArgValues,
															 allArgsProvidedNulls,
															 readOnly,
															 SPI_OK_SELECT, &isNull);

	if (isNull)
	{
		ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_INTERNALERROR),
						errmsg("Could not extract shard_id for newly created table"),
						errdetail_log("Could not get shardId value for postgres table %s",
									  postgresTable)));
	}

	/* Now get the source node/port for this shard */
	const char *shardPlacementTable =
		"SELECT nodename, nodeport FROM pg_dist_shard_placement WHERE shardid = $1";

	int shardPlacementArgs = 1;
	Datum shardPlacementArgValues[1] = { shardIdValue };
	Oid shardPlacementArgTypes[1] = { INT8OID };

	Datum currentNodeDatums[2] = { 0 };
	bool currentNodeIsNulls[2] = { 0 };
	ExtensionExecuteMultiValueQueryWithArgsViaSPI(shardPlacementTable, shardPlacementArgs,
												  shardPlacementArgTypes,
												  shardPlacementArgValues,
												  allArgsProvidedNulls, readOnly,
												  SPI_OK_SELECT, currentNodeDatums,
												  currentNodeIsNulls, 2);

	if (currentNodeIsNulls[0] || currentNodeIsNulls[1])
	{
		ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_INTERNALERROR),
						errmsg(
							"Could not find shard placement for newly created table shard"),
						errdetail_log(
							"Could not find shardId %ld in the placement table for table %s: node is null %d, port is null %d",
							DatumGetInt64(shardIdValue), postgresTable,
							currentNodeIsNulls[0], currentNodeIsNulls[1])));
	}

	*nodeName = TextDatumGetCString(currentNodeDatums[0]);
	*nodePort = DatumGetInt32(currentNodeDatums[1]);
	*shardId = DatumGetInt64(shardIdValue);
}


/*
 * Moves a single shard table to be colocated with another target single shard table.
 * -----
 * 将单分片表移动到与目标单分片表共置
 *
 * 调用 Citus 的 citus_move_shard_placement 函数，将分片从源节点移动到目标节点。
 * 这用于实现表共置，使相关数据位于相同的物理节点上。
 */
static void
MoveShardToDistributedTable(const char *postgresTableToMove, const char *targetShardTable)
{
	char *toMoveNodeName, *targetNodeName;
	int toMoveNodePort, targetNodePort;
	int64 toMoveShardId, targetShardId;
	GetNodeNamePortForPostgresTable(postgresTableToMove, &toMoveNodeName, &toMoveNodePort,
									&toMoveShardId);
	GetNodeNamePortForPostgresTable(targetShardTable, &targetNodeName, &targetNodePort,
									&targetShardId);

	elog(INFO, "Moving shard %ld from %s:%d to %s:%d", DatumGetInt64(toMoveShardId),
		 toMoveNodeName, toMoveNodePort,
		 targetNodeName, targetNodePort);
	const char *moveShardQuery =
		"SELECT citus_move_shard_placement(shard_id => $1, source_node_name => $2, source_node_port => $3,"
		" target_node_name => $4, target_node_port => $5, shard_transfer_mode => 'block_writes'::citus.shard_transfer_mode)";
	Datum moveShardDatums[5] =
	{
		toMoveShardId,
		CStringGetTextDatum(toMoveNodeName),
		Int32GetDatum(toMoveNodePort),
		CStringGetTextDatum(targetNodeName),
		Int32GetDatum(targetNodePort)
	};

	Oid moveShardTypes[5] =
	{
		INT8OID,
		TEXTOID,
		INT4OID,
		TEXTOID,
		INT4OID
	};

	bool resultIsNullIgnore = false;
	char *allArgsProvidedNulls = NULL;
	bool readOnly = false;
	ExtensionExecuteQueryWithArgsViaSPI(moveShardQuery, 5, moveShardTypes,
										moveShardDatums, allArgsProvidedNulls,
										readOnly, SPI_OK_SELECT, &resultIsNullIgnore);
}


/*
 * Write the shard map information to BSON writer
 * -----
 * 将分片映射信息写入 BSON writer
 *
 * 生成 MongoDB 兼容的分片映射格式，包含：
 * - map: 分片到节点列表的映射
 * - hosts: 节点到分片的映射
 * - nodes: 节点详细信息（角色、活跃状态、集群名称）
 */
static void
WriteShardMap(pgbson_writer *writer, List *groupMap)
{
	/* Now that we have the list ordered by group & role, write out the object */
	/* 首先将列表按组和角色排序后，输出对象 */
	/* First object is "map" */
	/* 第一个对象是 "map" */
	pgbson_writer childWriter;
	StringInfo hostStringInfo = makeStringInfo();
	int32_t groupId = -1;
	ListCell *listCell;
	const char *shardName = NULL;
	char *separator = "";
	PgbsonWriterStartDocument(writer, "map", 3, &childWriter);

	foreach(listCell, groupMap)
	{
		NodeInfo *nodeInfo = lfirst(listCell);
		if (nodeInfo->groupId != groupId)
		{
			/* New groupId */
			/* 新的 groupId，开始一个新的分片 */
			if (shardName != NULL)
			{
				PgbsonWriterAppendUtf8(&childWriter, shardName, -1, hostStringInfo->data);
				resetStringInfo(hostStringInfo);
			}

			shardName = nodeInfo->mongoShardName;
			groupId = nodeInfo->groupId;
			appendStringInfo(hostStringInfo, "%s/", nodeInfo->mongoShardName);
			separator = "";
		}

		if (nodeInfo->isactive)
		{
			appendStringInfo(hostStringInfo, "%s%s", separator, nodeInfo->mongoNodeName);
			separator = ",";
		}
	}

	if (shardName != NULL && hostStringInfo->len > 0)
	{
		PgbsonWriterAppendUtf8(&childWriter, shardName, -1, hostStringInfo->data);
	}

	PgbsonWriterEndDocument(writer, &childWriter);

	/* Now write the hosts object */
	/* 现在写入 hosts 对象：节点到分片的映射 */
	PgbsonWriterStartDocument(writer, "hosts", 5, &childWriter);

	foreach(listCell, groupMap)
	{
		NodeInfo *nodeInfo = lfirst(listCell);
		if (nodeInfo->isactive)
		{
			PgbsonWriterAppendUtf8(&childWriter, nodeInfo->mongoNodeName, -1,
								   nodeInfo->mongoShardName);
		}
	}

	PgbsonWriterEndDocument(writer, &childWriter);

	/* Now write the nodes object */
	/* 现在写入 nodes 对象：节点的详细信息 */
	PgbsonWriterStartDocument(writer, "nodes", 5, &childWriter);

	foreach(listCell, groupMap)
	{
		NodeInfo *nodeInfo = lfirst(listCell);
		pgbson_writer nodeWriter;
		PgbsonWriterStartDocument(&childWriter, nodeInfo->mongoNodeName, -1, &nodeWriter);
		PgbsonWriterAppendUtf8(&nodeWriter, "role", 4, nodeInfo->nodeRole);
		PgbsonWriterAppendBool(&nodeWriter, "active", 6, nodeInfo->isactive);
		PgbsonWriterAppendUtf8(&nodeWriter, "cluster", 7, nodeInfo->nodeCluster);
		PgbsonWriterEndDocument(&childWriter, &nodeWriter);
	}

	PgbsonWriterEndDocument(writer, &childWriter);
}


/*
 * Write the shard list information to BSON writer
 * -----
 * 将分片列表信息写入 BSON writer
 *
 * 生成 MongoDB 兼容的分片列表格式，包含每个分片的 ID 和节点列表。
 */
static void
WriteShardList(pgbson_writer *writer, List *groupMap)
{
	pgbson_array_writer arrayWriter;
	PgbsonWriterStartArray(writer, "shards", 6, &arrayWriter);

	ListCell *listCell;
	StringInfo hostStringInfo = makeStringInfo();
	int32_t groupId = -1;
	const char *shardName = NULL;
	char *separator = "";
	pgbson_writer nestedObjectWriter;
	foreach(listCell, groupMap)
	{
		NodeInfo *nodeInfo = lfirst(listCell);
		if (nodeInfo->groupId != groupId)
		{
			/* New groupId */
			if (shardName != NULL)
			{
				PgbsonArrayWriterStartDocument(&arrayWriter, &nestedObjectWriter);
				PgbsonWriterAppendUtf8(&nestedObjectWriter, "_id", 3, shardName);
				PgbsonWriterAppendUtf8(&nestedObjectWriter, "nodes", 5,
									   hostStringInfo->data);
				PgbsonArrayWriterEndDocument(&arrayWriter, &nestedObjectWriter);
				resetStringInfo(hostStringInfo);
			}

			shardName = nodeInfo->mongoShardName;
			groupId = nodeInfo->groupId;
			appendStringInfo(hostStringInfo, "%s/", nodeInfo->mongoShardName);
			separator = "";
		}

		if (nodeInfo->isactive)
		{
			appendStringInfo(hostStringInfo, "%s%s", separator, nodeInfo->mongoNodeName);
			separator = ",";
		}
	}

	if (shardName != NULL && hostStringInfo->len > 0)
	{
		PgbsonArrayWriterStartDocument(&arrayWriter, &nestedObjectWriter);
		PgbsonWriterAppendUtf8(&nestedObjectWriter, "_id", 3, shardName);
		PgbsonWriterAppendUtf8(&nestedObjectWriter, "nodes", 5, hostStringInfo->data);
		PgbsonArrayWriterEndDocument(&arrayWriter, &nestedObjectWriter);
	}

	PgbsonWriterEndArray(writer, &arrayWriter);
}


/*
 * Fetches the nodes in the cluster ordered by groupId and nodeRole.
 * -----
 * 获取集群中的节点列表，按 groupId 和 nodeRole 排序
 *
 * 查询 Citus 的 pg_dist_node 系统表，获取集群中所有应托管分片的节点信息。
 *
 * 返回：NodeInfo 结构体的列表，包含：
 * - groupid: 组 ID
 * - nodeid: 节点 ID
 * - noderole: 节点角色（primary/secondary）
 * - nodecluster: 集群名称
 * - isactive: 是否活跃
 *
 * 同时生成 MongoDB 兼容的节点名称和分片名称：
 * - mongoNodeName: node_<cluster>_<nodeid>
 * - mongoShardName: shard_<groupid>
 */
static List *
GetShardMapNodes(void)
{
	/* First query pg_dist_node to get the set of nodes in the cluster */
	/* 首先查询 pg_dist_node 获取集群中的节点集合 */
	const char *baseQuery = psprintf(
		"WITH base AS (SELECT groupid, nodeid, noderole::text, nodecluster::text, isactive FROM pg_dist_node WHERE shouldhaveshards ORDER BY groupid, noderole)"
		" SELECT %s.BSON_ARRAY_AGG(%s.row_get_bson(base), 'nodes') FROM base",
		ApiCatalogSchemaName, ApiCatalogSchemaName);

	bool isNull = true;
	Datum nodeDatum = ExtensionExecuteQueryViaSPI(baseQuery, true, SPI_OK_SELECT,
												  &isNull);

	if (isNull)
	{
		return NIL;
	}

	pgbson *queryResult = DatumGetPgBson(nodeDatum);
	pgbsonelement singleElement;
	bson_iter_t arrayIter;
	PgbsonToSinglePgbsonElement(queryResult, &singleElement);
	if (singleElement.bsonValue.value_type != BSON_TYPE_ARRAY)
	{
		ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_INTERNALERROR),
						errmsg(
							"Unexpected - getShardMap path %s should have an array not %s",
							singleElement.path,
							BsonTypeName(singleElement.bsonValue.value_type)),
						errdetail_log(
							"Unexpected - getShardMap path %s should have an array not %s",
							singleElement.path,
							BsonTypeName(singleElement.bsonValue.value_type))));
	}

	BsonValueInitIterator(&singleElement.bsonValue, &arrayIter);

	List *groupMap = NIL;
	int32_t currentGroup = -1;
	while (bson_iter_next(&arrayIter))
	{
		if (!BSON_ITER_HOLDS_DOCUMENT(&arrayIter))
		{
			ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_INTERNALERROR),
							errmsg(
								"Unexpected - getShardMap inner groupId %d should have a document not %s",
								currentGroup,
								BsonTypeName(bson_iter_type(&arrayIter))),
							errdetail_log(
								"Unexpected - getShardMap inner groupId %d should have a document not %s",
								currentGroup,
								BsonTypeName(bson_iter_type(&arrayIter)))));
		}

		bson_iter_t objectIter;
		NodeInfo *nodeInfo = palloc0(sizeof(NodeInfo));
		if (bson_iter_recurse(&arrayIter, &objectIter))
		{
			int numFields = 0;
			while (bson_iter_next(&objectIter))
			{
				const char *key = bson_iter_key(&objectIter);
				if (strcmp(key, "groupid") == 0)
				{
					nodeInfo->groupId = bson_iter_int32(&objectIter);
					currentGroup = nodeInfo->groupId;
					numFields++;
				}
				else if (strcmp(key, "nodeid") == 0)
				{
					nodeInfo->nodeId = bson_iter_int32(&objectIter);
					numFields++;
				}
				else if (strcmp(key, "noderole") == 0)
				{
					nodeInfo->nodeRole = bson_iter_dup_utf8(&objectIter, NULL);
					numFields++;
				}
				else if (strcmp(key, "nodecluster") == 0)
				{
					nodeInfo->nodeCluster = bson_iter_dup_utf8(&objectIter, NULL);
					numFields++;
				}
				else if (strcmp(key, "isactive") == 0)
				{
					nodeInfo->isactive = bson_iter_bool(&objectIter);
					numFields++;
				}
			}

			if (numFields != 5)
			{
				ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_INTERNALERROR),
								errmsg(
									"Found missing fields in querying shard table: Found %d fields",
									numFields),
								errdetail_log(
									"Found missing fields in querying shard table: Found %d fields",
									numFields)));
			}

			/* Generate MongoDB compatible node name and shard name */
			/* 生成 MongoDB 兼容的节点名称和分片名称 */
			nodeInfo->mongoNodeName = psprintf("node_%s_%d", nodeInfo->nodeCluster,
											   nodeInfo->nodeId);
			nodeInfo->mongoShardName = psprintf("shard_%d", nodeInfo->groupId);
			groupMap = lappend(groupMap, nodeInfo);
		}
	}

	return groupMap;
}
