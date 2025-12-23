/*-------------------------------------------------------------------------
 * Copyright (c) Microsoft Corporation.  All rights reserved.
 *
 * include/api_hooks.h
 *
 * Exports related to hooks for the public API surface that enable distribution.
 * 与支持分布式功能的公共 API 表面钩子相关的导出
 *
 *-------------------------------------------------------------------------
 */

#ifndef EXTENSION_API_HOOKS_COMMON_H
#define EXTENSION_API_HOOKS_COMMON_H

#include <postgres.h>
#include "io/bson_core.h"

/*
 * Represents a single row result that is
 * 表示在远程节点上执行的单行结果
 * executed on a remote node.
 * 在分布式环境中，命令可能在远程节点执行，此结构表示返回的结果
 */
typedef struct DistributedRunCommandResult
{
	/* The node that responded for the row */
	/* 响应该行的节点 ID */
	/* 在分布式集群中，标识执行命令的节点 */
	int nodeId;

	/* Whether or not the node succeeded in running the command */
	/* 节点是否成功运行命令 */
	/* true = 成功，false = 失败 */
	bool success;

	/* the response (string error, or string coerced value) */
	/* 响应内容（字符串形式的错误信息或字符串形式的强制转换值） */
	/* 如果 success=true，包含返回值；如果 success=false，包含错误信息 */
	text *response;
} DistributedRunCommandResult;


/* Private: Feature flag for update tracking */
/* 私有：更新跟踪的功能标志 */
/* BsonUpdateTracker 用于跟踪 BSON 文档的更新操作 */
typedef struct BsonUpdateTracker BsonUpdateTracker;
#endif
