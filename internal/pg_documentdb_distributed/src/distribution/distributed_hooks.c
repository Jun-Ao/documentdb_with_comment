/*-------------------------------------------------------------------------
 * Copyright (c) Microsoft Corporation.  All rights reserved.
 *
 * src/distribution/distributed_hooks.c
 *
 * Implementation of API Hooks for a distributed execution.
 *-------------------------------------------------------------------------
 * 分布式执行钩子实现
 *
 * 本文件实现了 DocumentDB 在分布式环境下运行所需的钩子函数。
 * 这些钩子函数覆盖了单机版 DocumentDB 的核心功能，使其能够在
 * Citus 分布式环境下正确执行。
 *
 * 主要功能：
 * 1. 元数据协调器检测：判断当前节点是否为协调器
 * 2. 分布式表管理：创建分布式表、管理分片
 * 3. 分布式查询执行：支持嵌套分布式执行、可交换写入
 * 4. 分片信息获取：获取集合的分片 ID 和名称
 * 5. 索引操作：分布式环境下的索引构建和取消
 * 6. 操作取消：取消分布式环境下的运行操作
 *
 * 核心概念：
 * - 钩子函数（Hook）：函数指针，允许扩展覆盖默认行为
 * - 元数据协调器：管理集群元数据的主节点
 * - 可交换写入：允许 Citus 并行优化某些修改操作
 * - 嵌套分布式执行：在分布式查询内执行另一个分布式查询
 * - 分片表：分布式表的物理存储表（格式：table_shardid）
 *-------------------------------------------------------------------------
 */
#include <postgres.h>
#include <miscadmin.h>
#include <utils/builtins.h>
#include <utils/timestamp.h>
#include <nodes/makefuncs.h>
#include <catalog/namespace.h>
#include <utils/lsyscache.h>
#include <utils/memutils.h>
#include <metadata/index.h>
#include <parser/parse_func.h>

#include "io/bson_core.h"
#include "utils/query_utils.h"
#include "utils/guc_utils.h"
#include "utils/version_utils.h"
#include "metadata/metadata_cache.h"
#include "utils/documentdb_errors.h"

#include "metadata/collection.h"
#include "api_hooks_def.h"

#include "shard_colocation.h"
#include "distributed_hooks.h"

#include "distributed_index_operations.h"

extern bool UseLocalExecutionShardQueries;
extern char *ApiDistributedSchemaName;

extern bool ShouldSetupIndexQueueInUdf;
extern bool EnableMetadataReferenceTableSync;
extern char *DistributedOperationsQuery;
extern char *DistributedApplicationNamePrefix;

/* Cached value for the current Global PID - can cache once
 * Since nodeId, Pid are stable.
 * -----
 * 当前 Global PID 的缓存值 - 只需缓存一次
 * 因为 nodeId 和 Pid 在连接期间保持稳定。
 * Global PID 是 Citus 中标识跨节点后端的唯一 ID。
 */
#define INVALID_CITUS_INTERNAL_BACKEND_GPID 0
static uint64 DocumentDBCitusGlobalPid = 0;

/*
 * In Citus we query citus_is_coordinator() to get if
 * the current node is a metadata coordinator
 * -----
 * 判断当前节点是否为元数据协调器
 *
 * 元数据协调器是负责管理集群元数据的主节点。
 * 某些操作（如元数据更新）只能在协调器上执行。
 *
 * 返回值：true 表示当前节点是协调器
 */
static bool
IsMetadataCoordinatorCore(void)
{
	bool readOnly = true;
	bool isNull = false;
	Datum resultBoolDatum = ExtensionExecuteQueryViaSPI(
		"SELECT citus_is_coordinator()", readOnly, SPI_OK_SELECT, &isNull);

	return !isNull && DatumGetBool(resultBoolDatum);
}


/*
 * Runs a command on the cluster's metadata holding coordinator node.
 * -----
 * 在集群的元数据协调器节点上运行命令
 *
 * 此函数用于将命令路由到协调器节点执行。
 * 使用 Citus 的 run_command_on_coordinator 函数实现。
 *
 * 参数：query - 要在协调器上执行的 SQL 查询
 *
 * 返回值：包含执行结果的结构体
 * - nodeId: 执行节点的 ID
 * - success: 是否执行成功
 * - response: 响应内容（如果有）
 */
