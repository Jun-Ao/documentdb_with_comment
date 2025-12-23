/*-------------------------------------------------------------------------
 * Copyright (c) Microsoft Corporation.  All rights reserved.
 *
 * include/aggregation/bson_bucket_auto.h
 *
 * Common declarations of functions for handling $bucketAuto stage.
 * 用于处理 $bucketAuto 聚合阶段的函数公共声明
 *
 * $bucketAuto 是 MongoDB 聚合管道中的一个阶段，用于将文档自动分组到指定数量的桶中。
 * 与 $bucket 不同，$bucketAuto 会自动确定桶的边界，确保每个桶中的文档数量大致相等。
 *
 *-------------------------------------------------------------------------
 */

#ifndef BSON_BUCKETAUTO_H
#define BSON_BUCKETAUTO_H

/* 包含 BSON 核心数据结构和操作定义 */
#include "io/bson_core.h"
/* 包含聚合管道相关定义 */
#include "aggregation/bson_aggregation_pipeline.h"
/* 包含聚合管道私有定义 */
#include "aggregation/bson_aggregation_pipeline_private.h"

/*
 * HandleBucketAuto - 处理 $bucketAuto 聚合阶段
 *
 * 此函数负责将输入文档自动分组到指定数量的桶中，自动计算桶边界。
 *
 * 参数:
 *   existingValue - 当前已存在的值（来自之前的聚合阶段）
 *   query - 当前的查询对象，用于构建聚合管道
 *   context - 聚合管道构建上下文，包含状态和配置信息
 *
 * 返回值:
 *   返回更新后的 Query 对象指针
 *
 * $bucketAuto 阶段特点:
 * - 自动确定桶边界，不需要手动指定 boundaries
 * - 确保每个桶中的文档数量大致相等（使用优化算法）
 * - 可以指定 output 字段来控制每个桶的输出格式
 * - 支持按指定字段进行排序后再分组
 */
Query * HandleBucketAuto(const bson_value_t *existingValue, Query *query,
						 AggregationPipelineBuildContext *context);

#endif
