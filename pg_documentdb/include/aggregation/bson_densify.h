/*-------------------------------------------------------------------------
 * Copyright (c) Microsoft Corporation.  All rights reserved.
 *
 * include/aggregation/bson_densify.h
 *
 * Common declarations of functions for handling $densify stage.
 * 用于处理 $densify 聚合阶段的函数公共声明
 *
 * $densify 是 MongoDB 5.1+ 引入的聚合阶段，用于在时间序列数据中填充缺失的时间点。
 * 这在处理不规则采样的时间序列数据时非常有用，可以确保输出结果具有连续的时间间隔。
 *
 *-------------------------------------------------------------------------
 */

#ifndef BSON_DENSIFY_H
#define BSON_DENSIFY_H


/* 包含 BSON 核心数据结构和操作定义 */
#include "io/bson_core.h"
/* 包含聚合管道相关定义 */
#include "aggregation/bson_aggregation_pipeline.h"

/* 包含聚合管道私有定义 */
#include "aggregation/bson_aggregation_pipeline_private.h"

/*
 * HandleDensify - 处理 $densify 聚合阶段
 *
 * 此函数负责在时间序列数据中填充缺失的时间点，确保输出结果具有连续的时间间隔。
 *
 * 参数:
 *   existingValue - 当前已存在的值（来自之前的聚合阶段）
 *   query - 当前的查询对象，用于构建聚合管道
 *   context - 聚合管道构建上下文，包含状态和配置信息
 *
 * 返回值:
 *   返回更新后的 Query 对象指针
 *
 * $densify 阶段特点:
 * - field: 指定要填充的时间序列字段
 * - range: 指定时间范围的边界（startTime, endTime）
 * - step: 指定填充的时间间隔（如 1 hour, 1 day）
 * - unit: 可选的时间单位（如 hour, day, minute, second）
 * - partitionByFields: 可选的分组字段，按这些字段分别进行填充
 *
 * 应用场景:
 * - 时间序列数据的可视化（确保图表连续）
 * - 缺失数据的插值
 * - 监控数据的趋势分析
 */
Query * HandleDensify(const bson_value_t *existingValue, Query *query,
					  AggregationPipelineBuildContext *context);

#endif
