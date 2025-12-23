/*-------------------------------------------------------------------------
 * Copyright (c) Microsoft Corporation.  All rights reserved.
 *
 * src/distribution/node_distribution_operations.c
 *
 * Implementation of scenarios that require distribution on a per node basis.
 * 实现需要在每个节点基础上进行分发的场景
 *-------------------------------------------------------------------------
 */

#include <postgres.h>
#include <miscadmin.h>
#include <utils/builtins.h>
#include <utils/timestamp.h>
#include <nodes/makefuncs.h>
#include <catalog/namespace.h>
#include <utils/lsyscache.h>

#include "utils/query_utils.h"
#include "utils/documentdb_errors.h"
#include "utils/error_utils.h"
#include "io/bson_core.h"
#include "metadata/metadata_cache.h"
#include "node_distributed_operations.h"
#include "api_hooks.h"


/*
 * ChooseShardNamesForTable - 为分布式表选择分片名称
 * @distributedTableName: 分布式表的名称
 *
 * 该函数为给定的分布式表选择分片名称集合。
 * 通过查询 Citus 的 pg_dist_shard 和 pg_dist_placement 系统表，
 * 获取每个分组（groupid）中最小的分片ID，并构造分片名称数组。
 *
 * 返回值：包含分片名称的数组，如果查询失败则返回 NULL
 */
static ArrayType *
ChooseShardNamesForTable(const char *distributedTableName)
{
	/* SQL 查询：从 pg_dist_shard 和 pg_dist_placement 表中获取分片信息
	 * r1 CTE: 按 groupid 分组，选择每个分组中最小的分片ID，构造分片名称（格式：表名_分片ID）
	 * 最终查询: 将所有分片名称聚合成一个数组返回
	 */
	const char *query =
		"WITH r1 AS (SELECT MIN($1 || '_' || sh.shardid) AS shardName FROM pg_dist_shard sh JOIN pg_dist_placement pl "
		" on pl.shardid = sh.shardid WHERE logicalrelid = $1::regclass GROUP by groupid) "
		" SELECT ARRAY_AGG(r1.shardName) FROM r1";

	int nargs = 1;
	Oid argTypes[1] = { TEXTOID };
	Datum argValues[1] = { CStringGetTextDatum(distributedTableName) };
	bool isReadOnly = true;
	bool isNull = true;
	Datum result = ExtensionExecuteQueryWithArgsViaSPI(query, nargs, argTypes, argValues,
													   NULL, isReadOnly, SPI_OK_SELECT,
													   &isNull);

	if (isNull)
	{
		return NULL;
	}

	return DatumGetArrayTypeP(result);
}


/*
 * CoordinatorHasShardsForTable - 检查协调器节点是否有指定表的分片
 * @distributedTableName: 分布式表的名称
 *
 * 该函数检查协调器节点（groupid = 0）是否承载了指定表的任何分片。
 * 通过查询 citus_shards 和 pg_dist_node 表来确定协调器上是否有分片。
 *
 * 返回值：如果协调器有该表的分片返回 true，否则返回 false
 */
static bool
CoordinatorHasShardsForTable(const char *distributedTableName)
{
	/* SQL 查询：检查协调器节点（groupid = 0）是否有指定表的分片
	 * 通过连接 citus_shards 和 pg_dist_node 表，统计符合条件的分片数量
	 */
	const char *query =
		"select COUNT(1) from citus_shards cs join pg_dist_node pd on cs.nodename = pd.nodename and cs.nodeport = pd.nodeport where cs.table_name = $1::regclass and pd.groupid = 0";

	int nargs = 1;
	Oid argTypes[1] = { TEXTOID };
	Datum argValues[1] = { CStringGetTextDatum(distributedTableName) };
	bool isReadOnly = true;
	bool isNull = true;
	Datum result = ExtensionExecuteQueryWithArgsViaSPI(query, nargs, argTypes, argValues,
													   NULL, isReadOnly, SPI_OK_SELECT,
													   &isNull);

	if (isNull)
	{
		return false;
	}

	return DatumGetInt32(result) > 0;
}


/*
 * ExecutePerNodeCommand - 在每个节点上执行分布式命令
 * @nodeFunction: 要在节点上执行的函数的 OID
 * @nodeFunctionArg: 传递给节点函数的 BSON 参数
 * @readOnly: 是否为只读操作
 * @distributedTableName: 分布式表的名称
 * @backFillCoordinator: 是否在协调器上回填执行（如果协调器没有分片）
 *
 * 该函数是分布式执行的核心函数，用于在所有承载指定表分片的节点上执行命令。
 *
 * 执行流程：
 * 1. 获取分布式表的所有分片名称
 * 2. 构造一个分布式查询，调用 command_node_worker 函数
 * 3. Citus 会自动将查询路由到各个分片所在的节点
 * 4. 每个节点验证自己是否在选定的分片列表中，如果是则执行，否则跳过
 * 5. 如果需要，在协调器上也执行一次（确保元数据一致性）
 *
 * 这种设计确保：
 * - 每个节点只执行一次命令
 * - 事务性：所有分片上的操作要么全部成功，要么全部失败
 * - 元数据和系统目录在协调器上保持一致
 *
 * 返回值：包含每个节点执行结果（BSON 格式）的列表
 */
