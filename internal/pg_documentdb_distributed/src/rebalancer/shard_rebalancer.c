/*-------------------------------------------------------------------------
 * Copyright (c) Microsoft Corporation.  All rights reserved.
 *
 * src/rebalancer/shard_rebalancer.c
 *
 * Implementation of a set of APIs for cluster rebalancing
 * 实现一组用于集群重新平衡的 API
 *
 *-------------------------------------------------------------------------
 */
#include "postgres.h"
#include "fmgr.h"
#include "miscadmin.h"
#include "utils/builtins.h"
#include "utils/resowner.h"
#include "lib/stringinfo.h"
#include "access/xact.h"
#include "utils/typcache.h"
#include "parser/parse_type.h"
#include "nodes/makefuncs.h"

#include "io/bson_core.h"
#include "utils/documentdb_errors.h"
#include "utils/query_utils.h"
#include "commands/parse_error.h"
#include "metadata/metadata_cache.h"
#include "api_hooks.h"

extern bool EnableShardRebalancer;

/* PostgreSQL 函数信息宏声明
 * 这些宏将 C 函数注册为 PostgreSQL 可调用的函数
 */
PG_FUNCTION_INFO_V1(command_rebalancer_status);
PG_FUNCTION_INFO_V1(command_rebalancer_start);
PG_FUNCTION_INFO_V1(command_rebalancer_stop);


/* 静态辅助函数声明 */
static void PopulateRebalancerRowsFromResponse(pgbson_writer *responseWriter,
											   pgbson *result);

static bool HasActiveRebalancing(void);
static char * GetRebalancerStrategy(pgbson *startArgs);

/*
 * command_rebalancer_status - 获取分片重新平衡器的状态
 *
 * 该函数返回当前分片重新平衡的状态信息，包括：
 * - mode: 当前模式（"off" 或 "full"）
 * - runningJobs: 正在运行的重新平衡任务列表
 * - otherJobs: 其他已完成的重新平衡任务列表
 * - strategies: 可用的重新平衡策略列表
 *
 * 返回值：包含状态信息的 BSON 文档
 */
Datum
command_rebalancer_status(PG_FUNCTION_ARGS)
{
	/* 检查是否启用了分片重新平衡功能 */
	if (!EnableShardRebalancer)
	{
		ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_COMMANDNOTSUPPORTED),
						errmsg("rebalancer_status is not supported yet")));
	}

	bool readOnly = true;
	bool isNull = false;

	/* 查询 Citus 的重新平衡状态
	 * r1 CTE: 从 citus_rebalance_status() 获取状态，构造包含任务详情的对象
	 * r2 CTE: 将所有任务聚合成一个数组
	 * 最终查询: 将 JSONB 转换为 BSON 格式返回
	 */
	Datum result = ExtensionExecuteQueryViaSPI(
		FormatSqlQuery(
			"WITH r1 AS (SELECT jsonb_build_object("
			"  'state', state::text,"
			"  'startedAt', started_at::text,"
			"  'finishedAt', finished_at::text,"
			"  'task_state_counts', details->'task_state_counts',"
			"  'tasks', (SELECT jsonb_agg(task - 'LSN' - 'command' - 'hosts' - 'task_id') FROM jsonb_array_elements(details->'tasks') AS task)) AS obj"
			" FROM citus_rebalance_status()),"
			" r2 AS (SELECT jsonb_build_object('rows', jsonb_agg(r1.obj)) AS obj FROM r1)"
			" SELECT %s.bson_json_to_bson(r2.obj::text) FROM r2",
			CoreSchemaName), readOnly, SPI_OK_SELECT, &isNull);

	pgbson_writer responseWriter;
	PgbsonWriterInit(&responseWriter);
	if (isNull)
	{
		/* 没有重新平衡结果，设置为 "off" 模式 */
		PgbsonWriterAppendUtf8(&responseWriter, "mode", 4, "off");
	}
	else
	{
		/* 解析并填充重新平衡的任务信息 */
		PopulateRebalancerRowsFromResponse(&responseWriter, DatumGetPgBson(result));
	}

	/* 查询可用的重新平衡策略列表 */
	result = ExtensionExecuteQueryViaSPI(
		FormatSqlQuery(
			"WITH r1 AS (SELECT jsonb_build_object('strategy_name', name, 'isDefault', default_strategy) AS obj FROM pg_dist_rebalance_strategy),"
			" r2 AS (SELECT jsonb_build_object('strategies', jsonb_agg(r1.obj)) AS obj FROM r1)"
			" SELECT %s.bson_json_to_bson(r2.obj::text) FROM r2",
			CoreSchemaName), readOnly, SPI_OK_SELECT, &isNull);

	if (!isNull)
	{
		/* 将策略信息合并到响应中 */
		PgbsonWriterConcat(&responseWriter, DatumGetPgBson(result));
	}

	/* 添加成功标志 */
	PgbsonWriterAppendDouble(&responseWriter, "ok", 2, 1);

	PG_RETURN_POINTER(PgbsonWriterGetPgbson(&responseWriter));
}


