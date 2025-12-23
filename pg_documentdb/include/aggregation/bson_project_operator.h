/*-------------------------------------------------------------------------
 * Copyright (c) Microsoft Corporation.  All rights reserved.
 *
 * include/bson/bson_project_operator.h
 *
 * Common declarations of functions for handling projection operators in find queries
 * 用于处理 find 查询中投影操作符的函数公共声明
 *
 * 投影操作符是 MongoDB find 查询中的特殊操作，用于控制返回文档的字段和格式。
 * 常见的投影操作符包括 $、$elemMatch、$slice 等。
 *
 *-------------------------------------------------------------------------
 */

#ifndef BSON_PROJECT_OPERATOR_H
#define BSON_PROJECT_OPERATOR_H

#include "postgres.h"

/* 包含 BSON 表达式评估相关定义 */
#include "operators/bson_expr_eval.h"
/* 包含投影操作相关定义 */
#include "aggregation/bson_project.h"

/*
 * 投影操作符处理函数指针类型
 *
 * 定义处理各种投影操作符的回调函数类型。
 *
 * 参数:
 *   sourceValue - 源值（被投影的字段值）
 *   path - 字段路径
 *   writer - BSON 写入器，用于写入投影结果
 *   projectDocState - 文档投影状态
 *   state - 操作符特定的状态数据
 *   isInNestedArray - 是否在嵌套数组中
 */
typedef void (*ProjectionOpHandlerFunc)(const bson_value_t *sourceValue,
										const StringView *path,
										pgbson_writer *writer,
										ProjectDocumentState *projectDocState,
										void *state, bool isInNestedArray);

/*
 * 投影操作符处理上下文结构
 *
 * 存储投影操作符的处理函数和状态数据。
 */
typedef struct ProjectionOpHandlerContext
{
	/* 投影操作符特定的处理函数 */
	ProjectionOpHandlerFunc projectionOpHandlerFunc;

	/* 操作符特定的状态数据 */
	void *state;
} ProjectionOpHandlerContext;


/*
 * Find 查询投影的路径树函数实现
 *
 * 这组函数覆盖了默认的树遍历行为，专门为 find 查询的投影操作定制。
 *
 * 支持的投影操作符:
 * - $ : 位置操作符，返回数组中第一个匹配查询条件的元素
 * - $elemMatch : 返回数组中第一个匹配指定条件的元素
 * - $slice : 返回数组的切片（子集）
 * - $meta : 返回与文档相关的元数据（如文本搜索分数）
 */
extern BuildBsonPathTreeFunctions FindPathTreeFunctions;


/*
 * GetPathTreeStateForFind - 为 find 查询获取路径树状态
 *
 * 根据查询规范初始化路径树状态，用于处理 find 查询中的投影操作。
 *
 * 参数:
 *   querySpec - 查询规范（BSON 格式），包含查询条件
 *   collationString - 排序规则字符串，用于字符串比较
 *
 * 返回值:
 *   初始化后的路径树状态指针
 *
 * 使用场景:
 * - 处理包含 $ 操作符的投影
 * - 处理包含 $elemMatch 的投影
 * - 需要根据查询条件确定数组元素索引的场景
 */
void * GetPathTreeStateForFind(pgbson *querySpec,
							   const char *collationString);

/*
 * PostProcessStateForFind - find 查询的后处理状态
 *
 * 在构建完路径树后进行后处理，设置必要的函数钩子。
 *
 * 参数:
 *   projectDocumentFuncs - 投影文档函数表，用于设置回调函数
 *   context - 路径树构建上下文
 *
 * 返回值:
 *   返回状态码（0 表示成功，非 0 表示错误）
 *
 * 后处理操作:
 * - 设置处理中间数组节点的函数
 * - 初始化待处理投影状态
 * - 配置投影写入函数
 */
int PostProcessStateForFind(BsonProjectDocumentFunctions *projectDocumentFuncs,
							BuildBsonPathTreeContext *context);

#endif
