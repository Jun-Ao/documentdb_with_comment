/*-------------------------------------------------------------------------
 * Copyright (c) Microsoft Corporation.  All rights reserved.
 *
 * include/aggregation/bson_aggregation_statistics.h
 *
 * Exports for the window operator for stage: $setWindowField
 * $setWindowFields 阶段的窗口操作符导出
 *
 * 此文件提供与聚合统计相关的函数声明，主要支持：
 * - 指数移动平均（Exponential Moving Average）的权重解析
 *
 *-------------------------------------------------------------------------
 */
#include "aggregation/bson_aggregation_window_operators.h"

/*
 * ParseInputWeightForExpMovingAvg - 解析指数移动平均的输入权重
 *
 * 参数说明：
 * - opValue: 操作符的 BSON 值（包含 $expMovingAvg 的完整规范）
 * - inputExpression: 输出参数，返回输入表达式
 * - weightExpression: 输出参数，返回权重表达式
 * - decimalWeightValue: 输出参数，返回十进制权重值
 *
 * 返回值：
 * - 成功返回 true，失败返回 false
 *
 * 说明：
 * - $expMovingAvg 是 MongoDB 的窗口函数，计算指数移动平均
 * - 指数移动平均给予近期数据更高的权重
 * - 权重参数（alpha）控制平滑程度，范围 (0, 1)
 */
bool ParseInputWeightForExpMovingAvg(const bson_value_t *opValue,
									 bson_value_t *inputExpression,
									 bson_value_t *weightExpression,
									 bson_value_t *decimalWeightValue);