static DistributedRunCommandResult
RunCommandOnMetadataCoordinatorCore(const char *query)
{
	const char *baseQuery =
		"SELECT nodeId, success, result FROM run_command_on_coordinator($1)";

	int nargs = 1;
	Oid argTypes[1] = { TEXTOID };
	Datum argValues[1] = { CStringGetTextDatum(query) };
	char argNulls[1] = { ' ' };
	bool readOnly = true;

	int numResultValues = 3;
	Datum resultDatums[3] = { 0 };
	bool resultNulls[3] = { 0 };
	ExtensionExecuteMultiValueQueryWithArgsViaSPI(
		baseQuery, nargs, argTypes, argValues, argNulls, readOnly, SPI_OK_SELECT,
		resultDatums, resultNulls, numResultValues);

	DistributedRunCommandResult result = { 0 };

	/* TODO: handle error in coordinator correctly as it could be an exception we need to honor. */
	if (resultNulls[0] || resultNulls[1])
	{
		result.success = false;
		return result;
	}

	result.nodeId = DatumGetInt32(resultDatums[0]);
	result.success = DatumGetBool(resultDatums[1]);
	result.response = NULL;
	if (!resultNulls[2])
	{
		result.response = DatumGetTextP(resultDatums[2]);
	}

	return result;
}


/*
 * Hook to run a query with commutative writes.
 *
 * This sets citus.all_modifications_commutative to true, before executing the query.
 * Enabling this setting allows Citus to optimize the execution of these modifications
 * across distributed shards in parallel, potentially improving performance for certain workloads.
 *
 * This setting should be used cautiously in various queries. Currently the use cases here are around
 * modifying reference tables based on the primary key only (where we know we only update the one
 * row).
 * See https://github.com/citusdata/citus/blob/a2315fdc677675b420913ca4f81116e165d52397/src/backend/distributed/executor/distributed_execution_locks.c#L149
 * for more details.
 * -----
 * 使用可交换写入模式运行查询
 *
 * 在执行查询前设置 citus.all_modifications_commutative 为 true。
 * 启用此设置允许 Citus 并行优化跨分布式分片的修改操作，
 * 可能提高某些工作负载的性能。
 *
 * 注意：此设置应谨慎使用。当前使用场景包括：
 * - 仅基于主键修改参考表（确保只更新一行）
 *
 * 可交换写入允许 Citus 重新排序或并行执行写操作，
 * 因为这些操作的结果与执行顺序无关。
 */
static Datum
RunQueryWithCommutativeWritesCore(const char *query, int nargs, Oid *argTypes,
								  Datum *argValues, char *argNulls,
								  int expectedSPIOK, bool *isNull)
{
	int savedGUCLevel = NewGUCNestLevel();
	SetGUCLocally("citus.all_modifications_commutative", "true");

	Datum result;
	bool readOnly = false;
	if (nargs > 0)
	{
		result = ExtensionExecuteQueryWithArgsViaSPI(query, nargs, argTypes, argValues,
													 argNulls, readOnly, expectedSPIOK,
													 isNull);
	}
	else
	{
		result = ExtensionExecuteQueryViaSPI(query, readOnly, expectedSPIOK, isNull);
	}

	RollbackGUCChange(savedGUCLevel);
	return result;
}


/* 使用顺序修改模式运行查询 */
static Datum
RunQueryWithSequentialModificationCore(const char *query, int expectedSPIOK, bool *isNull)
{
	/* 设置 citus.multi_shard_modify_mode 为 sequential */
	/* 这确保跨分片修改使用单个连接顺序执行，而非并行 */
	int savedGUCLevel = NewGUCNestLevel();
	SetGUCLocally("citus.multi_shard_modify_mode", "sequential");

	bool readOnly = false;
	Datum result = ExtensionExecuteQueryViaSPI(query, readOnly, expectedSPIOK, isNull);
	RollbackGUCChange(savedGUCLevel);
	return result;
}


/*
 * 判断表名是否为 DocumentDB 的分片表
 * -----
 * 判断给定的表名是否为 DocumentDB 的分片表
 *
 * DocumentDB 的分片表命名格式：documents_<collection_id>_<shard_id>
 * 例如：documents_1_102011
 *
 * 参数：
 * - relName: 表名
 * - numEndPointer: 指向表名中最后一个数字之后的指针
 *
 * 返回值：true 如果是分片表（在第二个下划线之后）
 *
 * 注意：这是一个简化的检查，适用于热路径。
 *       更完整的检查需要查询 pg_dist_shard 表，但开销较大。
 */
