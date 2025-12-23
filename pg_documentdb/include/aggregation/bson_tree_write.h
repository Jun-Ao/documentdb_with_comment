/*-------------------------------------------------------------------------
 * Copyright (c) Microsoft Corporation.  All rights reserved.
 *
 * include/bson/bson_tree_write.h
 *
 * Common declarations of functions to write components of a tree.
 * 用于写入树组件的通用函数声明
 *
 *-------------------------------------------------------------------------
 */
#include "aggregation/bson_tree_common.h"

#ifndef BSON_TREE_WRITE_H
#define BSON_TREE_WRITE_H

/* Forward declaration of data types */
/* 数据类型的前向声明 */

/* Hook function that returns true if the node should be skipped */
/* 钩子函数，如果节点应该被跳过则返回 true */
/* when writing a tree to a writer. */
/* 在将树写入写入器时 */
typedef bool (*FilterNodeForWriteFunc)(void *state, int index);
// 函数指针类型：用于在写入树时过滤节点
// 参数：
//   state - 用户自定义的状态数据
//   index - 节点索引
// 返回：true 表示跳过该节点，false 表示写入该节点

/* The write context holding the function hooks and current */
/* 写入上下文，包含用于写入树的函数钩子和当前 */
/* write state that is used to write the tree. */
/* 写入状态 */
typedef struct WriteTreeContext
{
	/* Pointer to a state that is passed to the filter function. */
	/* 传递给过滤函数的状态指针 */
	void *state;

	/* Callback function to determine whether the node should be skipped */
	/* 回调函数，用于确定是否应该跳过该节点 */
	FilterNodeForWriteFunc filterNodeFunc;

	/* Whether we should write null when a path expression is found and the path is undefined on the source document. */
	/* 当找到路径表达式但源文档上未定义该路径时，是否写入 null 值 */
	/* 例如：投影 { "a.b.c": 1 }，如果源文档中没有 a.b.c 路径，
	 *   isNullOnEmpty=true 则写入 a.b.c: null
	 *   isNullOnEmpty=false 则不写入该字段 */
	bool isNullOnEmpty;
} WriteTreeContext;

// 遍历树并将字段写入写入器
// 这是写入 BSON 树的核心函数，根据树结构和上下文将数据写入 BSON writer
// 参数：
//   parentNode - 要遍历的父节点（中间节点）
//   writer - BSON 写入器，用于输出 BSON 数据
//   parentDocument - 父文档，用于读取源数据
//   context - 写入上下文，包含过滤器和配置选项
//   variableContext - 变量上下文，用于解析表达式中的变量
void TraverseTreeAndWriteFieldsToWriter(const BsonIntermediatePathNode *parentNode,
										pgbson_writer *writer,
										pgbson *parentDocument,
										WriteTreeContext *context,
										const ExpressionVariableContext *variableContext);

// 将叶子数组字段写入写入器
// 用于处理 NodeType_LeafWithArrayField 类型的节点，这些节点包含数组字段
// 参数：
//   writer - BSON 写入器
//   child - 要写入的叶子节点
//   document - 源文档
//   variableContext - 变量上下文，用于解析表达式中的变量
void WriteLeafArrayFieldToWriter(pgbson_writer *writer, const BsonPathNode *child,
								 pgbson *document,
								 const ExpressionVariableContext *variableContext);

// 将叶子数组字段的子节点追加到数组写入器
// 用于处理数组元素的写入，将多个子节点写入同一个数组
// 参数：
//   arrayWriter - BSON 数组写入器
//   leafArrayNode - 叶子数组节点，包含要写入的子元素
//   document - 源文档
//   variableContext - 变量上下文，用于解析表达式中的变量
void AppendLeafArrayFieldChildrenToWriter(pgbson_array_writer *arrayWriter, const
										  BsonLeafArrayWithFieldPathNode *leafArrayNode,
										  pgbson *document,
										  const ExpressionVariableContext *variableContext);

// 遍历树并写入（简化版本，不需要上下文）
// 这是 TraverseTreeAndWriteFieldsToWriter 的简化版本，用于不需要变量上下文和过滤的场景
// 参数：
//   parentNode - 要遍历的父节点
//   writer - BSON 写入器
//   parentDocument - 父文档
void TraverseTreeAndWrite(const BsonIntermediatePathNode *parentNode,
						  pgbson_writer *writer, pgbson *parentDocument);

#endif
