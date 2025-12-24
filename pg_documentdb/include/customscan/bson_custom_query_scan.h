/*-------------------------------------------------------------------------
 * Copyright (c) Microsoft Corporation.  All rights reserved.
 *
 * include/customscan/bson_custom_query_scan.h
 *
 *  Implementation of a custom scan plan.
 *  自定义扫描计划的实现，用于优化文本查询和向量查询
 *
 *-------------------------------------------------------------------------
 */

#ifndef BSON_CUSTOM_QUERY_SCAN_H
#define BSON_CUSTOM_QUERY_SCAN_H

#include <optimizer/plancat.h>
#include <utils/builtins.h>
#include <utils/varlena.h>
#include <opclass/bson_index_support.h>

/*
 * AddExtensionQueryScanForTextQuery - 为文本查询添加扩展查询扫描
 * @root: 查询规划器根节点
 * @rel: 关系优化信息
 * @rte: 范围表入口
 * @textIndexOptions: 文本索引选项数据
 * 为文本查询添加自定义扫描计划，利用索引优化文本搜索性能
 */
void AddExtensionQueryScanForTextQuery(PlannerInfo *root, RelOptInfo *rel,
									   RangeTblEntry *rte,
									   QueryTextIndexData *textIndexOptions);

/*
 * AddExtensionQueryScanForVectorQuery - 为向量查询添加扩展查询扫描
 * @root: 查询规划器根节点
 * @rel: 关系优化信息
 * @rte: 范围表入口
 * @searchQueryData: 搜索查询评估数据
 * 为向量相似性搜索添加自定义扫描计划，优化向量查询性能
 */
void AddExtensionQueryScanForVectorQuery(PlannerInfo *root, RelOptInfo *rel,
										 RangeTblEntry *rte,
										 const SearchQueryEvalData *searchQueryData);

/*
 * AddExplainCustomScanWrapper - 添加自定义扫描的解释包装器
 * @root: 查询规划器根节点
 * @rel: 关系优化信息
 * @rte: 范围表入口
 * 为自定义扫描添加 EXPLAIN 支持，帮助调试和优化查询计划
 */
void AddExplainCustomScanWrapper(PlannerInfo *root, RelOptInfo *rel,
								 RangeTblEntry *rte);
#endif
