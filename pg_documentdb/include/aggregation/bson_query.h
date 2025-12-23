/*-------------------------------------------------------------------------
 * Copyright (c) Microsoft Corporation.  All rights reserved.
 *
 * include/bson/bson_query.h
 *
 * Common declarations of functions for handling bson queries.
 * 用于处理 BSON 查询操作的函数公共声明
 *
 * 提供了遍历和处理 BSON 查询文档的核心功能，用于 find、update、delete 等操作。
 *
 *-------------------------------------------------------------------------
 */

#ifndef BSON_QUERY_H
#define BSON_QUERY_H


/* 包含 BSON 核心数据结构和操作定义 */
#include "io/bson_core.h"

/*
 * 处理查询值的函数指针类型
 *
 * 当处理叶子查询过滤器（如 "a.b": <value>）时调用的回调函数类型。
 *
 * 参数:
 *   context - 用户提供的上下文数据
 *   path - 字段路径（如 "a.b"）
 *   value - 查询条件值（可以是字面值或查询操作符）
 */
typedef void (*ProcessQueryValueFunc)(void *context, const char *path, const
									  bson_value_t *value);

/*
 * 处理查询过滤器的函数指针类型
 *
 * 在处理完查询文档后调用的回调函数类型，用于完成查询处理的收尾工作。
 *
 * 参数:
 *   context - 用户提供的上下文数据
 */
typedef void (*ProcessQueryFilterFunc)(void *context);

/*
 * TraverseQueryDocumentAndGetId - 遍历查询文档并获取 _id 值
 *
 * 从查询文档中提取 _id 字段的值，这是 MongoDB 中最常用的查询优化。
 * 如果查询只包含 _id，可以直接使用主键索引快速定位文档。
 *
 * 参数:
 *   queryDocument - 查询文档迭代器
 *   idValue - 输出参数，用于存储提取的 _id 值
 *   errorOnConflict - 当查询中既有 _id 又有其他字段时是否报错
 *   hasNonIdFields - 输出参数，指示查询中是否包含非 _id 字段
 *   isIdValueCollationAware - 输出参数，指示 _id 值是否需要排序规则感知
 *
 * 返回值:
 *   true 表示成功提取 _id，false 表示查询中没有 _id 字段
 *
 * 使用场景:
 * - find 查询优化：如果查询只包含 _id，可以直接使用主键查找
 * - update 操作优化：_id 查询可以直接定位到文档
 * - delete 操作优化：_id 查询可以直接删除文档
 *
 * 查询示例:
 *   { _id: ObjectId("...") }     -> 提取 ObjectId
 *   { _id: { $eq: "value" } }    -> 提取 "value"
 *   { _id: "value", name: "test" } -> 根据 errorOnConflict 决定是否报错
 */
bool TraverseQueryDocumentAndGetId(bson_iter_t *queryDocument,
								   bson_value_t *idValue, bool errorOnConflict,
								   bool *hasNonIdFields, bool *isIdValueCollationAware);

/*
 * TraverseQueryDocumentAndProcess - 遍历查询文档并处理每个查询条件
 *
 * 通用的查询文档遍历函数，对每个查询条件调用用户提供的回调函数。
 * 这是最底层的查询处理函数，被所有查询操作使用。
 *
 * 参数:
 *   queryDocument - 查询文档迭代器
 *   context - 用户提供的上下文数据，会传递给回调函数
 *   processValueFunc - 处理查询值的回调函数
 *   processFilterFunc - 处理查询过滤器的回调函数（可选）
 *   isUpsert - 是否为 upsert 操作（影响查询语义）
 *
 * 功能:
 * - 递归遍历嵌套的查询文档
 * - 处理顶层查询操作符（$and, $or, $not 等）
 * - 处理字段级别的查询操作符（$eq, $gt, $lt 等）
 * - 支持点号路径（如 "a.b.c"）
 *
 * 查询文档示例:
 *   { "age": { $gte: 18, $lt: 65 } }
 *   { $or: [ { "status": "active" }, { "verified": true } ] }
 *   { "name": "John", "age": { $gt: 25 } }
 */
void TraverseQueryDocumentAndProcess(bson_iter_t *queryDocument, void *context,
									 ProcessQueryValueFunc processValueFunc,
									 ProcessQueryFilterFunc processFilterFunc,
									 bool isUpsert);

#endif
