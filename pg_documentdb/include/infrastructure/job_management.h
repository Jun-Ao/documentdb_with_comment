/*-------------------------------------------------------------------------
 * Copyright (c) Microsoft Corporation.  All rights reserved.
 *
 * include/infrastructure/job_management.h
 *
 * pg_documentdb作业管理通用声明
 *
 * 本文件定义了DocumentDB中作业管理的全局配置选项，
 * 用于控制后台工作进程的启用和监控功能。
 *
 *-------------------------------------------------------------------------
 */

#ifndef DOCUMENTDB_JOB_MANAGEMENT_H
#define DOCUMENTDB_JOB_MANAGEMENT_H

/* 启用后台工作进程的全局配置标志 */
extern bool EnableBackgroundWorker;

/* 启用后台工作进程作业的全局配置标志 */
extern bool EnableBackgroundWorkerJobs;

/* 启用后台工作进程指标发射的全局配置标志 */
extern bool EnableBgWorkerMetricsEmission;

#endif
