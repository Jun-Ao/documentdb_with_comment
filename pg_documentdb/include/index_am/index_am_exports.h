/*-------------------------------------------------------------------------
 * Copyright (c) Microsoft Corporation.  All rights reserved.
 *
 * include/index_am/index_am_exports.h
 *
 * 索引可扩展性通用导出接口
 *
 * 本文件定义了DocumentDB中用于索引可扩展性的核心接口和数据结构，
 * 支持多种BSON索引访问方法的统一管理和动态注册机制。
 *
 *-------------------------------------------------------------------------
 */

#ifndef INDEX_AM_EXPORTS_H
#define INDEX_AM_EXPORTS_H

#include <postgres.h>
#include <utils/rel.h>

struct IndexScanDescData;
struct ExplainState;

/* 尝试解释索引扫描的函数类型
 * 用于将索引特定的解释信息添加到查询解释输出中
 */
typedef void (*TryExplainIndexFunc)(struct IndexScanDescData *scan, struct
									ExplainState *es);

/* 获取多键状态的函数类型
 * 用于检查索引是否支持多键（数组字段的多键索引）
 */
typedef bool (*GetMultikeyStatusFunc)(Relation indexRelation);

/* 获取索引截断状态的函数类型
 * 用于检查索引是否可以被截断（清理过期数据）
 */
typedef bool (*GetTruncationStatusFunc)(Relation indexRelation);

/*
 * BSON索引访问方法条目结构体
 * 用于描述和注册替代的BSON索引访问方法，包含索引能力和各种工具函数
 */
typedef struct
{
	/* 是否支持单路径索引 */
	bool is_single_path_index_supported;

	/* 是否支持唯一索引 */
	bool is_unique_index_supported;

	/* 是否支持通配符索引 */
	bool is_wild_card_supported;

	/* 是否支持复合索引 */
	bool is_composite_index_supported;

	/* 是否支持全文索引 */
	bool is_text_index_supported;

	/* 是否支持哈希索引 */
	bool is_hashed_index_supported;

	/* 是否支持排序索引 */
	bool is_order_by_supported;

	/* 是否支持反向扫描 */
	bool is_backwards_scan_supported;

	/* 是否支持仅索引扫描 */
	bool is_index_only_scan_supported;

	/* 是否支持并行扫描 */
	bool can_support_parallel_scans;

	/* 获取访问方法OID的函数 */
	Oid (*get_am_oid)(void);

	/* 获取单路径操作符族OID的函数 */
	Oid (*get_single_path_op_family_oid)(void);

	/* 获取复合路径操作符族OID的函数 */
	Oid (*get_composite_path_op_family_oid)(void);

	/* 获取文本路径操作符族OID的函数 */
	Oid (*get_text_path_op_family_oid)(void);

	/* 获取哈希路径操作符族OID的函数 */
	Oid (*get_hashed_path_op_family_oid)(void);

	/* 获取唯一路径操作符族OID的函数 */
	Oid (*get_unique_path_op_family_oid)(void);

	/* 可选函数：添加解释输出信息 */
	TryExplainIndexFunc add_explain_output;

	/* 索引访问方法名称（用于创建索引） */
	const char *am_name;

	/* 操作符类的主目录模式名称 */
	const char *(*get_opclass_catalog_schema)(void);

	/* 操作符类的替代内部模式名称（如果不是目录模式） */
	const char *(*get_opclass_internal_catalog_schema)(void);

	/* 可选函数：处理获取索引的多键状态 */
	GetMultikeyStatusFunc get_multikey_status;

	/* 可选函数：返回索引的截断状态 */
	GetTruncationStatusFunc get_truncation_status;
} BsonIndexAmEntry;

/*
 * 在系统启动时注册BSON索引访问方法
 * 此函数用于将新的索引访问方法注册到DocumentDB系统中
 * 输入参数：
 *   - indexAmEntry: BSON索引访问方法条目，包含索引的所有相关信息
 */
void RegisterIndexAm(BsonIndexAmEntry indexAmEntry);

#endif
