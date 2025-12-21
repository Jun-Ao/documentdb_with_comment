/*-------------------------------------------------------------------------
 * Copyright (c) Microsoft Corporation.  All rights reserved.
 *
 * include/node_distributed_operations.h
 *
 * The implementation for node level distributed operations.
 *
 *-------------------------------------------------------------------------
 */
#ifndef DOCUMENTDB_NODE_DISTRIBUTED_OPS_H
#define DOCUMENTDB_NODE_DISTRIBUTED_OPS_H

/*
 * ExecutePerNodeCommand - 在分布式节点上执行命令
 *
 * 该函数是DocumentDB分布式架构中的核心执行函数，负责在Citus集群的
 * 各个节点上执行特定的命令或操作。它提供了细粒度的节点级控制能力，
 * 允许在分布式环境中执行自定义的节点操作。
 *
 * 参数说明：
 * @param nodeFunction: 节点函数标识符（Oid类型）
 *                      PostgreSQL的对象标识符，用于标识要在每个节点上
 *                      执行的函数。这个函数必须在所有Worker节点上可用。
 *                      通常是一个预定义的SQL函数或C扩展函数。
 *
 * @param nodeFunctionArg: 节点函数参数（pgbson指针）
 *                         以BSON格式传递给节点函数的参数数据。
 *                         BSON格式支持复杂的嵌套结构，可以传递：
 *                         - 查询条件
 *                         - 更新操作
 *                         - 配置参数
 *                         - 任意自定义数据
 *
 * @param readOnly: 只读标志（布尔值）
 *                  指示操作是否为只读：
 *                  - true: 只读操作，不修改数据，可以优化执行路径
 *                  - false: 写操作，可能需要分布式事务和锁管理
 *
 * @param distributedTableName: 分布式表名（字符串指针）
 *                              指定操作针对的分布式表（集合）。
 *                              用于确定：
 *                              - 哪些节点需要参与操作
 *                              - 数据分片的位置信息
 *                              - 路由策略选择
 *
 * @param backFillCoordinator: 回填协调器标志（布尔值）
 *                             指示是否需要将结果回填到协调器节点：
 *                             - true: 将各节点的执行结果汇总到协调器
 *                             - false: 节点独立执行，无需结果汇总
 *
 * 返回值：List *
 * 返回一个链表，包含每个节点的执行结果。
 * 链表中的每个元素通常是pgbson结构，包含：
 * - 节点标识符
 * - 执行状态（成功/失败）
 * - 返回数据（如果有）
 * - 错误信息（如果失败）
 *
 * 主要功能：
 * 1. 节点发现和路由
 *    - 根据distributedTableName确定相关的Worker节点
 *    - 构建节点列表和执行计划
 *    - 处理节点不可用的情况
 *
 * 2. 命令分发执行
 *    - 将nodeFunctionArg序列化为适合网络传输的格式
 *    - 并行或串行地在各节点上执行指定函数
 *    - 处理执行超时和重试逻辑
 *
 * 3. 结果收集和处理
 *    - 接收各节点的执行结果
 *    - 根据backFillCoordinator决定是否汇总结果
 *    - 处理部分节点失败的情况
 *
 * 4. 分布式事务管理
 *    - 对于写操作，确保分布式事务的原子性
 *    - 使用两阶段提交（2PC）或类似的分布式事务协议
 *    - 处理事务回滚和补偿操作
 *
 * 使用场景：
 * - 批量数据更新：在所有分片上执行相同的更新操作
 * - 元数据同步：同步各节点的集合或索引信息
 * - 健康检查：检查各节点的健康状态和负载
 * - 数据迁移：在分片重新平衡时移动数据
 * - 查询执行：执行需要跨多个分片的复杂查询
 *
 * 性能考虑：
 * - 只读操作可以并行执行，提高吞吐量
 * - 写操作需要协调，可能影响性能
 * - 大结果集需要考虑内存和网络带宽
 * - 超时设置需要根据操作复杂度调整
 *
 * 错误处理：
 * - 部分节点失败时的处理策略
 * - 网络分区和超时的重试机制
 * - 数据一致性保证和恢复
 * - 详细的错误日志记录
 *
 * 注意事项：
 * - nodeFunction必须在所有Worker节点上预先注册
 * - BSON参数大小可能受网络传输限制
 * - 需要处理节点动态加入/离开的情况
 * - 与Citus的查询路由机制需要协调
 */
List * ExecutePerNodeCommand(Oid nodeFunction, pgbson *nodeFunctionArg, bool readOnly,
							 const char *distributedTableName,
							 bool backFillCoordinator);

#endif