static bool
IsShardTableForDocumentDbTableCore(const char *relName, const char *numEndPointer)
{
	/* It's definitely a documents query - it's a shard query if there's a documents_<num>_<num>
	 * So treat it as such if there's 2 '_'.
	 * This is a hack but need to find a better way to recognize
	 * a worker query.
	 * Note that this logic is a simpler form of the RelationIsAKnownShard
	 * function in Citus. However, that function does extract the shard_id
	 * and does a Scan on the pg_dist table as well to determine if it's really
	 * a shard. However, this is too expensive for the hotpath of every query.
	 * Consequently this simple check *should* be sufficient in the hot path.
	 *
	 * TODO: Could we do something like IsCitusTableType where we cache the results of
	 * this? Ideally we could map this to something in the Mongo Collection Cache. However
	 * the inverse lookup if it's not in the cache is not easily done in the query path.
	 */
	return numEndPointer != NULL && *numEndPointer == '_';
}


/*
 * Distributes a Postgres table across all the available node based on the
 * specified distribution column.
 *
 * returns the actual distribution column used in the table.
 * -----
 * 将 PostgreSQL 表分布到所有可用节点
 *
 * 此函数是创建分布式表的核心实现，负责：
 * 1. 调用 Citus 的 create_distributed_table 函数
 * 2. 设置适当的 GUC 参数以确保正确行为
 * 3. 处理共置配置（与其他表共享分片）
 *
 * 参数：
 * - postgresTable: 要分布的表名（带 schema）
 * - distributionColumn: 分布列（分片键），NULL 表示单分片表
 * - colocateWith: 共置表名，NULL 表示自动选择（优先 changes 表）
 * - shardCount: 分片数量，0 表示单分片
 *
 * 返回值：实际使用的分布列名称
 *
 * 特殊处理：
 * - 启用不安全触发器（用于本地操作）
 * - 使用顺序修改模式（确保同一事务中可见分片）
 * - 自动检测并使用 changes 表作为默认共置表
 */
static const char *
DistributePostgresTableCore(const char *postgresTable, const char *distributionColumn,
							const char *colocateWith, int shardCount)
{
	const char *distributionColumnUsed = distributionColumn;

	/*
	 * By default, Citus triggers are off as there are potential pitfalls if
	 * not used properly, such as, doing operations on the remote node. We use
	 * them here only for local operations.
	 */
	SetGUCLocally("citus.enable_unsafe_triggers", "on");

	/*
	 * Make sure that create_distributed_table does not parallelize shard creation,
	 * since that would prevent us from pushing down an insert_one or update_one
	 * call in the same transaction. When Citus pushes down a function call, it needs
	 * to see both a distributed table and a shard, and if those are created over
	 * separate connections that is not possible until commit.
	 *
	 * Setting multi_shard_modify_mode to sequential to enforce using a single
	 * connection is a temporary workaround until this is solved in Citus.
	 * https://github.com/citusdata/citus/issues/6169
	 */
	SetGUCLocally("citus.multi_shard_modify_mode", "sequential");

	/* Because ApiDataSchema.changes is created inside initialize/complete
	 * We need to skip checking cluster version there so do other checks first.
	 */
	const char *createQuery =
		"SELECT create_distributed_table($1::regclass, $2, colocate_with => $3, shard_count => $4)";
	int nargs = 4;
	Oid argTypes[4] = { TEXTOID, TEXTOID, TEXTOID, INT4OID };
	Datum argValues[4] = {
		CStringGetTextDatum(postgresTable),
		(Datum) 0,
		(Datum) 0,
		(Datum) 0,
	};

	if (distributionColumnUsed == NULL && shardCount != 0)
	{
		ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_INTERNALERROR),
						errmsg(
							"Unexpected - distribution column is null but shardCount is %d",
							shardCount),
						errdetail_log(
							"Unexpected - distribution column is null but shardCount is %d",
							shardCount)));
	}

	char argNulls[4] = { ' ', 'n', 'n', 'n' };

	if (distributionColumnUsed != NULL)
	{
		argValues[1] = CStringGetTextDatum(distributionColumnUsed);
		argNulls[1] = ' ';
	}

	if (colocateWith != NULL)
	{
		argValues[2] = CStringGetTextDatum(colocateWith);
		argNulls[2] = ' ';
	}
	else
	{
		bool innerReadOnly = true;
		bool isNull = true;
		ExtensionExecuteQueryViaSPI(
			FormatSqlQuery("SELECT 1 FROM pg_catalog.pg_dist_partition pdp "
						   " JOIN pg_class pc on pdp.logicalrelid = pc.oid "
						   " WHERE relname = 'changes' AND relnamespace = '%s'::regnamespace",
						   ApiDataSchemaName),
			innerReadOnly, SPI_OK_SELECT, &isNull);
		if (!isNull)
		{
			char changesTableName[NAMEDATALEN] = { 0 };
			sprintf(changesTableName, "%s.changes", ApiDataSchemaName);
			argValues[2] = CStringGetTextDatum(changesTableName);
			argNulls[2] = ' ';
		}
		else
		{
			/* If ApiDataSchema.changes doesn't exist - fall back into "none" */
			argValues[2] = CStringGetTextDatum("none");
			argNulls[2] = ' ';
		}
	}

	if (shardCount > 0)
	{
		argValues[3] = Int32GetDatum(shardCount);
		argNulls[3] = ' ';
	}

	bool readOnly = false;
	bool isNull = false;
	ExtensionExecuteQueryWithArgsViaSPI(createQuery, nargs,
										argTypes, argValues, argNulls, readOnly,
										SPI_OK_SELECT, &isNull);

	return distributionColumnUsed;
}


