/*-------------------------------------------------------------------------
 * Copyright (c) Microsoft Corporation.  All rights reserved.
 *
 * include/aggregation/aggregation_commands.h
 *
 * Exports for the aggregation external commands.
 * 聚合外部命令的导出声明
 *
 * 此头文件声明了 DocumentDB 聚合管道相关的核心函数接口，
 * 这些函数实现了 MongoDB 兼容的查询和聚合操作。
 *-------------------------------------------------------------------------
 */

#ifndef DOCUMENTDB_AGGREGATION_COMMANDS_H
#define DOCUMENTDB_AGGREGATION_COMMANDS_H

#include <utils/array.h>

/* ---------------------------------------------------------
 * 游标管理函数
 * 这些函数用于管理查询和聚合操作的游标
 * --------------------------------------------------------- */

/*
 * delete_cursors - 批量删除游标
 * @cursorArray: 要删除的游标 ID 数组
 *
 * 该函数用于清理不再需要的游标，释放相关资源。
 * 返回值：操作结果的 Datum
 */
extern Datum delete_cursors(ArrayType *cursorArray);

/*
 * find_cursor_first_page - 执行 find 查询并返回第一页结果
 * @database: 数据库名称
 * @findSpec: 查询规格（BSON 格式），包含查询条件、投影、排序等
 * @cursorId: 游标 ID（首次查询时为 0）
 *
 * 该函数执行 MongoDB 风格的 find 查询，返回第一页结果。
 * 如果查询结果超过一页大小，会创建一个游标供后续获取。
 *
 * 返回值：包含查询结果的 BSON 文档
 */
extern Datum find_cursor_first_page(text *database, pgbson *findSpec, int64_t cursorId);

/*
 * aggregate_cursor_first_page - 执行聚合管道并返回第一页结果
 * @database: 数据库名称
 * @aggregationSpec: 聚合规格（BSON 格式），包含管道数组、选项等
 * @cursorId: 游标 ID（首次查询时为 0）
 *
 * 该函数执行 MongoDB 风格的聚合管道操作，返回第一页结果。
 * 聚合管道支持 $match、$group、$sort、$project 等多个阶段。
 *
 * 返回值：包含聚合结果的 BSON 文档
 */
extern Datum aggregate_cursor_first_page(text *database, pgbson *aggregationSpec,
										 int64_t cursorId);

/*
 * aggregation_cursor_get_more - 获取游标的下一页结果
 * @database: 数据库名称
 * @getMoreSpec: getMore 规格（BSON 格式），包含批处理大小等选项
 * @cursorSpec: 游标规格（BSON 格式），包含游标标识信息
 * @maxResponseAttributeNumber: 响应文档的最大属性数量
 *
 * 该函数用于获取游标后续页面的结果，支持分批获取大量数据。
 *
 * 返回值：包含下一批结果的 BSON 文档
 */
extern Datum aggregation_cursor_get_more(text *database, pgbson *getMoreSpec,
										 pgbson *cursorSpec, AttrNumber
										 maxResponseAttributeNumber);

/* ---------------------------------------------------------
 * 元数据查询函数
 * 这些函数用于查询数据库和集合的元数据信息
 * --------------------------------------------------------- */

/*
 * list_collections_first_page - 列出数据库中的集合
 * @database: 数据库名称
 * @listCollectionsSpec: 列表规格（BSON 格式），包含过滤条件等
 *
 * 该函数返回指定数据库中所有集合的列表，
 * 类似于 MongoDB 的 listCollections 命令。
 *
 * 返回值：包含集合列表的 BSON 文档
 */
extern Datum list_collections_first_page(text *database, pgbson *listCollectionsSpec);

/*
 * list_indexes_first_page - 列出集合的索引
 * @database: 数据库名称
 * @listIndexesSpec: 列表规格（BSON 格式），包含集合名称等
 *
 * 该函数返回指定集合的所有索引信息，
 * 类似于 MongoDB 的 listIndexes 命令。
 *
 * 返回值：包含索引列表的 BSON 文档
 */
extern Datum list_indexes_first_page(text *database, pgbson *listIndexesSpec);

#endif /* DOCUMENTDB_AGGREGATION_COMMANDS_H */
