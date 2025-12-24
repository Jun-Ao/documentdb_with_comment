/*-------------------------------------------------------------------------
 * Copyright (c) Microsoft Corporation.  All rights reserved.
 *
 * include/commands/diagnostic_commands_common.h
 *
 * 诊断命令的通用声明
 * 这些命令用于诊断场景，如 CurrentOp（当前操作）、IndexStats（索引统计）、CollStats（集合统计）等
 *
 *-------------------------------------------------------------------------
 */

#ifndef DIAGNOSTIC_COMMANDS_COMMON_H
#define DIAGNOSTIC_COMMANDS_COMMON_H

/* 在所有服务器节点上运行查询的函数
 * commandName: 命令名称
 * values: 参数值数组
 * types: 参数类型数组
 * numValues: 参数数量
 * directFunc: 直接执行的函数
 * nameSpaceName: 命名空间名称
 * functionName: 函数名称
 */
List * RunQueryOnAllServerNodes(const char *commandName, Datum *values, Oid *types, int
								numValues, PGFunction directFunc,
								const char *nameSpaceName, const char *functionName);


/* 运行工作进程的诊断逻辑
 * workerFunc: 工作进程执行的函数
 * state: 传递给工作进程函数的状态
 */
pgbson * RunWorkerDiagnosticLogic(pgbson *(*workerFunc)(void *state), void *state);


/* 通用的键定义（用于解析从工作进程到协调器的错误消息和代码）
 * 注意这些使用 #define 而不是 const，因为 C 编译器会抱怨如果包含这些宏的
 * 文件中没有明确使用这些宏
 */
#define ErrMsgKey "err_msg"     /* 错误消息键 */
#define ErrMsgLength 7          /* 错误消息键长度 */
#define ErrCodeKey "err_code"    /* 错误代码键 */
#define ErrCodeLength 8         /* 错误代码键长度 */

/*
 * 对于单节点场景，节点ID始终指向自身
 */
#define SINGLE_NODE_ID 10000000000LL      /* 单节点ID常量 */
#define SINGLE_NODE_ID_STR "10000000000"  /* 单节点ID字符串表示 */

#endif