/* 允许在当前事务中使用嵌套分布式执行 */
static void
AllowNestedDistributionInCurrentTransactionCore(void)
{
	/* 设置 citus.allow_nested_distributed_execution 为 true */
	/* 允许在分布式查询内执行另一个分布式查询 */
	SetGUCLocally("citus.allow_nested_distributed_execution", "true");
}


/*
 * Allows nested distributed execution in the current query for citus.
 * -----
 * 使用嵌套分布式执行运行多值查询
 *
 * 此函数允许在分布式查询内部执行另一个分布式查询。
 * 某些场景下需要此功能，例如在分布式聚合中访问元数据表。
 *
 * 参数：
 * - query: 要执行的 SQL 查询
 * - nArgs: 参数数量
 * - argTypes: 参数类型数组
 * - argDatums: 参数值数组
 * - argNulls: 参数是否为空的标记数组
 * - readOnly: 是否为只读查询
 * - expectedSPIOK: 期望的 SPI 返回代码
 * - datums: 输出参数，返回结果值数组
 * - isNull: 输出参数，返回结果是否为空
 * - numValues: 要获取的返回值数量
 */
static void
RunMultiValueQueryWithNestedDistributionCore(const char *query, int nArgs, Oid *argTypes,
											 Datum *argDatums, char *argNulls, bool
											 readOnly,
											 int expectedSPIOK, Datum *datums,
											 bool *isNull, int numValues)
{
	int gucLevel = NewGUCNestLevel();
	AllowNestedDistributionInCurrentTransactionCore();
	ExtensionExecuteMultiValueQueryWithArgsViaSPI(query, nArgs, argTypes, argDatums,
												  argNulls, readOnly,
												  expectedSPIOK, datums, isNull,
												  numValues);
	RollbackGUCChange(gucLevel);
}


/*
 * Given a relationId and a collectionId for the relation, tries to get
 * the shard tableName for the table if it is an unsharded table with a single shard.
 * e.g. for documents_1 returns documents_1_102011.
 * If shards are unavailable returns NULL - can be retried.
 * If the shard is remote and not loca - returns ""
 * -----
 * 获取未分片集合的分片表名
 *
 * 对于单分片的分布式表（未分片集合），获取其物理分片表名。
 * 这允许某些操作直接在物理分片表上执行，提高性能。
 *
 * 参数：
 * - relationId: 集合表的 OID
 * - collectionId: 集合 ID
 * - tableName: 集合表名（如 documents_1）
 *
 * 返回值：
 * - 分片表名（如 documents_1_102011）- 如果是本地单分片表
 * - NULL - 如果不是分布式表
 * - "" - 如果分片不在本地（远程分片）
 *
 * 注意：此功能需要启用 UseLocalExecutionShardQueries 标志
 */
static const char *
TryGetShardNameForUnshardedCollectionCore(Oid relationId, uint64 collectionId, const
										  char *tableName)
{
	if (!UseLocalExecutionShardQueries)
	{
		/* Defensive - only turn this on with feature flag */
		return "";
	}

	const char *shardIdDetailsQuery =
		"SELECT shardid, shardminvalue, shardmaxvalue FROM pg_dist_shard WHERE logicalrelid = $1 LIMIT 1";

	Oid shardCountArgTypes[1] = { OIDOID };
	Datum shardCountArgValues[1] = { ObjectIdGetDatum(relationId) };
	char *argNullNone = NULL;
	bool readOnly = true;

	int numValues = 3;
	Datum resultDatums[3] = { 0 };
	bool resultNulls[3] = { 0 };

	ExtensionExecuteMultiValueQueryWithArgsViaSPI(
		shardIdDetailsQuery, 1, shardCountArgTypes, shardCountArgValues, argNullNone,
		readOnly, SPI_OK_SELECT, resultDatums, resultNulls, numValues);
	if (resultNulls[0])
	{
		/* Not a distributed table */
		return NULL;
	}

	int64_t shardIdValue = DatumGetInt64(resultDatums[0]);

	/* Only support this for single shard distributed
	 * This is only true if shardminvalue  and shardmaxvalue are NULL
	 */
	if (!resultNulls[1] || !resultNulls[2])
	{
		/* has at least some shard values */
		return "";
	}

	/* Construct the shard table name */
	char *shardTableName = psprintf("%s_%ld", tableName, shardIdValue);

	/* Now that we have a shard table name, try to find it in pg_class without locking it */
	Oid shardTableOid = get_relname_relid(shardTableName, ApiDataNamespaceOid());
	if (shardTableOid != InvalidOid)
	{
		return shardTableName;
	}
	else
	{
		return "";
	}
}


