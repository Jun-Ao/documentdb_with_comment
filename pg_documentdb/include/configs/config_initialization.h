/*-------------------------------------------------------------------------
 * Copyright (c) Microsoft Corporation.  All rights reserved.
 *
 * include/configs/config_initialization.h
 *
 * Common initialization of configs.
 * 配置项初始化的相关函数声明
 * 包括测试配置、功能开关配置、后台作业配置和系统配置的初始化
 *
 *-------------------------------------------------------------------------
 */

#ifndef DOCUMENTS_CONFIG_INITIALIZATION_H
#define DOCUMENTS_CONFIG_INITIALIZATION_H

/*
 * InitializeTestConfigurations - 初始化测试配置项
 * @prefix: 配置项前缀
 * @newGucPrefix: 新的 GUC 配置前缀
 * 初始化 DocumentDB 相关的测试配置参数
 */
void InitializeTestConfigurations(const char *prefix, const char *newGucPrefix);

/*
 * InitializeFeatureFlagConfigurations - 初始化功能开关配置项
 * @prefix: 配置项前缀
 * @newGucPrefix: 新的 GUC 配置前缀
 * 初始化 DocumentDB 的功能开关，用于控制各个功能模块的启用/禁用
 */
void InitializeFeatureFlagConfigurations(const char *prefix, const char *newGucPrefix);

/*
 * InitializeBackgroundJobConfigurations - 初始化后台作业配置项
 * @prefix: 配置项前缀
 * @newGucPrefix: 新的 GUC 配置前缀
 * 初始化 DocumentDB 后台作业相关的配置参数
 */
void InitializeBackgroundJobConfigurations(const char *prefix, const char *newGucPrefix);

/*
 * InitializeSystemConfigurations - 初始化系统配置项
 * @prefix: 配置项前缀
 * @newGucPrefix: 新的 GUC 配置前缀
 * 初始化 DocumentDB 系统级别的配置参数
 */
void InitializeSystemConfigurations(const char *prefix, const char *newGucPrefix);

/*
 * InitDocumentDBBackgroundWorkerConfigurations - 初始化 DocumentDB 后台工作进程配置
 * @prefix: 配置项前缀
 * 专门初始化后台工作进程相关的配置项
 */
void InitDocumentDBBackgroundWorkerConfigurations(const char *prefix);
#endif
