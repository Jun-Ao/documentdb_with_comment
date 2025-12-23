/*-------------------------------------------------------------------------
 * Copyright (c) Microsoft Corporation.  All rights reserved.
 *
 * include/background_worker/background_worker_job.h
 *
 * Common declarations related to pg_documentdb background worker.
 * 与 pg_documentdb 后台工作进程相关的通用声明
 *
 *-------------------------------------------------------------------------
 */

 #include <postgres.h>

 #ifndef DOCUMENTS_BACKGROUND_WORKER_JOB_H
 #define DOCUMENTS_BACKGROUND_WORKER_JOB_H

/*
 * Background worker job command.
 * 后台工作作业命令
 * 描述要执行的函数或过程的标识信息
 */
typedef struct
{
	/*
	 * Function/Procedure schema.
	 * 函数/过程所在的 schema（模式）
	 */
	const char *schema;

	/*
	 * Function/Procedure name.
	 * 函数/过程的名称
	 */
	const char *name;
} BackgroundWorkerJobCommand;

/*
 * Background worker job argument.
 * 后台工作作业参数
 * 描述传递给后台作业的参数信息
 */
typedef struct
{
	/*
	 * Argument Oid.
	 * 参数的对象标识符（OID），PostgreSQL 中类型的唯一标识
	 */
	Oid argType;

	/*
	 * Argument value as a string.
	 * 参数值的字符串表示
	 */
	const char *argValue;

	/*
	 * Boolean for null argument.
	 * 布尔值，指示参数是否为 NULL
	 */
	bool isNull;
} BackgroundWorkerJobArgument;


/*
 * Define a hook that clients can supply. This can be used to dynamically
 * 定义客户端可以提供的钩子。可用于动态
 * change the schedule interval of the job.
 * 更改作业的调度间隔
 */
typedef int (*get_schedule_interval_in_seconds_hook_type)(void);
// 函数指针类型：获取调度间隔（秒）的钩子函数
// 允许在运行时动态调整后台作业的执行频率


/* Background worker job definition */
/* 后台工作作业定义 */
typedef struct
{
	/* Job id. */
	/* 作业 ID */
	int jobId;

	/* Job name, this will be used in log emission. */
	/* 作业名称，将用于日志输出 */
	const char *jobName;

	/* Pair of schema and function/procedure name to be executed. */
	/* 要执行的 schema 和函数/过程名称对 */
	BackgroundWorkerJobCommand command;

	/*
	 * Argument for the command. The number of arguments
	 * 命令的参数。当前参数数量
	 * is currently limited to 1.
	 * 限制为 1 个
	 */
	BackgroundWorkerJobArgument argument;

	/*
	 * Hook to get the schedule interval in seconds.
	 * 获取调度间隔（秒）的钩子
	 * This can be used to dynamically change the schedule interval.
	 * 可用于动态更改调度间隔
	 */
	get_schedule_interval_in_seconds_hook_type get_schedule_interval_in_seconds_hook;

	/*
	 * Command timeout in seconds. The job will be canceled if it runs for longer than this.
	 * 命令超时时间（秒）。如果作业运行时间超过此值，将被取消
	 */
	int timeoutInSeconds;

	/* Flag to decide whether to run the job on metadata coordinator only or on all nodes. */
	/* 标志：决定是仅在元数据协调器上运行作业还是在所有节点上运行 */
	bool toBeExecutedOnMetadataCoordinatorOnly;
} BackgroundWorkerJob;

/*
 * Function to register a new BackgroundWorkerJob to-be scheduled.
 * 注册新的待调度后台工作作业的函数
 * 注册后，系统将按照指定的调度间隔定期执行该作业
 */
void RegisterBackgroundWorkerJob(BackgroundWorkerJob job);

#endif /* DOCUMENTS_BACKGROUND_WORKER_JOB_H */