/*
 * Gets distributed application for citus based applications.
 * -----
 * 获取分布式应用名称
 *
 * 返回符合 Citus 内部后端命名规范的应用名称。
 * 这样可以使这些连接不计入 Citus 的 max_client_backends 配额。
 *
 * 格式：citus_run_command gpid=<global_pid> <app_name>
 *
 * Global PID 是 Citus 中标识跨节点后端的唯一 ID。
 * 使用此格式可以让 Citus 识别这些是内部连接。
 */
static const char *
GetDistributedApplicationNameCore(void)
{
	if (DocumentDBCitusGlobalPid == INVALID_CITUS_INTERNAL_BACKEND_GPID)
	{
		bool isNull;
		Datum result = ExtensionExecuteQueryViaSPI(
			"SELECT pg_catalog.citus_backend_gpid()", true, SPI_OK_SELECT, &isNull);

		if (isNull)
		{
			return NULL;
		}

		DocumentDBCitusGlobalPid = DatumGetUInt64(result);

		if (DocumentDBCitusGlobalPid == INVALID_CITUS_INTERNAL_BACKEND_GPID)
		{
			return NULL;
		}
	}

	/*
	 * Match the application name pattern for the citus run_command* internal backend
	 * so these don't count in the quota for max_client_backends for citus.
	 * -----
	 * 匹配 Citus run_command* 内部后端的应用名称模式
	 * 这样这些连接就不会计入 Citus 的 max_client_backends 配额
	 */
	return psprintf("citus_run_command gpid=%lu %s",
					DocumentDBCitusGlobalPid, GetExtensionApplicationName());
}


/* 执行参考表的元数据检查 */
static bool
ExecuteMetadataChecksForReferenceTables(const char *tableName)
{
	/* First get the shard_id for the table */
	/* 首先获取表的分片 ID */
	StringInfo queryStringInfo = makeStringInfo();
	appendStringInfo(queryStringInfo,
					 "SELECT shardid FROM pg_catalog.pg_dist_shard WHERE logicalrelid = '%s.%s'::regclass",
					 ApiCatalogSchemaName, tableName);

	bool isNull = false;
	Datum result = ExtensionExecuteQueryViaSPI(queryStringInfo->data, false,
											   SPI_OK_SELECT, &isNull);

	if (isNull)
	{
		return false;
	}

	int64 shardId = DatumGetInt64(result);

	/* Get the number of nodes for the primary group */
	/* 获取主节点组的数量 */
	result = ExtensionExecuteQueryViaSPI(
		"SELECT COUNT(*)::int4 FROM pg_catalog.pg_dist_node WHERE isactive AND noderole = 'primary'",
		false, SPI_OK_SELECT, &isNull);
	if (isNull)
	{
		return false;
	}

	int numNodes = DatumGetInt32(result);

	resetStringInfo(queryStringInfo);
	appendStringInfo(queryStringInfo,
					 "SELECT COUNT(*)::int4 FROM pg_catalog.pg_dist_placement WHERE shardid = %ld",
					 shardId);
	result = ExtensionExecuteQueryViaSPI(queryStringInfo->data, false, SPI_OK_SELECT,
										 &isNull);
	if (isNull)
	{
		return false;
	}

	int numPlacements = DatumGetInt32(result);

	if (numPlacements != numNodes)
	{
		/* There was an add node but the metadata table needed wasn't replicated: Call replicate_reference_tables first */
		/* 有新节点添加但元数据表未复制：调用 replicate_reference_tables */
		ExtensionExecuteQueryOnLocalhostViaLibPQ(
			"SELECT pg_catalog.replicate_reference_tables('block_writes')");
		return true;
	}
	else
	{
		return false;
	}
}


