/*-------------------------------------------------------------------------
 * Copyright (c) Microsoft Corporation.  All rights reserved.
 *
 * include/aggregation/bson_aggregation_window_operators.h
 *
 * Exports for the window operator for stage: $setWindowField
 * $setWindowFields 阶段的窗口操作符导出
 *
 * 此文件定义了 MongoDB 窗口函数 $setWindowFields 的处理函数。
 * 窗口函数允许在文档分区内执行跨行计算，类似于 SQL 的窗口函数。
 *
 * 支持的窗口函数包括：
 * - $sum, $avg, $min, $max: 基础聚合函数
 * - $rank, $denseRank, $documentNumber: 排名函数
 * - $shift: 移位函数（访问相邻行的值）
 * - $expMovingAvg: 指数移动平均
 * - $derivitive: 导数（变化率）
 * - $integral: 积分
 *
 *-------------------------------------------------------------------------
 */

#ifndef BSON_AGGREGATION_WINDOW_OPERATORS_H
#define BSON_AGGREGATION_WINDOW_OPERATORS_H

#include <nodes/parsenodes.h>

#include "io/bson_core.h"
#include "metadata/metadata_cache.h"
#include "aggregation/bson_aggregation_pipeline.h"

#include "aggregation/bson_aggregation_pipeline_private.h"

/*
 * HandleSetWindowFields - 处理 $setWindowFields 阶段
 *
 * 参数说明：
 * - existingValue: $setWindowFields 阶段的 BSON 规范
 * - query: 当前查询树
 * - context: 管道构建上下文
 *
 * 返回值：
 * - 返回添加了窗口函数的查询树
 *
 * 说明：
 * - $setWindowFields 是 MongoDB 的窗口函数阶段
 * - 允许在不使用 $group 的情况下执行跨行计算
 * - 支持分区（PARTITION BY）、排序（ORDER BY）和窗口边界
 */
Query * HandleSetWindowFields(const bson_value_t *existingValue, Query *query,
							  AggregationPipelineBuildContext *context);

/*
 * HandleSetWindowFieldsCore - $setWindowFields 的核心实现
 *
 * 参数说明：
 * - existingValue: $setWindowFields 阶段的 BSON 规范
 * - query: 当前查询树
 * - context: 管道构建上下文
 * - partitionByExpr: 分区表达式（如果已预先计算）
 * - enableInternalWindowOperator: 是否启用内部窗口操作符优化
 *
 * 返回值：
 * - 返回添加了窗口函数的查询树
 *
 * 说明：
 * - 这是 HandleSetWindowFields 的内部实现
 * - 支持外部提供分区表达式以优化性能
 * - enableInternalWindowOperator 控制是否使用 DocumentDB 特定的优化
 */
Query * HandleSetWindowFieldsCore(const bson_value_t *existingValue,
								  Query *query,
								  AggregationPipelineBuildContext *context,
								  Expr *partitionByExpr,
								  bool enableInternalWindowOperator);

#endif /* BSON_AGGREGATION_WINDOW_OPERATORS_H */