/*
 * command_rebalancer_start - 启动分片重新平衡
 *
 * 该函数启动分片重新平衡过程，用于在集群中重新分布分片以实现负载均衡。
 *
 * 参数：
 * - startArgs (BSON): 启动参数，可包含：
 *   - strategy: 重新平衡策略名称（可选）
 *
 * 执行前检查：
 * 1. 确保分片重新平衡功能已启用
 * 2. 确保没有启用变更流功能（与重新平衡不兼容）
 * 3. 确保没有正在运行的重新平衡任务
 *
 * 返回值：包含成功标志的 BSON 文档
 */
Datum
command_rebalancer_start(PG_FUNCTION_ARGS)
{
	/* 检查是否启用了分片重新平衡功能 */
	if (!EnableShardRebalancer)
	{
		ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_COMMANDNOTSUPPORTED),
						errmsg("starting the shard rebalancer is not supported yet")));
	}

	/* 检查是否启用了变更流功能
	 * 变更流与重新平衡不兼容，因为重新平衡会移动分片，
	 * 可能导致变更流的连续性中断
	 */
	if (IsChangeStreamFeatureAvailableAndCompatible())
	{
		ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_COMMANDNOTSUPPORTED),
						errmsg(
							"starting the shard rebalancer is not supported when change streams is enabled")));
	}

	pgbson *startArgs = PG_GETARG_PGBSON(0);

	/* 检查是否已有正在运行的重新平衡任务 */
	if (HasActiveRebalancing())
	{
		ereport(ERROR, (errcode(
							ERRCODE_DOCUMENTDB_BACKGROUNDOPERATIONINPROGRESSFORNAMESPACE),
						errmsg(
							"Cannot start rebalancing when another rebalancing is in progress")));
	}

	/* 获取用户指定的重新平衡策略（如果有） */
	char *rebalancerStrategyName = GetRebalancerStrategy(startArgs);

	bool readOnly = false;
	bool isNull = false;

	/* 如果指定了策略，则设置为默认策略 */
	if (rebalancerStrategyName != NULL)
	{
		Oid argTypes[1] = { TEXTOID };
		Datum argValues[1] = { CStringGetTextDatum(rebalancerStrategyName) };
		ExtensionExecuteQueryWithArgsViaSPI(
			"SELECT citus_set_default_rebalance_strategy($1)", 1,
			argTypes, argValues, NULL, readOnly,
			SPI_OK_SELECT, &isNull);
	}

	/* 启动 Citus 重新平衡过程 */
	ExtensionExecuteQueryViaSPI("SELECT citus_rebalance_start()", readOnly, SPI_OK_SELECT,
								&isNull);

	/* 返回成功响应 */
	pgbson_writer responseWriter;
	PgbsonWriterInit(&responseWriter);
	PgbsonWriterAppendDouble(&responseWriter, "ok", 2, 1);
	PG_RETURN_POINTER(PgbsonWriterGetPgbson(&responseWriter));
}


