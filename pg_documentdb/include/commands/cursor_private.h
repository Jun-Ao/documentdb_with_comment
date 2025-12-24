/*-------------------------------------------------------------------------
 * Copyright (c) Microsoft Corporation.  All rights reserved.
 *
 * include/commands/cursor_private.h
 *
 * 游标功能的私有声明
 * 定义在 cursors.c 和 aggregation_cursors.c 之间共享的函数和类型
 *
 *-------------------------------------------------------------------------
 */

#ifndef CURSOR_PRIVATE_H
#define CURSOR_PRIVATE_H

/* 清空流式查询，将结果写入数组写入器 */
bool DrainStreamingQuery(HTAB *cursorMap, Query *query, int batchSize,
						 int32_t *numIterations, uint32_t accumulatedSize,
						 pgbson_array_writer *arrayWriters);
/* 清空可跟踪游标查询，返回 BSON 格式的结果 */
pgbson * DrainTailableQuery(HTAB *cursorMap, Query *query, int batchSize,
							int32_t *numIterations, uint32_t accumulatedSize,
							pgbson_array_writer *arrayWriter);
/* 创建并清空持久化查询，支持保持游标 */
bool CreateAndDrainPersistedQuery(const char *cursorName, Query *query,
								  int batchSize, int32_t *numIterations, uint32_t
								  accumulatedSize,
								  pgbson_array_writer *arrayWriter, bool isHoldCursor,
								  bool closeCursor);
/* 创建并清空单批次查询，一次性获取所有结果 */
void CreateAndDrainSingleBatchQuery(const char *cursorName, Query *query,
									int batchSize, int32_t *numIterations, uint32_t
									accumulatedSize, pgbson_array_writer *arrayWriter);
/* 创建并清空带文件的持久化查询，返回文件状态 */
bytea * CreateAndDrainPersistedQueryWithFiles(const char *cursorName, Query *query,
											  int batchSize, int32_t *numIterations,
											  uint32_t
											  accumulatedSize,
											  pgbson_array_writer *arrayWriter, bool
											  closeCursor);
/* 清空持久化游标，继续之前的查询 */
bool DrainPersistedCursor(const char *cursorName, int batchSize,
						  int32_t *numIterations, uint32_t accumulatedSize,
						  pgbson_array_writer *arrayWriter);
/* 清空持久化文件游标，使用文件状态继续查询 */
bytea * DrainPersistedFileCursor(const char *cursorName, int batchSize,
								 int32_t *numIterations, uint32_t accumulatedSize,
								 pgbson_array_writer *arrayWriter,
								 bytea *cursorFileState);

/* 创建并清空点读取查询，用于精确查询单个文档 */
void CreateAndDrainPointReadQuery(const char *cursorName, Query *query,
								  int32_t *numIterations, uint32_t
								  accumulatedSize,
								  pgbson_array_writer *arrayWriter);

/* 构造游标结果表的元数据描述 */
TupleDesc ConstructCursorResultTupleDesc(AttrNumber maxAttrNum);

/* 后处理游标页面，将结果格式化为 BSON 格式 */
Datum PostProcessCursorPage(pgbson_writer *cursorDoc,
							pgbson_array_writer *arrayWriter,
							pgbson_writer *topLevelWriter, int64_t cursorId,
							pgbson *continuation, bool persistConnection,
							pgbson *lastContinuationToken,
							TupleDesc tupleDesc);

/* 创建游标哈希集合 */
HTAB * CreateCursorHashSet(void);
/* 创建可跟踪游标哈希集合 */
HTAB * CreateTailableCursorHashSet(void);
/* 构建继续映射表，用于分页查询 */
void BuildContinuationMap(pgbson *continuationValue, HTAB *cursorMap);
/* 构建可跟踪游标的继续映射表 */
void BuildTailableCursorContinuationMap(pgbson *continuationValue, HTAB *cursorMap);
/* 将继续信息序列化写入写入器 */
void SerializeContinuationsToWriter(pgbson_writer *writer, HTAB *cursorMap);
/* 将可跟踪游标的继续信息序列化写入写入器 */
void SerializeTailableContinuationsToWriter(pgbson_writer *writer, HTAB *cursorMap);
/* 为工作进程序列化继续信息 */
pgbson * SerializeContinuationForWorker(HTAB *cursorMap, int32_t batchSize,
										bool isTailable);
/* 清空单个结果查询，返回 BSON 格式结果 */
pgbson * DrainSingleResultQuery(Query *query);

/* 设置游标页面的前导部分，包含基本信息 */
void SetupCursorPagePreamble(pgbson_writer *topLevelWriter,
							 pgbson_writer *cursorDoc,
							 pgbson_array_writer *arrayWriter,
							 const char *namespaceName, bool isFirstPage,
							 uint32_t *accumulatedLength);

#endif
