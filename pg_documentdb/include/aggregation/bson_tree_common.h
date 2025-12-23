/*-------------------------------------------------------------------------
 * Copyright (c) Microsoft Corporation.  All rights reserved.
 *
 * include/bson/bson_tree_common.h
 *
 * Common declarations of functions for handling bson path trees.
 * 处理 BSON 路径树的通用函数声明
 *
 *-------------------------------------------------------------------------
 */
#include "io/bson_core.h"
#include "utils/string_view.h"

#ifndef BSON_TREE_COMMON_H
#define BSON_TREE_COMMON_H

// 叶子节点的基础值（用于位运算判断节点类型）
// 节点类型值 >= 0x80 表示这是一个叶子节点
#define LEAF_NODE_BASE 0x80

/*
 * Node types for various BsonPathNodes
 * 各种 BsonPathNode 的节点类型
 * Categories:
 * 分类：
 * 0x80+ are all leaf nodes (not intermediate)
 * 0x80 及以上都是叶子节点（非中间节点）
 */
typedef enum NodeType
{
	/* Default setting assigned for NodeType */
	/* NodeType 的默认设置值 */
	NodeType_None = 0,

	/* a non-leaf tree node */
	/* 非叶子树节点 */
	/* in a projection a.b.c.d, 'a.b.c' are considered */
	/* 在投影 a.b.c.d 中，'a.b.c' 被视为 */
	/* intermediate nodes */
	/* 中间节点（需要继续向下遍历） */
	NodeType_Intermediate = 0x1,

	/* A leaf tree that contains a */
	/* 包含路径包含的叶子树节点 */
	/* path inclusion (if the path exists, add it to */
	/* 路径包含（如果路径存在，将其添加到 */
	/* the target document): e.g. 'a.b': 1 */
	/* 目标文档）：例如 'a.b': 1 */
	NodeType_LeafIncluded = LEAF_NODE_BASE,

	/* A leaf tree that contains a */
	/* 包含路径排除的叶子树节点 */
	/* path exclusion (if the path exists, actively do not add it to */
	/* 路径排除（如果路径存在，主动不将其添加到 */
	/* the target document): e.g. 'a.b': 0 */
	/* 目标文档）：例如 'a.b': 0 */
	NodeType_LeafExcluded = LEAF_NODE_BASE + 0x1,

	/* A leaf tree that contains an */
	/* 包含表达式的叶子树节点 */
	/* expression which can be a static field, or a path, or operator */
	/* 表达式可以是静态字段、路径或操作符 */
	/* expression : e.g. 'a.b': '$c.1' */
	/* 表达式：例如 'a.b': '$c.1' */
	NodeType_LeafField = LEAF_NODE_BASE + 0x2,

	/*
	 * An effective leaf node, in a sense that once we are at this node,
	 * 有效叶子节点，一旦到达该节点，
	 * we can stop traversing and start writing values. However,
	 * 就可以停止遍历并开始写入值。但是，
	 * the values are a set of array elements that are stored as child nodes.
	 * 这些值是存储为子节点的数组元素集合。
	 */
	NodeType_LeafWithArrayField = LEAF_NODE_BASE + 0x3,

	/*
	 * A leaf node with context, that can help in storing misc operators states for narrow cases.
	 * 带上下文的叶子节点，可以帮助在特定情况下存储各种操作符状态。
	 * e.g. $ positional, $slice and $elemMatch projection operators etc.
	 * 例如：$ 位置操作符、$slice 和 $elemMatch 投影操作符等。
	 */
	NodeType_LeafFieldWithContext = LEAF_NODE_BASE + 0x4,
} NodeType;

// 判断给定节点类型是否为叶子节点
// 通过位运算检查节点类型值是否设置了 LEAF_NODE_BASE 位（0x80）
#define NodeType_IsLeaf(x) ((x & LEAF_NODE_BASE) != 0)

#endif