/*
 * command_rebalancer_stop - 停止分片重新平衡
 *
 * 该函数停止当前正在运行的分片重新平衡过程。
 *
 * 执行逻辑：
 * 1. 检查是否有活动的重新平衡任务
 * 2. 如果有，调用 Citus 的停止函数
 * 3. 返回停止操作的结果
 *
 * 返回值：包含以下字段的 BSON 文档：
 * - wasActive: 停止前是否有活动的重新平衡任务
 * - ok: 操作是否成功
 */
Datum
command_rebalancer_stop(PG_FUNCTION_ARGS)
{
	/* 检查是否启用了分片重新平衡功能 */
	if (!EnableShardRebalancer)
	{
		ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_COMMANDNOTSUPPORTED),
						errmsg("stopping the rebalancer is not supported yet")));
	}

	/* 检查是否有活动的重新平衡任务 */
	bool hasActiveJobs = HasActiveRebalancing();
	if (hasActiveJobs)
	{
		/* 如果有活动任务，调用 Citus 的停止函数 */
		bool readOnly = false;
		bool isNull = false;
		ExtensionExecuteQueryViaSPI("SELECT citus_rebalance_stop()", readOnly,
									SPI_OK_SELECT,
									&isNull);
	}

	/* 构造响应，返回是否有活动任务和成功标志 */
	pgbson_writer responseWriter;
	PgbsonWriterInit(&responseWriter);
	PgbsonWriterAppendBool(&responseWriter, "wasActive", 9, hasActiveJobs);
	PgbsonWriterAppendDouble(&responseWriter, "ok", 2, 1);
	PG_RETURN_POINTER(PgbsonWriterGetPgbson(&responseWriter));
}


/*
 * PopulateRebalancerRowsFromResponse - 从 Citus 响应填充 DocumentDB 兼容的重新平衡状态
 * @responseWriter: BSON 写入器，用于构造响应
 * @result: Citus 返回的重新平衡状态数据
 *
 * 该函数将 Citus 的重新平衡状态转换为 DocumentDB 兼容的格式：
 * {
 *    "mode": "off|full",  // 根据是否有正在运行的任务决定
 *    "runningJobs": [ ... ],  // 状态为 scheduled 或 running 的任务
 *    "otherJobs": [ ... ],    // 其他状态的任务
 * }
 *
 * 分片逻辑：
 * - 状态为 "scheduled" 或 "running" 的任务放入 runningJobs
 * - 其他状态的任务放入 otherJobs
 * - 如果有 runningJobs，mode 设置为 "full"，否则为 "off"
 */
