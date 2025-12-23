/*-------------------------------------------------------------------------
 * Copyright (c) Microsoft Corporation.  All rights reserved.
 *
 * include/bson/bson_positional_query.h
 *
 * Declarations of functions for the BSON Positional $ operator.
 * 用于处理 BSON 位置操作符 $ 的函数声明
 *
 * 位置操作符 $ 是 MongoDB 中用于更新和投影操作的特殊操作符，
 * 它根据查询条件匹配数组中的第一个元素，然后对该元素进行操作。
 *
 *-------------------------------------------------------------------------
 */

#ifndef BSON_POSITIONAL_QUERY_H
#define BSON_POSITIONAL_QUERY_H

/* 前向声明：位置查询数据结构 */
typedef struct BsonPositionalQueryData BsonPositionalQueryData;

/*
 * 位置查询数据结构
 *
 * 存储在顶级查询中找到的每个限定符的 BsonPositionalQueryQual 列表。
 * 用于追踪位置操作符 $ 需要匹配的数组元素位置。
 */
typedef struct BsonPositionalQueryData
{
	/* 查询限定符列表，存储所有需要匹配的查询条件 */
	List *queryQuals;
} BsonPositionalQueryData;

/*
 * GetPositionalQueryData - 从查询条件中提取位置查询数据
 *
 * 解析 BSON 查询对象，提取与位置操作符 $ 相关的查询条件，
 * 构建位置查询数据结构。
 *
 * 参数:
 *   query - BSON 格式的查询条件对象
 *   collationString - 排序规则字符串，用于字符串比较时的排序规则
 *
 * 返回值:
 *   返回新分配的 BsonPositionalQueryData 对象指针
 *
 * 使用场景:
 * - 在 find 查询中使用 $ 操作符进行投影时
 * - 在 update 操作中使用 $ 操作符更新数组元素时
 *
 * 示例:
 *   查询: { "grades": { $gte: 80 } }
 *   投影: { "grades.$": 1 }
 *   此函数会解析查询条件，确定哪些数组元素匹配条件
 */
BsonPositionalQueryData * GetPositionalQueryData(const bson_value_t *query,
												 const char *collationString);

/*
 * MatchPositionalQueryAgainstDocument - 将位置查询与文档进行匹配
 *
 * 根据位置查询条件，检查文档是否匹配，并返回匹配的数组元素索引。
 *
 * 参数:
 *   data - 位置查询数据结构，包含查询条件
 *   document - 要匹配的 BSON 文档
 *
 * 返回值:
 *   返回匹配的数组元素索引（从 0 开始）
 *   如果没有匹配的元素，返回 -1
 *
 * 匹配逻辑:
 * - 遍历文档中的数组元素
 * - 对每个元素应用查询条件进行匹配
 * - 返回第一个匹配元素的索引
 * - 如果数组为空或没有匹配元素，返回 -1
 *
 * 使用示例:
 *   文档: { "grades": [85, 90, 75] }
 *   查询: { "grades": { $gte: 80 } }
 *   结果: 返回 0（第一个匹配元素的索引）
 */
int32_t MatchPositionalQueryAgainstDocument(const BsonPositionalQueryData *data,
											const pgbson *document);

#endif
