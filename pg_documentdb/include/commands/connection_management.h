/*-------------------------------------------------------------------------
 * Copyright (c) Microsoft Corporation.  All rights reserved.
 *
 * include/commands/connection_management.h
 *
 * Functions and callbacks related with connection management.
 * 与连接管理相关的函数和回调
 *
 *-------------------------------------------------------------------------
 */
#include <libpq-fe.h>

#ifndef CONNECTION_MANAGEMENT_H
#define CONNECTION_MANAGEMENT_H


/*
 * Function that needs to be called via abort handler.
 * 需要通过中止处理程序调用的函数
 * 当操作被中止时（如查询取消），此函数用于取消活动的连接
 */
void ConnMgrTryCancelActiveConnection(void);

/*
 * Functions internally used by ExtensionExecuteQueryViaLibPQ to let the
 * 由 ExtensionExecuteQueryViaLibPQ 内部使用的函数，用于让
 * connection manager know about the active libpq connection.
 * 连接管理器知道活动的 libpq 连接
 * libpq 是 PostgreSQL 的客户端库，用于建立和管理与数据库服务器的连接
 */
void ConnMgrResetActiveConnection(PGconn *conn);
void ConnMgrForgetActiveConnection(void);

/*
 * Functions used to manage a PG connection's state and report possible errors.
 * 用于管理 PostgreSQL 连接状态并报告可能的错误的函数
 */

// 检查 PG 连接的事务是否处于活动状态
// 返回 true 表示连接上有活动的事务
bool PGConnXactIsActive(PGconn *conn);

// 尝试取消 PG 连接上正在执行的查询
// 用于中止长时间运行的查询
// 返回 true 表示成功发送取消请求
bool PGConnTryCancel(PGconn *conn);

// 报告 PG 连接的错误
// 参数：
//   conn - PostgreSQL 连接
//   result - 查询结果，可能包含错误信息
//   elevel - 错误级别（如 ERROR、WARNING 等）
void PGConnReportError(PGconn *conn, PGresult *result, int elevel);

// 完成 PG 连接的建立
// 在连接成功建立后调用，进行必要的初始化
void PGConnFinishConnectionEstablishment(PGconn *conn);

#endif
