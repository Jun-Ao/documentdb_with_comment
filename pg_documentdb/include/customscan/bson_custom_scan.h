/*-------------------------------------------------------------------------
 * Copyright (c) Microsoft Corporation.  All rights reserved.
 *
 * include/customscan/bson_custom_scan.h
 *
 *  Implementation of a custom scan plan.
 *  自定义扫描计划的实现，提供游标扫描、索引扫描优化等功能
 *
 *-------------------------------------------------------------------------
 */

#ifndef BSON_CUSTOM_SCAN_H
#define BSON_CUSTOM_SCAN_H

#include <optimizer/plancat.h>

/*
 * ReplaceExtensionFunctionContext - 替换扩展函数上下文结构体
 * 用于在查询路径替换过程中保存上下文信息
 */

/*
 * UpdatePathsWithExtensionStreamingCursorPlans - 使用扩展流式游标计划更新路径
 * @root: 查询规划器根节点
 * @rel: 关系优化信息
 * @rte: 范围表入口
 * @context: 替换扩展函数上下文
 * 更新查询计划路径，使用扩展的流式游标计划，优化大数据量扫描性能
 */

struct ReplaceExtensionFunctionContext;
bool UpdatePathsWithExtensionStreamingCursorPlans(PlannerInfo *root, RelOptInfo *rel,
												  RangeTblEntry *rte, struct
												  ReplaceExtensionFunctionContext *context);

/*
 * UpdatePathsToForceRumIndexScanToBitmapHeapScan - 强制使用 RUM 索引扫描到位图堆扫描
 * @root: 查询规划器根节点
 * @rel: 关系优化信息
 * 优化查询计划，强制将 RUM 索引扫描转换为位图堆扫描，提升查询效率
 */
void UpdatePathsToForceRumIndexScanToBitmapHeapScan(PlannerInfo *root, RelOptInfo *rel);

/*
 * ReplaceCursorParamValues - 替换游标参数值
 * @query: 查询语句
 * @boundParams: 绑定的参数列表信息
 * 替换游标查询中的参数值为实际绑定的值，支持参数化查询
 */
Query * ReplaceCursorParamValues(Query *query, ParamListInfo boundParams);

/*
 * ValidateCursorCustomScanPlan - 验证游标自定义扫描计划
 * @plan: 执行计划
 * 验证自定义扫描计划的有效性，确保计划符合 DocumentDB 的要求
 */
void ValidateCursorCustomScanPlan(Plan *plan);

/*
 * BuildBaseRelPathTarget - 构建基础关系路径目标
 * @tableRel: 表关系
 * @relIdIndex: 关系 ID 索引
 * 构建基础关系的路径目标，用于后续的查询计划优化
 */
PathTarget * BuildBaseRelPathTarget(Relation tableRel, Index relIdIndex);
#endif
