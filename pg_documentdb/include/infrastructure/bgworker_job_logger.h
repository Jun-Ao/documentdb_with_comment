/*-------------------------------------------------------------------------
 * Copyright (c) Microsoft Corporation.  All rights reserved.
 *
 * include/infrastructure/bgworker_job_logger.h
 *
 * 后台工作进程作业执行日志工具
 *
 * 本文件定义了DocumentDB中后台工作进程作业执行的日志记录和遥测功能，
 * 用于监控、调试和性能分析后台作业的执行情况。
 *
 *-------------------------------------------------------------------------
 */

#ifndef BGWORKER_JOB_LOGGER_H
#define BGWORKER_JOB_LOGGER_H

#include <postgres.h>
#include <datatype/timestamp.h>
#include <sys/time.h>
#include <nodes/pg_list.h>

/* 后台工作进程作业执行事件遥测结构体 */
typedef struct BgWorkerJobExecutionEvent
{
	/* 事件时间戳 */
	TimestampTz eventTime;

	/* 生成此事件的作业ID */
	int32_t jobId;

	/* 作业执行实例ID（用于唯一性/记录的指针值） */
	uintptr_t instanceId;

	/* 作业执行状态 */
	int32_t state;

	/* （可选）消息，主要用于调试失败情况 */
	char *message;
} BgWorkerJobExecutionEvent;

/* 共享内存管理函数 */
/*
 * 获取后台工作进程作业日志器所需的共享内存大小
 * 返回值：所需的共享内存大小（字节）
 */
Size BgWorkerJobLoggerShmemSize(void);

/*
 * 初始化后台工作进程作业日志器的共享内存
 * 此函数在系统启动时调用，用于分配和初始化共享内存区域
 */
void InitializeBgWorkerJobLoggerShmem(void);

/* 事件日志记录API */
/*
 * 记录后台工作进程事件
 * 输入参数：
 *   - event: 后台工作进程作业执行事件
 */
void RecordBgWorkerEvent(BgWorkerJobExecutionEvent event);

/*
 * 获取后台工作进程作业执行指标数据
 * 返回值：包含作业执行指标的列表
 */
List * GetBgWorkerJobExecutionMetricData(void);

/* 辅助工具函数 */
/*
 * 获取已注册的后台工作进程作业数量
 * 返回值：已注册的作业数量
 */
int GetBgWorkerRegisteredJobsCount(void);

#endif
