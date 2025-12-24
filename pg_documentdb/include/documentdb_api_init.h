/*-------------------------------------------------------------------------
 * Copyright (c) Microsoft Corporation.  All rights reserved.
 *
 * include/documentdb_api_init.h
 *
 * Exports related to shared library initialization for the API.
 * DocumentDB API 共享库初始化相关的导出函数声明
 * 包括 API 配置初始化、PostgreSQL 钩子安装和后台工作进程初始化
 *
 *-------------------------------------------------------------------------
 */
#ifndef DOCUMENTDB_API_INIT_H
#define DOCUMENTDB_API_INIT_H

/*
 * InitApiConfigurations - 初始化 API 配置项
 * @prefix: 配置项前缀
 * @newGucPrefix: 新的 GUC 配置前缀
 * 初始化 DocumentDB API 的所有配置参数，包括功能开关和系统参数
 */
void InitApiConfigurations(char *prefix, char *newGucPrefix);

/*
 * InstallDocumentDBApiPostgresHooks - 安装 DocumentDB API PostgreSQL 钩子
 * 安装 DocumentDB API 所需的 PostgreSQL 钩子函数，用于拦截和修改 SQL 查询执行
 */
void InstallDocumentDBApiPostgresHooks(void);

/*
 * UninstallDocumentDBApiPostgresHooks - 卸载 DocumentDB API PostgreSQL 钩子
 * 卸载之前安装的 PostgreSQL 钩子函数，确保干净卸载
 */
void UninstallDocumentDBApiPostgresHooks(void);

/*
 * InitializeDocumentDBBackgroundWorker - 初始化 DocumentDB 后台工作进程
 * @libraryName: 库名称
 * @gucPrefix: GUC 配置前缀
 * @extensionObjectPrefix: 扩展对象前缀
 * 初始化 DocumentDB 的后台工作进程，用于处理定时任务和异步操作
 */
void InitializeDocumentDBBackgroundWorker(char *libraryName, char *gucPrefix,
										  char *extensionObjectPrefix);

/*
 * InitializeSharedMemoryHooks - 初始化共享内存钩子
 * 初始化 DocumentDB 在共享内存中的管理钩子，用于跨进程共享数据
 */
void InitializeSharedMemoryHooks(void);

/*
 * InitializeBackgroundWorkerJobAllowedCommands - 初始化后台工作进程允许的命令
 * 初始化后台工作进程可以执行的安全命令列表，确保后台操作的安全性
 */
void InitializeBackgroundWorkerJobAllowedCommands(void);
#endif