/*
 * 确保元数据表已复制到所有节点
 *
 * 检查参考表是否已复制到所有活动节点。
 * 如果发现节点数量不匹配，触发参考表复制。
 *
 * 参数：tableName - 要检查的表名
 *
 * 返回值：true 表示触发了复制操作
 */
static bool
EnsureMetadataTableReplicatedCore(const char *tableName)
{
	if (!EnableMetadataReferenceTableSync)
	{
		return false;
	}

	/* Set min messagees to reduce log spam in tests */
	int savedGUCLevel = NewGUCNestLevel();
	SetGUCLocally("client_min_messages", "WARNING");
	bool result = ExecuteMetadataChecksForReferenceTables(tableName);
	RollbackGUCChange(savedGUCLevel);
	return result;
}


/* 获取扩展版本刷新查询 */
static char *
TryGetExtendedVersionRefreshQueryCore(void)
{
	/* Update the version check query to consider distributed versions */
	/* 更新版本检查查询以考虑分布式版本 */
	MemoryContext currContext = MemoryContextSwitchTo(TopMemoryContext);
	StringInfo s = makeStringInfo();
	appendStringInfo(s,
					 "SELECT regexp_split_to_array(TRIM(%s.bson_get_value_text(metadata, 'last_deploy_version'), '\"'), '[-\\.]')::int4[] FROM %s.%s_cluster_data",
					 CoreSchemaName, ApiDistributedSchemaName, ExtensionObjectPrefix);
	MemoryContextSwitchTo(currContext);

	ereport(DEBUG1, (errmsg("Version refresh query is %s", s->data)));
	return s->data;
}


/*
 * 获取集合的分片 ID 列表
 *
 * 查询 Citus 的 pg_dist_shard 表，获取指定集合的所有分片 ID。
 *
 * 参数：relationOid - 集合表的 OID
 *
 * 返回值：分片 ID 列表（每个元素是 uint64_t*）
 */
static List *
GetShardIdsForCollection(Oid relationOid)
{
	const char *query =
		"SELECT array_agg(shardid) FROM pg_dist_shard WHERE logicalrelid = $1";

	int nargs = 1;
	Oid argTypes[1] = { OIDOID };
	Datum argValues[1] = { ObjectIdGetDatum(relationOid) };
	bool isReadOnly = true;
	bool isNull = true;
	Datum shardIds = ExtensionExecuteQueryWithArgsViaSPI(query, nargs, argTypes,
														 argValues,
														 NULL, isReadOnly, SPI_OK_SELECT,
														 &isNull);

	if (isNull)
	{
		return NIL;
	}

	ArrayType *arrayType = DatumGetArrayTypeP(shardIds);

	/* Need to build the result */
	const int slice_ndim = 0;
	ArrayMetaState *mState = NULL;
	ArrayIterator shardIterator = array_create_iterator(arrayType,
														slice_ndim, mState);

	List *shardIdList = NIL;
	Datum shardIdDatum;
	while (array_iterate(shardIterator, &shardIdDatum, &isNull))
	{
		if (isNull)
		{
			continue;
		}

		uint64_t *shardIdPointer = palloc(sizeof(uint64_t));
		*shardIdPointer = DatumGetInt64(shardIdDatum);
		shardIdList = lappend(shardIdList, shardIdPointer);
	}

	array_free_iterator(shardIterator);
	return shardIdList;
}


/*
 * 获取集合的分片 ID 和名称
 *
 * 获取指定集合的所有分片的 OID 和名称。
 * 用于需要在所有分片上执行操作的场景。
 *
 * 参数：
 * - relationOid: 集合表的 OID
 * - tableName: 集合表名（如 documents_1）
 * - shardOidArray: 输出参数，返回分片 OID 数组
 * - shardNameArray: 输出参数，返回分片名称数组
 * - shardCount: 输出参数，返回分片数量
 */