List *
ExecutePerNodeCommand(Oid nodeFunction, pgbson *nodeFunctionArg, bool readOnly, const
					  char *distributedTableName, bool backFillCoordinator)
{
	/* 步骤1：获取分布式表的所有分片名称 */
	ArrayType *chosenShards = ChooseShardNamesForTable(distributedTableName);
	if (chosenShards == NULL)
	{
		ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_INTERNALERROR),
						errmsg("Failed to get shards for table"),
						errdetail_log(
							"Failed to get shard names for distributed table %s",
							distributedTableName)));
	}

	MemoryContext targetContext = CurrentMemoryContext;
	if (SPI_connect() != SPI_OK_CONNECT)
	{
		ereport(ERROR, (errmsg("could not connect to SPI manager")));
	}

	/* 步骤2：构造分布式查询
	 *
	 * 我们构造类似于 update_worker 的查询：
	 * SELECT command_node_worker(nodeFunction, nodeFunctionArg, 0, chosenShards, fullyQualified)
	 *   FROM distributedTableName;
	 *
	 * Citus 会应用分布式路由并将查询发送到每个分片。
	 * 在分片规划器的 relpathlisthook 中，我们会重写查询为：
	 * SELECT command_node_worker(nodeFunction, nodeFunctionArg, shardOid, chosenShards, fullyQualified);
	 *
	 * 然后每个分片会验证自己是否匹配 chosenShards 之一：
	 * - 如果匹配，则运行 nodeFunction
	 * - 否则，跳过（noop）
	 *
	 * 这确保了命令在所有承载分片的节点上进行事务性处理，
	 * 但每个节点只运行逻辑一次。
	 *
	 * 我们不在这里创建聚合，以避免分布式聚合的规划开销。
	 * 在 SPI 上下文中分配此字符串，以便在 SPI_Finish() 时释放。
	 */
	StringInfoData s;
	initStringInfo(&s);
	appendStringInfo(&s,
					 "SELECT %s.command_node_worker($1::oid, $2::%s.bson, 0, $3::text[], TRUE, NULL) FROM %s",
					 ApiInternalSchemaNameV2, CoreSchemaNameV2, distributedTableName);
	int nargs = 3;
	Oid argTypes[3] = { OIDOID, BsonTypeId(), TEXTARRAYOID };
	Datum argValues[3] = {
		ObjectIdGetDatum(nodeFunction),
		PointerGetDatum(nodeFunctionArg),
		PointerGetDatum(chosenShards)
	};
	char argNulls[3] = { ' ', ' ', ' ' };

	List *resultList = NIL;

	/* 步骤3：执行分布式查询并收集结果 */
	int tupleCountLimit = 0;
	if (SPI_execute_with_args(s.data, nargs, argTypes, argValues, argNulls,
							  readOnly, tupleCountLimit) != SPI_OK_SELECT)
	{
		ereport(ERROR, (errmsg("could not run SPI query")));
	}

	/* 步骤4：遍历结果集，收集每个节点的返回值 */
	for (uint64 i = 0; i < SPI_processed && SPI_tuptable; i++)
	{
		AttrNumber attrNumber = 1;
		bool isNull = false;
		Datum resultDatum = SPI_getbinval(SPI_tuptable->vals[i],
										  SPI_tuptable->tupdesc, attrNumber, &isNull);
		if (isNull)
		{
			/* 该分片没有处理任何响应，跳过 */
			continue;
		}

		pgbson *resultBson = DatumGetPgBson(resultDatum);
		MemoryContext oldContext = MemoryContextSwitchTo(targetContext);
		pgbson *copiedBson = CopyPgbsonIntoMemoryContext(resultBson, targetContext);
		resultList = lappend(resultList, copiedBson);
		MemoryContextSwitchTo(oldContext);
	}

	SPI_finish();

	/* 步骤5：如果需要，在协调器上也执行命令
	 *
	 * 如果协调器没有该表的分片，我们仍然需要在协调器上执行命令，
	 * 因为 command_node_worker 只在具有表分片的节点上运行。
	 * 我们需要确保元数据和系统目录在协调器上保持一致，
	 * 特别是对于添加节点、重新平衡等管理操作。
	 */
	if (backFillCoordinator && IsMetadataCoordinator() && !CoordinatorHasShardsForTable(
			distributedTableName))
	{
		Datum result = OidFunctionCall1(nodeFunction,
										PointerGetDatum(nodeFunctionArg));
		pgbson *resultBson = DatumGetPgBson(result);
		resultList = lappend(resultList, resultBson);
	}

	return resultList;
}
