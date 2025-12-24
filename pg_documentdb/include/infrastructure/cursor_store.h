/*-------------------------------------------------------------------------
 * Copyright (c) Microsoft Corporation.  All rights reserved.
 *
 * include/infrastructure/cursor_store.h
 *
 * pg_documentdb游标存储通用声明
 *
 * 本文件定义了DocumentDB中游标存储的核心接口和数据结构，
 * 用于管理和持久化查询游标，支持大型数据集的分页查询。
 *
 *-------------------------------------------------------------------------
 */

#ifndef DOCUMENTDB_CURSOR_STORE_H
#define DOCUMENTDB_CURSOR_STORE_H
#include <postgres.h>

/* 游标文件状态结构体前向声明 */
typedef struct CursorFileState CursorFileState;

/*
 * 设置游标存储
 * 此函数用于初始化游标存储子系统，包括创建必要的目录结构和初始化配置
 */
void SetupCursorStorage(void);

/*
 * 初始化文件游标共享内存
 * 此函数在系统启动时调用，用于分配和初始化文件游标所需的共享内存区域
 */
void InitializeFileCursorShmem(void);

/*
 * 获取文件游标共享内存大小
 * 返回值：所需的共享内存大小（字节）
 */
Size FileCursorShmemSize(void);

/*
 * 删除待处理的游标文件
 * 此函数会清理所有标记为删除但尚未实际删除的游标文件
 */
void DeletePendingCursorFiles(void);

/*
 * 获取当前游标统计信息
 * 输出参数：
 *   - currentCursorCount: 当前游标数量
 *   - measuredCursorCount: 实际测量的游标数量
 *   - lastCursorSize: 最后一个游标的大小
 */
void GetCurrentCursorCount(int32_t *currentCursorCount, int32_t *measuredCursorCount,
						   int64_t *lastCursorSize);

/*
 * 删除指定的游标文件
 * 输入参数：
 *   - cursorName: 游标名称
 */
void DeleteCursorFile(const char *cursorName);

/*
 * 创建新的游标文件
 * 输入参数：
 *   - cursorName: 游标名称
 * 返回值：游标文件状态结构体指针
 */
CursorFileState * CreateCursorFile(const char *cursorName);

/*
 * 向游标文件写入BSON数据
 * 输入参数：
 *   - cursorFileState: 游标文件状态
 *   - bson: 要写入的BSON数据
 */
void WriteToCursorFile(CursorFileState *cursorFileState, pgbson *bson);

/*
 * 从游标文件读取BSON数据
 * 输入参数：
 *   - cursorFileState: 游标文件状态
 * 返回值：读取的BSON数据
 */
pgbson * ReadFromCursorFile(CursorFileState *cursorFileState);

/*
 * 关闭游标文件状态并返回字节表示
 * 输入参数：
 *   - cursorFileState: 游标文件状态
 *   - writerContext: 写入者内存上下文
 * 返回值：游标状态的字节表示
 */
bytea * CursorFileStateClose(CursorFileState *cursorFileState, MemoryContext
							 writerContext);

/*
 * 反序列化游标文件状态
 * 输入参数：
 *   - cursorFileState: 游标文件状态的字节表示
 * 返回值：反序列化后的游标文件状态结构体指针
 */
CursorFileState * DeserializeFileState(bytea *cursorFileState);

#endif