static void
GetShardIdsAndNamesForCollectionCore(Oid relationOid, const char *tableName,
									 Datum **shardOidArray, Datum **shardNameArray,
									 int32_t *shardCount)
{
	*shardOidArray = NULL;
	*shardNameArray = NULL;
	*shardCount = 0;

	ListCell *shardCell;
	List *shardIdList = GetShardIdsForCollection(relationOid);

	/* Need to build the result */
	/* 需要构建结果 */
	int numItems = list_length(shardIdList);
	Datum *resultDatums = palloc0(sizeof(Datum) * numItems);
	Datum *resultNameDatums = palloc0(sizeof(Datum) * numItems);
	int resultCount = 0;
	foreach(shardCell, shardIdList)
	{
		uint64_t *shardIdPointer = (uint64_t *) lfirst(shardCell);
		char shardName[NAMEDATALEN] = { 0 };
		pg_sprintf(shardName, "%s_%lu", tableName, *shardIdPointer);

		RangeVar *rangeVar = makeRangeVar(ApiDataSchemaName, shardName, -1);
		bool missingOk = true;
		Oid shardRelationId = RangeVarGetRelid(rangeVar, AccessShareLock, missingOk);
		if (shardRelationId != InvalidOid)
		{
			Assert(resultCount < numItems);
			resultDatums[resultCount] = shardRelationId;
			resultNameDatums[resultCount] = CStringGetTextDatum(shardName);
			resultCount++;
		}
	}

	/* Now that we have the shard list as a Datum*, create an array type */
	/* 现在我们有了分片列表作为 Datum*，创建数组类型 */
	if (resultCount > 0)
	{
		*shardOidArray = resultDatums;
		*shardNameArray = resultNameDatums;
		*shardCount = resultCount;
	}
	else
	{
		pfree(resultDatums);
		pfree(resultNameDatums);
	}

	list_free_deep(shardIdList);
}


/*
 * Get an index build request from the Index queue.
 */
static const char *
GetPidForIndexBuildCore(void)
{
	const char *queryStrDistributed = " citus_pid_for_gpid(iq.global_pid)";

	return queryStrDistributed;
}


static const char *
TryGetIndexBuildJobOpIdQueryCore(void)
{
	const char *queryStrDistributed =
		"SELECT citus_backend_gpid(), query_start FROM pg_stat_activity where pid = pg_backend_pid();";

	return queryStrDistributed;
}


static char *
TryGetCancelIndexBuildQueryCore(int32_t indexId, char cmdType)
{
	StringInfo cmdStr = makeStringInfo();
	appendStringInfo(cmdStr,
					 "SELECT citus_pid_for_gpid(iq.global_pid) AS pid, iq.start_time AS timestamp");
	appendStringInfo(cmdStr,
					 " FROM %s iq WHERE index_id = %d",
					 GetIndexQueueName(), indexId);

	return cmdStr->data;
}


static bool
ShouldScheduleIndexBuildsCore()
{
	return false;
}


static List *
GetDistributedShardIndexOidsCore(uint64_t collectionId, int indexId, bool ignoreMissing)
{
	/* First for the given collection, get the shard ids associated with it */
	char tableName[NAMEDATALEN] = { 0 };
	pg_sprintf(tableName, DOCUMENT_DATA_TABLE_NAME_FORMAT, collectionId);

	Oid relationOid = get_relname_relid(tableName, ApiDataNamespaceOid());
	List *shardIdList = GetShardIdsForCollection(relationOid);

	AllowNestedDistributionInCurrentTransactionCore();

	List *indexShardList = NIL;
	ListCell *shardCell;
	foreach(shardCell, shardIdList)
	{
		uint64_t *shardIdPointer = (uint64_t *) lfirst(shardCell);
		char shardIndexName[NAMEDATALEN] = { 0 };
		pg_sprintf(shardIndexName, DOCUMENT_DATA_TABLE_INDEX_NAME_FORMAT "_%lu", indexId,
				   *shardIdPointer);


		Oid indexOid = get_relname_relid(shardIndexName, ApiDataNamespaceOid());
		if (indexOid != InvalidOid)
		{
			indexShardList = lappend_oid(indexShardList, indexOid);
		}
		else if (!ignoreMissing)
		{
			ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_COMMANDNOTSUPPORTED),
							errmsg("failed to find index to get index metadata."
								   " This can happen if it's a multi-node cluster and is not yet supported")));
		}
	}

	return indexShardList;
}


static const char *
GetDistributedOperationCancellationQuery(int64 shardId, StringView *opIdView,
										 int *nargs, Oid **argTypes, Datum **argValues,
										 char **argNulls)
{
	StringInfo query = makeStringInfo();

	/*
	 * KillOp query attempts to cancel any operation that is still active but is a no-op
	 * when the operation is already finished the connection state is 'idle', in order to
	 * kill idle connection we have to force terminate the backend.
	 *
	 * For distributed cases we use citus overrides to cancel operations that are identified the
	 * gpid
	 */
	appendStringInfo(query,
					 " SELECT "
					 "  CASE WHEN state = 'idle' THEN pg_terminate_backend($1)"
					 "       ELSE pg_cancel_backend($1)"
					 "  END "
					 " FROM citus_stat_activity WHERE global_pid = $1 "
					 " AND (EXTRACT(epoch FROM query_start) * 1000000)::numeric(20,0) = $2::numeric(20,0) "
					 " AND NOT is_worker_query "
					 "LIMIT 1");

	*nargs = 2;
	*argTypes = palloc(sizeof(Oid) * (*nargs));
	*argValues = palloc(sizeof(Datum) * (*nargs));
	*argNulls = palloc0(sizeof(char) * (*nargs));
	(*argTypes)[0] = INT8OID;
	(*argValues)[0] = Int64GetDatum(shardId);
	(*argTypes)[1] = TEXTOID;
	(*argValues)[1] = CStringGetTextDatum(opIdView->string);

	(*argNulls)[0] = ' ';
	(*argNulls)[1] = ' ';

	return query->data;
}