static void
PopulateRebalancerRowsFromResponse(pgbson_writer *responseWriter, pgbson *result)
{
	pgbsonelement element;
	PgbsonToSinglePgbsonElement(result, &element);

	if (element.bsonValue.value_type == BSON_TYPE_NULL)
	{
		/* 如果结果为空，设置为 "off" 模式 */
		PgbsonWriterAppendUtf8(responseWriter, "mode", 4, "off");
		return;
	}

	/* 验证结果必须是数组类型 */
	if (element.bsonValue.value_type != BSON_TYPE_ARRAY)
	{
		ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_INTERNALERROR),
						errmsg("shard rebalancer response should be an array, not %s",
							   BsonTypeName(element.bsonValue.value_type)),
						errdetail_log(
							"shard rebalancer response should be an array, not %s",
							BsonTypeName(
								element.bsonValue.value_type))));
	}

	/* 遍历任务数组，根据状态分类 */
	bson_iter_t rowsIter;
	BsonValueInitIterator(&element.bsonValue, &rowsIter);

	List *runningJobs = NIL;
	List *otherJobs = NIL;
	while (bson_iter_next(&rowsIter))
	{
		const bson_value_t *arrayValue = bson_iter_value(&rowsIter);
		if (arrayValue->value_type != BSON_TYPE_DOCUMENT)
		{
			ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_INTERNALERROR),
							errmsg(
								"shard rebalancer array element should be a document, not %s",
								BsonTypeName(
									arrayValue->value_type)),
							errdetail_log(
								"shard rebalancer array element should be a document, not %s",
								BsonTypeName(
									arrayValue->value_type))));
		}

		/* 复制任务值（需要持久化） */
		bson_value_t *arrayValueCopy = palloc(sizeof(bson_value_t));
		*arrayValueCopy = *arrayValue;

		/* 检查任务状态 */
		bson_iter_t arrayValueIter;
		BsonValueInitIterator(arrayValue, &arrayValueIter);
		if (bson_iter_find(&arrayValueIter, "state"))
		{
			const char *stateValue = bson_iter_utf8(&arrayValueIter, NULL);
			if (strcmp(stateValue, "scheduled") == 0 ||
				strcmp(stateValue, "running") == 0)
			{
				/* 正在运行或已调度的任务放入 runningJobs */
				runningJobs = lappend(runningJobs, arrayValueCopy);
			}
			else
			{
				/* 其他状态的任务放入 otherJobs */
				otherJobs = lappend(otherJobs, arrayValueCopy);
			}
		}
		else
		{
			/* 没有状态字段的任务放入 otherJobs */
			otherJobs = lappend(otherJobs, arrayValueCopy);
		}
	}

	/* 根据是否有运行中的任务设置模式 */
	char *status = list_length(runningJobs) > 0 ? "full" : "off";
	PgbsonWriterAppendUtf8(responseWriter, "mode", 4, status);

	/* 添加正在运行的任务数组 */
	pgbson_array_writer jobsWriter;
	PgbsonWriterStartArray(responseWriter, "runningJobs", 11, &jobsWriter);

	ListCell *jobCell;
	foreach(jobCell, runningJobs)
	{
		bson_value_t *value = lfirst(jobCell);
		PgbsonArrayWriterWriteValue(&jobsWriter, value);
	}

	PgbsonWriterEndArray(responseWriter, &jobsWriter);

	/* 添加其他任务数组 */
	PgbsonWriterStartArray(responseWriter, "otherJobs", 9, &jobsWriter);

	foreach(jobCell, otherJobs)
	{
		bson_value_t *value = lfirst(jobCell);
		PgbsonArrayWriterWriteValue(&jobsWriter, value);
	}

	PgbsonWriterEndArray(responseWriter, &jobsWriter);
}


/*
 * HasActiveRebalancing - 检查是否有活动的重新平衡任务
 *
 * 该函数查询 Citus 的重新平衡状态，检查是否有处于以下状态的任务：
 * - scheduled: 已调度但未开始
 * - running: 正在运行
 * - cancelling: 正在取消
 * - failing: 正在失败
 *
 * 返回值：如果有活动任务返回 true，否则返回 false
 */
static bool
HasActiveRebalancing(void)
{
	bool readOnly = true;
	bool isNull = false;
	Datum result = ExtensionExecuteQueryViaSPI(
		"SELECT COUNT(*)::int4 FROM citus_rebalance_status() WHERE state::text IN ('scheduled', 'running', 'cancelling', 'failing')",
		readOnly, SPI_OK_SELECT, &isNull);

	return !isNull && DatumGetInt32(result) > 0;
}


/*
 * GetRebalancerStrategy - 从启动参数中获取重新平衡策略名称
 * @startArgs: 启动参数（BSON 格式）
 *
 * 该函数从启动参数中提取 "strategy" 字段的值。
 * 如果未指定策略，返回 NULL，将使用默认策略。
 *
 * 返回值：策略名称的副本（需要调用者释放），如果没有指定则返回 NULL
 */
static char *
GetRebalancerStrategy(pgbson *startArgs)
{
	bson_iter_t argsIter;
	PgbsonInitIterator(startArgs, &argsIter);
	while (bson_iter_next(&argsIter))
	{
		const char *key = bson_iter_key(&argsIter);
		if (strcmp(key, "strategy") == 0)
		{
			/* 找到 strategy 字段，验证类型并返回其值 */
			EnsureTopLevelFieldType("strategy", &argsIter, BSON_TYPE_UTF8);
			return bson_iter_dup_utf8(&argsIter, NULL);
		}
	}

	/* 没有找到 strategy 字段，返回 NULL（使用默认策略） */
	return NULL;
}
