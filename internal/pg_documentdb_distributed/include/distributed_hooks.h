/*-------------------------------------------------------------------------
 * Copyright (c) Microsoft Corporation.  All rights reserved.
 *
 * include/distributed_hooks.h
 *
 * The implementation for shard colocation for DocumentDB with citus.
 *
 *-------------------------------------------------------------------------
 */
#ifndef DOCUMENTDB_DISTRIBUTED_HOOKS_H
#define DOCUMENTDB_DISTRIBUTED_HOOKS_H

/*
 * InitializeDocumentDBDistributedHooks - 初始化DocumentDB分布式钩子函数
 *
 * 该函数负责初始化DocumentDB与Citus集成的分布式功能钩子。
 * 在分布式环境中，DocumentDB需要与Citus的分片机制进行深度集成，
 * 通过钩子函数可以在Citus的关键执行点注入DocumentDB的自定义逻辑。
 *
 * 主要功能包括：
 * 1. 注册分片相关的钩子函数，用于处理分片数据的路由和查询
 * 2. 设置分片位置化（colocation）策略，确保相关数据分布在相同的分片节点上
 * 3. 配置分布式查询优化器的钩子，优化跨分片查询的执行计划
 * 4. 初始化分布式事务管理的钩子，保证分布式环境下的数据一致性
 *
 * 调用时机：
 * - 在PostgreSQL扩展加载时调用（通过PG_INIT钩子）
 * - 在Citus扩展加载完成后调用，确保依赖关系正确
 *
 * 注意事项：
 * - 该函数必须在所有分布式操作之前调用
 * - 依赖Citus扩展的正确加载和初始化
 * - 会修改全局的查询执行钩子函数指针
 *
 * 返回值：void
 * 无返回值，但会设置全局的钩子函数指针
 */
void InitializeDocumentDBDistributedHooks(void);

#endif
