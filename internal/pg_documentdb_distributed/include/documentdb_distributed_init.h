/*-------------------------------------------------------------------------
 * Copyright (c) Microsoft Corporation.  All rights reserved.
 *
 * include/documentdb_distributed_init.h
 *
 * The implementation for shard colocation for documentdb with citus.
 *
 *-------------------------------------------------------------------------
 */
#ifndef DOCUMENTDB_DISTRIBUTED_INIT_H
#define DOCUMENTDB_DISTRIBUTED_INIT_H

/*
 * InitDocumentDBDistributedConfigurations - 初始化DocumentDB分布式配置
 *
 * 该函数负责初始化DocumentDB分布式功能的所有配置参数和全局状态。
 * 这是分布式扩展启动时的核心初始化函数，确保系统在分布式环境下
 * 能够正确运行。
 *
 * 参数说明：
 * @param prefix: 配置参数前缀（字符串指针）
 *                用于标识配置参数的命名空间，例如：
 *                - "documentdb" -> documentdb.param_name
 *                - "citus" -> citus.documentdb.param_name
 *                - 用于避免配置参数命名冲突
 *
 * 主要功能：
 * 1. 初始化分布式相关的GUC（Grand Unified Configuration）参数
 *    - 配置分片策略和分片键选择
 *    - 设置节点健康检查参数
 *    - 配置负载均衡和查询路由策略
 *    - 设置分布式事务超时参数
 *
 * 2. 分片位置化（Colocation）配置初始化
 *    - 定义哪些集合需要位置化
 *    - 配置位置化组（colocation group）的规则
 *    - 设置分片映射策略
 *    - 初始化分片键元数据
 *
 * 3. Citus集成配置
 *    - 检查Citus扩展的可用性
 *    - 配置与Citus的通信参数
 *    - 设置Citus Worker节点的发现机制
 *    - 初始化Citus查询优化器钩子
 *
 * 4. 分布式元数据缓存初始化
 *    - 分片映射缓存
 *    - 节点状态缓存
 *    - 集合位置信息缓存
 *    - 索引分布信息缓存
 *
 * 5. 错误处理和恢复策略配置
 *    - 节点故障检测参数
 *    - 重试策略配置
 *    - 数据一致性保证级别
 *    - 故障转移策略
 *
 * 调用时机：
 * - 在PostgreSQL扩展加载的早期阶段调用
 * - 在DocumentDB核心扩展初始化之后调用
 * - 在任何分布式操作之前必须调用
 * - 在PostgreSQL.conf配置加载完成后调用
 *
 * 注意事项：
 * - 该函数只会执行一次，使用静态变量保证
 * - 需要PostgreSQL配置系统支持
 * - 依赖Citus扩展的正确加载
 * - 如果配置参数已存在，不会重复初始化
 *
 * 返回值：void
 * 无返回值，但会设置全局配置参数
 *
 * 配置示例：
 * 以下参数会被注册到PostgreSQL配置系统中：
 * - documentdb.enable_citus_integration (bool)
 * - documentdb.shard_colocation_enabled (bool)
 * - documentdb.distributed_query_timeout (int)
 * - documentdb.max_coordinator_nodes (int)
 */
void InitDocumentDBDistributedConfigurations(const char *prefix);

#endif