/*
 * Register hook overrides for DocumentDB.
 * -----
 * 注册 DocumentDB 的钩子覆盖
 *
 * 此函数在扩展加载时调用，将分布式环境的实现函数注册到各个钩子中。
 * 这使单机版 DocumentDB 的核心功能能够在 Citus 分布式环境下正确执行。
 *
 * 主要钩子：
 * - is_metadata_coordinator_hook: 判断是否为协调器
 * - run_command_on_metadata_coordinator_hook: 在协调器上执行命令
 * - distribute_postgres_table_hook: 创建分布式表
 * - get_shard_ids_and_names_for_collection_hook: 获取分片信息
 * - update_postgres_index_hook: 更新索引（分布式版本）
 *
 * 全局配置：
 * - DefaultInlineWriteOperations: 禁用内联写入操作
 * - ShouldUpgradeDataTables: 禁用数据表升级
 * - ShouldSetupIndexQueueInUdf: 禁用 UDF 中设置索引队列
 *
 * 特殊查询：
 * - DistributedOperationsQuery: 用于获取分布式操作的查询
 * - DistributedApplicationNamePrefix: Citus 内部应用名称前缀
 */
void
InitializeDocumentDBDistributedHooks(void)
{
	/* 注册元数据协调器相关钩子 */
	is_metadata_coordinator_hook = IsMetadataCoordinatorCore;
	run_command_on_metadata_coordinator_hook = RunCommandOnMetadataCoordinatorCore;

	/* 注册分布式查询执行钩子 */
	run_query_with_commutative_writes_hook = RunQueryWithCommutativeWritesCore;
	run_query_with_sequential_modification_mode_hook =
		RunQueryWithSequentialModificationCore;

	/* 注册分布式表管理钩子 */
	distribute_postgres_table_hook = DistributePostgresTableCore;
	run_query_with_nested_distribution_hook =
		RunMultiValueQueryWithNestedDistributionCore;
	allow_nested_distribution_in_current_transaction_hook =
		AllowNestedDistributionInCurrentTransactionCore;

	/* 注册分片相关钩子 */
	is_shard_table_for_documentdb_table_hook = IsShardTableForDocumentDbTableCore;
	try_get_shard_name_for_unsharded_collection_hook =
		TryGetShardNameForUnshardedCollectionCore;
	get_distributed_application_name_hook = GetDistributedApplicationNameCore;
	ensure_metadata_table_replicated_hook = EnsureMetadataTableReplicatedCore;

	/* 设置全局配置 */
	DefaultInlineWriteOperations = false;
	ShouldUpgradeDataTables = false;
	ShouldSetupIndexQueueInUdf = false;

	/* 更新共置相关钩子 */
	UpdateColocationHooks();

	/* 注册版本刷新和分片信息钩子 */
	try_get_extended_version_refresh_query_hook = TryGetExtendedVersionRefreshQueryCore;
	get_shard_ids_and_names_for_collection_hook = GetShardIdsAndNamesForCollectionCore;

	/* 注册索引构建相关钩子 */
	get_pid_for_index_build_hook = GetPidForIndexBuildCore;
	try_get_index_build_job_op_id_query_hook = TryGetIndexBuildJobOpIdQueryCore;
	try_get_cancel_index_build_query_hook = TryGetCancelIndexBuildQueryCore;
	should_schedule_index_builds_hook = ShouldScheduleIndexBuildsCore;

	/* 注册分片索引和更新索引钩子 */
	get_shard_index_oids_hook = GetDistributedShardIndexOidsCore;
	update_postgres_index_hook = UpdateDistributedPostgresIndex;
	get_operation_cancellation_query_hook = GetDistributedOperationCancellationQuery;

	/* 设置分布式操作查询和应用名称前缀 */
	DistributedOperationsQuery =
		"SELECT * FROM pg_stat_activity LEFT JOIN pg_catalog.get_all_active_transactions() ON process_id = pid JOIN pg_catalog.pg_dist_local_group ON TRUE";
	DistributedApplicationNamePrefix = "citus_internal";
}
