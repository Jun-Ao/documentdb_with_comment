# DocumentDB 项目技术实现分析报告

## TLDR - 主要特点

DocumentDB 是基于 PostgreSQL 的 MongoDB 兼容数据库，核心特点如下：

**核心定位**
- 基于 PostgreSQL 扩展实现，充分利用 PostgreSQL 的查询优化器、并行执行、JIT 编译等特性
- 提供完整的 MongoDB API 兼容性，支持 MongoDB Wire Protocol 和标准 MongoDB 客户端

**原生 BSON 存储**
- 自定义 BSON 数据类型，以原始二进制格式存储，避免序列化/反序列化开销
- 严格保持字段顺序，完整支持 MongoDB 数据类型（ObjectId、Decimal128、Binary 等）
- 自动 TOAST 压缩，大文档高效存储

**双接口架构**
- **SQL API**：通过 `documentdb_api` schema 中的 SQL 函数直接操作集合
- **MongoDB 协议**：通过 Rust 网关（pg_documentdb_gw）支持 MongoDB Wire Protocol
- 两套接口共享相同底层实现，确保行为一致性和数据一致性

**智能查询编译**
- 聚合管道编译器将 MongoDB 聚合管道转换为优化的 PostgreSQL 查询树
- 利用基于代价的优化器（CBO）自动选择最优执行计划
- 支持 $match、$group、$lookup、$facet 等复杂聚合操作

**多类型索引支持**
- **B-Tree 索引**：单路径索引、复合索引、通配符索引
- **GIN 索引**：全文检索和数组查询
- **RUM 索引**：带排序的全文检索，避免回表排序
- **向量索引**：集成 pgvector，支持 HNSW 和 IVFFlat，支持混合检索

**高级功能**
- **完整事务支持**：ACID 事务，支持多隔离级别（REPEATABLE READ、READ COMMITTED）
- **向量搜索**：原生 pgvector 集成，支持 L2、内积、余弦距离，适合 AI 应用
- **全文检索**：RUM 索引集成，支持相关性排序
- **地理空间查询**：PostGIS 集成，支持 $near、$geoWithin 等操作
- **Change Streams**：基于逻辑复制实现实时变更通知
- **分布式支持**：基于 Citus 的水平扩展，支持分片和聚合下推

**技术架构**
- **pg_documentdb_core**：C 语言实现，提供原生 BSON 数据类型和操作符
- **pg_documentdb**：C 语言实现，提供 API 层和聚合管道编译器
- **pg_documentdb_gw**：Rust 语言实现，高性能协议网关，异步 I/O，连接池优化
- **pg_documentdb_extended_rum**：扩展的 RUM 索引支持
- **pg_documentdb_distributed**：基于 Citus 的分布式支持

**与 PostgreSQL 的共存**
- DocumentDB 作为 PostgreSQL 扩展安装，不影响 PostgreSQL 原有功能
- 可以在同一个数据库中混合使用关系型表和文档集合
- BSON 类型可作为普通列类型使用，支持混合数据模型

## 1. 项目架构概览

### 1.1 技术栈

DocumentDB 采用三层架构设计，使用多种编程语言实现：

- **pg_documentdb_core**: C 语言实现，PostgreSQL 扩展，提供原生 BSON 数据类型支持
- **pg_documentdb**: C 语言实现，PostgreSQL 扩展，提供 API 层和聚合管道编译器
- **pg_documentdb_gw**: Rust 语言实现，协议网关服务，处理 MongoDB Wire Protocol
- **pg_documentdb_extended_rum**: C 语言实现，扩展的 RUM 索引支持
- **pg_documentdb_distributed**: C 语言实现，分布式支持（基于 Citus）

### 1.2 核心依赖

- **PostgreSQL**: 基础数据库引擎（推荐 15+，主要目标版本 17）。**重要说明**：本项目不包含 PostgreSQL 的完整源代码，PostgreSQL 作为外部依赖通过以下方式获取：
    - 使用系统已安装的 PostgreSQL 开发包（推荐方式）
    - 通过构建脚本从 GitHub 动态下载特定版本的 PostgreSQL 源代码进行编译
    - 构建脚本会根据配置的版本号（15/16/17/18）从 PostgreSQL 官方仓库获取对应的发布标签或提交
- **libbson**: MongoDB 官方 BSON 库，用于 BSON 数据解析和序列化
- **pgvector**: 向量搜索扩展
- **PostGIS**: 地理空间查询支持
- **pg_cron**: 定时任务支持
- **RUM 索引**: 增强的全文检索索引

### 1.3 版本信息

- pg_documentdb_core: 0.109-0
- pg_documentdb: 0.109-0
- 网关版本: 0.1.0 (Rust)

## 2. 核心组件技术实现

### 2.1 pg_documentdb_core - BSON 存储引擎

#### 2.1.1 BSON 数据类型实现

核心数据结构采用 PostgreSQL 的 varlena 变长数据类型结构，包含长度头部和二进制数据区域。BSON 数据以原始二进制格式存储在变长数组中。

**关键技术点：**

1. **varlena 结构**: 使用 PostgreSQL 的 varlena 变长数据类型，自动处理内存分配和 TOAST 机制
2. **二进制存储**: BSON 数据以原始二进制格式存储，避免序列化/反序列化开销
3. **TOAST 支持**: 超过页面大小（8KB）的文档自动使用 TOAST 机制压缩存储，保证主表扫描效率

**TOAST 机制说明**：

BSON 类型使用 PostgreSQL 标准的 TOAST（The Oversized-Attribute Storage Technique）机制，与 JSONB 类型使用相同的 TOAST 基础设施，但在数据格式和压缩效果上存在差异：

**相同点**：

- **存储策略**：两者都使用 `storage = extended`，表示使用扩展存储
- **触发条件**：当数据超过页面大小（通常 8KB）时，自动触发 TOAST
- **压缩算法**：使用 PostgreSQL 默认的压缩算法（通常是 LZ 压缩）
- **存储位置**：大文档存储在独立的 TOAST 表中，主表只存储引用指针
- **自动管理**：TOAST 的压缩、解压、存储都由 PostgreSQL 内核自动处理

**差异点**：

1. **数据格式**：
    - **JSONB**：存储为 PostgreSQL 内部的树形结构（已解析的 JSON），包含指针和元数据
    - **BSON**：存储为原始二进制格式（MongoDB 标准 BSON 字节流）
    - **影响**：BSON 的二进制格式通常比 JSONB 的树形结构更紧凑，压缩效果可能更好

2. **压缩效率**：
    - **JSONB**：树形结构包含指针和元数据，压缩率相对较低
    - **BSON**：紧凑的二进制格式，重复模式更多，压缩率通常更高
    - **影响**：相同内容的文档，BSON 的 TOAST 压缩后可能占用更少空间

3. **解压开销**：
    - **JSONB**：TOAST 解压后需要构建树形结构，需要内存分配和指针设置
    - **BSON**：TOAST 解压后直接得到二进制数据，可以直接使用，无需额外解析
    - **影响**：BSON 的解压开销更小，特别是在只需要部分数据时

4. **部分读取**：
    - **JSONB**：即使只需要文档的一部分，通常也需要解压整个文档
    - **BSON**：理论上可以支持部分解压，但当前实现仍需要完整解压
    - **影响**：两者在部分读取场景下的行为相似，都需要完整解压

**技术实现**：

BSON 类型完全依赖 PostgreSQL 的标准 TOAST 机制，没有自定义压缩函数：

- 类型定义使用 `storage = extended`，启用 TOAST
- 使用标准的 `PG_DETOAST_DATUM` 宏处理 TOAST 数据
- 压缩和解压由 PostgreSQL 内核的 `toast_compress_datum` 和 `toast_decompress_datum` 函数处理
- 存储格式遵循 PostgreSQL 的 TOAST 规范

**性能影响**：

- **存储空间**：BSON 的二进制格式压缩率通常更高，相同内容占用空间更小
- **查询性能**：两者都需要完整解压，性能差异主要来自数据格式而非 TOAST 机制本身
- **写入性能**：BSON 的二进制格式写入时无需构建树形结构，写入开销更小

#### 2.1.2 BSON 比较操作符

核心比较函数实现了 MongoDB 兼容的 BSON 比较语义：

- **类型排序**: 遵循 MongoDB 的 BSON 类型排序规则（MinKey < Null < Numbers < String < ... < MaxKey）
- **数值比较**: 统一将 Int32、Int64、Double 转换为 long double 进行比较，确保数值比较的一致性
- **字符串比较**: 支持 ICU 排序规则（Collation），通过排序规则字符串参数传递，支持多语言排序
- **数组和文档**: 递归比较，支持嵌套结构的深度比较

#### 2.1.3 BSON 操作符族

**B-Tree 操作符类**: 定义了完整的 B-Tree 操作符族，包括小于、小于等于、等于、大于等于、大于五种比较操作符，以及对应的比较函数。这允许在 BSON 类型上创建标准 B-Tree 索引，用于范围查询和排序操作。

**哈希操作符类**: 实现了哈希操作符类，支持 BSON 类型的哈希索引和哈希连接。哈希函数对 BSON 文档计算哈希值，用于等值查询优化、哈希连接和去重操作。

### 2.2 pg_documentdb - API 层与查询编译器

#### 2.2.1 聚合管道编译器

聚合管道编译器是 DocumentDB 最复杂的技术组件之一，负责将 MongoDB 聚合管道转换为 PostgreSQL 查询树。

**编译流程：**

1. **阶段解析**: 解析 BSON 格式的管道数组，识别每个阶段类型（$match, $group, $lookup 等），验证阶段顺序和语义正确性

2. **查询树构建**: 从基础表查询开始，按顺序应用每个阶段的转换函数，根据阶段特性决定是否需要子查询包装

3. **阶段转换映射**:

| MongoDB 阶段 | PostgreSQL 转换 |
|-------------|----------------|
| $match | WHERE 子句 |
| $project | SELECT 列表转换 |
| $group | GROUP BY + 聚合函数 |
| $lookup | LEFT JOIN |
| $sort | ORDER BY |
| $limit | LIMIT |
| $skip | OFFSET |
| $unwind | LATERAL JOIN 或 unnest() |
| $facet | 多个并行子查询 + UNION ALL |

**子查询注入策略：**

编译器在以下情况会注入子查询：
- 投影阶段后需要排序或分组
- 需要保留中间结果用于后续阶段
- 复杂表达式需要物化

#### 2.2.2 查询操作符实现

实现了 MongoDB 查询操作符到 PostgreSQL 函数的映射：

- **比较操作符**: $eq, $ne, $gt, $gte, $lt, $lte 映射到 BSON 比较操作符
- **成员操作符**: $in, $nin 使用 ANY/ALL 子查询实现
- **存在性检查**: $exists 使用路径提取函数加 NULL 检查
- **正则表达式**: $regex 集成 PCRE2 正则表达式引擎
- **数组匹配**: $elemMatch 使用 bson_sequence 类型进行数组元素匹配

#### 2.2.3 索引访问方法

**B-Tree 索引**:
- 单路径索引: 对特定字段路径（如 user.name）建立索引
- 复合索引: 多字段组合索引，支持多字段联合查询优化
- 通配符索引: 使用 GIN 索引，自动索引文档中的所有字段，适合动态 Schema 场景

**GIN 索引**:
- 用于全文检索和数组查询
- 支持通配符索引（Wildcard Indexes）
- 索引选项存储在操作符类选项中

**RUM 索引**:
- 扩展的 GIN 索引，存储位置信息
- 支持带排序的全文检索，避免回表排序
- 用于 $text 查询优化，提升全文检索性能

**向量索引**:
- 集成 pgvector，支持 HNSW 和 IVFFlat 索引
- 支持 L2、内积、余弦距离三种相似度度量
- 支持半精度向量压缩（halfvec），减少存储空间

#### 2.2.4 查询优化器集成

**选择性估算**: 利用 PostgreSQL 的统计信息（MCV、直方图）进行选择性估算。对于无统计信息的操作符，使用默认选择性（0.5 或 0.01）。支持全扫描标记（selectivity = 1.0），用于排序场景。

**索引选择**: 索引优先级排序为：主键索引（B-Tree on _id）、复合索引、常规单路径索引、通配符索引、其他索引。优化器会根据查询条件自动选择最优索引。

### 2.3 pg_documentdb_gw - 协议网关

#### 2.3.1 协议解析

网关实现了 MongoDB Wire Protocol 的完整解析：

- **OP_QUERY**: 旧版查询协议支持
- **OP_MSG**: 新版消息协议（主要使用）
- **OP_INSERT**: 批量插入协议
- **OP_GETMORE**: 游标分页协议

协议解析流程：根据操作码类型路由到相应的解析函数，解析 BSON 格式的命令和参数，提取数据库名、集合名和操作类型。

#### 2.3.2 命令路由

网关根据请求类型路由到相应的处理函数，包括聚合查询、查找、插入、更新、删除、索引管理、用户管理等操作。每个操作类型都有对应的处理函数，实现命令的分发和执行。

#### 2.3.3 SQL 查询生成

网关将 MongoDB 命令转换为 PostgreSQL 函数调用，**这些函数与 SQL API 使用的是同一套底层函数**：

**Find 查询转换**: MongoDB 的 find 命令转换为调用 `documentdb_api.find_cursor_first_page` SQL 函数，传入数据库名和查询条件 BSON。

**Aggregate 查询转换**: MongoDB 的 aggregate 命令转换为调用 `documentdb_api.aggregate_cursor_first_page` SQL 函数，传入数据库名和完整的管道 BSON 数组。

**Insert 查询转换**: MongoDB 的 insert 命令转换为调用 `documentdb_api.insert` SQL 函数。

**Update 查询转换**: MongoDB 的 update 命令转换为调用 `documentdb_api.update` SQL 函数。

**Delete 查询转换**: MongoDB 的 delete 命令转换为调用 `documentdb_api.delete` SQL 函数。

所有转换都使用参数化查询，避免 SQL 注入，并支持查询计划缓存。

**关键特性**：网关和 SQL API 共享相同的底层实现，确保两种接口的行为一致性和数据一致性。

#### 2.3.4 连接池管理

使用连接池库实现连接管理：

- **连接复用**: 减少连接建立开销，提高并发性能
- **自动重连**: 处理连接断开情况，自动恢复连接
- **事务隔离**: 每个事务使用独立连接，保证事务隔离性

#### 2.3.5 游标管理

实现 MongoDB 风格的游标机制：

- **游标存储**: 使用游标存储管理活跃游标，跟踪游标状态
- **分页**: 通过 LIMIT/OFFSET 实现结果分页
- **getMore**: 客户端通过 cursorId 获取下一页数据
- **超时清理**: 自动清理过期游标，释放资源

#### 2.3.6 事务管理

事务状态管理结构包含会话 ID、事务编号、游标存储和底层 PostgreSQL 事务对象。

**事务生命周期**:
1. **开始**: startTransaction 命令转换为 BEGIN TRANSACTION
2. **执行**: 所有命令在同一事务中执行
3. **提交**: commitTransaction 转换为 COMMIT
4. **回滚**: abortTransaction 转换为 ROLLBACK

**隔离级别映射**:
- MongoDB readConcern: "snapshot" 映射到 PostgreSQL REPEATABLE READ
- MongoDB readConcern: "local" 映射到 PostgreSQL READ COMMITTED

**事务存储**: 使用事务存储跟踪所有活跃事务，防止重复提交，支持事务超时和自动清理。

## 3. 关键技术实现细节

### 3.1 BSON 路径提取

路径提取函数支持点号分隔的路径，如 user.address.city。实现流程包括：解析路径字符串，使用 BSON 迭代器递归查找，返回找到的值或 NULL。

**性能优化**:
- 路径缓存: 常用路径的解析结果被缓存，避免重复解析
- 早期终止: 如果路径不存在，立即返回，不进行完整遍历

### 3.2 查询操作符编译

MongoDB 查询表达式转换为 PostgreSQL 表达式树。例如，包含多个条件的查询会被转换为多个条件表达式的 AND 组合，使用路径提取操作符提取字段值，然后应用相应的比较操作符。

### 3.3 向量搜索实现

**向量提取**: 从 BSON 文档中提取向量字段，转换为 pgvector 的 vector 类型，支持指定维度。

**相似度搜索**: 使用向量相似度操作符进行搜索，按距离排序，支持限制返回数量。查询会自动过滤掉没有向量字段的文档。

**混合检索**: 支持向量搜索与标量过滤的组合。优化器可以选择先执行向量搜索后过滤，或先过滤后向量搜索，根据索引效率和过滤选择性自动选择最优策略。

### 3.4 全文检索实现

**RUM 索引集成**:

1. **索引创建**: 使用 RUM 索引类型，对文本字段建立全文索引，支持部分索引（只索引包含文本字段的文档）

2. **查询转换**: MongoDB 的 $text 查询转换为 PostgreSQL 的全文检索查询，使用文本搜索操作符

3. **排序优化**: RUM 索引存储位置信息，支持按相关性排序而无需回表，大幅提升性能

### 3.5 地理空间查询

**PostGIS 集成**:

1. **几何验证**: 验证 GeoJSON 格式的几何数据，确保数据有效性

2. **索引创建**: 使用 GIST 索引，支持 2D 和 2DSphere 索引，配置边界范围

3. **查询操作**: $near, $geoWithin 等地理空间操作转换为 PostGIS 函数，支持距离计算和空间关系判断

### 3.6 Change Streams 实现

**基于逻辑复制**:

1. **WAL 监听**: 使用 PostgreSQL 逻辑解码插件监听预写式日志

2. **变更解析**: 解析 WAL 中的 BSON 变更，识别插入、更新、删除操作

3. **流式输出**: 转换为 MongoDB Change Stream 格式，支持实时变更通知

### 3.7 分布式支持

**Citus 集成**:

1. **分片键**: 支持基于字段的分片，配置分片策略

2. **查询路由**: 单分片查询直接路由到对应节点，减少网络传输

3. **聚合下推**: 尽可能在分片上执行聚合，减少网络传输和中心节点负载

## 4. 性能优化技术

### 4.1 查询计划缓存

PostgreSQL 的查询计划缓存机制：

- **参数化查询**: 使用参数化查询避免重复解析，提高效率
- **计划复用**: 相同模式的查询复用执行计划，减少优化开销
- **计划失效**: 统计信息更新时自动失效，保证计划准确性

### 4.2 并行查询

利用 PostgreSQL 并行查询引擎：

- **并行扫描**: 大表全扫描自动并行化，利用多核 CPU
- **并行聚合**: GROUP BY 操作并行执行，提升聚合性能
- **并行排序**: ORDER BY 并行排序，加快排序速度

### 4.3 JIT 编译

PostgreSQL LLVM JIT 支持：

- **表达式编译**: 复杂 BSON 表达式编译为机器码，提升执行效率
- **聚合函数**: 聚合函数 JIT 编译，减少函数调用开销
- **条件判断**: WHERE 子句中的复杂条件 JIT 编译，加快过滤速度

### 4.4 索引优化

**索引选择策略**:

1. **选择性估算**: 基于统计信息选择最优索引，考虑索引选择性和查询模式
2. **索引合并**: 多个索引条件可以合并使用，组合多个索引的优势
3. **覆盖索引**: 某些查询可以直接从索引获取数据，无需回表，大幅提升性能

**部分索引**: 只对满足特定条件的文档建立索引，减少索引大小和写入开销，同时保持查询性能。

### 4.5 连接池优化

网关层连接池优化：

- **连接复用**: 减少连接建立开销，提高并发处理能力
- **健康检查**: 自动检测和移除失效连接，保证连接可用性
- **连接预热**: 启动时预建立连接，减少首次请求延迟

## 5. 数据存储结构

### 5.1 集合存储

每个集合对应一个 PostgreSQL 表，表结构包含：

- **document 列**: 存储完整的 BSON 文档，使用 bson 数据类型
- **shard_key_value**: 分片键值（分布式场景），用于数据分片
- **隐藏列**: ctid 用于物理行定位，支持高效的行访问

### 5.2 索引存储

索引存储在 PostgreSQL 系统表中：

- **pg_index**: 存储索引定义和元数据
- **pg_class**: 存储索引关系信息
- **pg_opclass**: 存储操作符类定义

**自定义选项**: 通过操作符类选项存储索引特定配置，如路径、通配符模式、向量维度等。

### 5.3 元数据管理

**集合元数据**: 存储在系统目录表中，包含集合 ID（UUID）、数据库名、集合名、创建时间、更新时间等信息。

**索引元数据**: 存储在系统目录表中，包含索引定义（BSON 格式）、索引状态、创建选项等信息。

## 6. 错误处理与兼容性

### 6.1 错误码映射

MongoDB 错误码到 PostgreSQL 错误码的映射机制：

- 解析 MongoDB 错误消息，提取错误类型和详细信息
- 转换为对应的 PostgreSQL 错误码，保持错误语义一致性
- 确保客户端能够正确识别和处理错误

### 6.2 API 兼容性

**支持的操作**:
- CRUD: insert, find, update, delete 完整支持
- 聚合: aggregate pipeline 完整支持
- 索引: createIndex, dropIndex 完整支持
- 管理: createCollection, dropCollection 完整支持

**部分支持**:
- 某些边缘操作符可能不完全兼容，需要验证
- 新版本 MongoDB 特性可能延迟支持，需要持续更新

## 7. 安全与认证

### 7.1 SCRAM 认证

实现 MongoDB SCRAM-SHA-1/256 认证机制：

1. 客户端发送认证请求，包含用户名
2. 服务器生成挑战（nonce + salt），返回给客户端
3. 客户端计算证明，使用密码和挑战信息
4. 服务器验证证明，确认身份

认证信息存储在 PostgreSQL 用户系统中，与 PostgreSQL 权限系统集成。

### 7.2 角色管理

MongoDB 角色映射到 PostgreSQL 权限：

- readAnyDatabase 映射到 PostgreSQL SELECT 权限
- readWriteAnyDatabase 映射到 PostgreSQL SELECT, INSERT, UPDATE, DELETE 权限

角色管理通过 PostgreSQL 的权限系统实现，支持细粒度的权限控制。

## 8. 使用方式与部署

### 8.1 使用方式概述

DocumentDB 支持两种使用方式，用户可以根据实际需求选择：

**方式一：使用现有 PostgreSQL（推荐）**

适用于已有 PostgreSQL 运行环境的场景，这是最简单和推荐的方式：

1. **准备 PostgreSQL 环境**：在服务器上安装并运行 PostgreSQL（版本 15/16/17/18）
2. **安装开发包**：安装对应版本的 PostgreSQL 开发包（如 `postgresql-server-dev-16`），提供编译所需的头文件和库
3. **编译 DocumentDB 扩展**：在 DocumentDB 项目目录执行 `make install`，扩展会被编译并安装到 PostgreSQL 的扩展目录
4. **安装扩展**：在 PostgreSQL 数据库中执行 `CREATE EXTENSION pg_documentdb CASCADE;` 启用扩展
5. **配置 PostgreSQL**：修改 `postgresql.conf`，添加必要的预加载库配置
6. **启动网关**：编译并启动网关服务，提供 MongoDB 协议接口

**方式二：从源码构建 PostgreSQL**

适用于需要特定 PostgreSQL 配置或完全控制构建过程的场景：

1. **使用构建脚本**：执行 `build_documentdb_with_scripts.sh`，脚本会自动：
    - 从 GitHub 下载指定版本的 PostgreSQL 源代码
    - 编译并安装 PostgreSQL
    - 编译并安装所有依赖扩展（pgvector、PostGIS、pg_cron 等）
    - 编译并安装 DocumentDB 扩展
2. **配置和启动**：配置 PostgreSQL 并启动服务
3. **安装扩展**：在数据库中创建扩展
4. **启动网关**：启动网关服务

**Docker 方式（最简单）**

对于快速体验和开发测试，推荐使用 Docker 镜像：

1. 拉取预构建的 Docker 镜像
2. 运行容器，自动包含 PostgreSQL 和 DocumentDB
3. 直接连接使用，无需手动编译和配置

### 8.2 编译流程

**PostgreSQL 依赖获取**：

项目不包含 PostgreSQL 源代码，需要在编译前准备 PostgreSQL 环境。有两种方式：

1. **使用系统 PostgreSQL**（推荐）：安装 PostgreSQL 开发包（如 `postgresql-server-dev-16`），扩展直接链接到系统 PostgreSQL
2. **从源码编译 PostgreSQL**：使用构建脚本从 GitHub 下载并编译指定版本的 PostgreSQL，脚本会根据版本配置（15/16/17/18）获取对应的发布标签

**DocumentDB 扩展编译**：

编译采用分层结构，编译顺序很重要：

1. 首先编译 pg_documentdb_core（核心扩展）
2. 然后编译 pg_documentdb（API 层）
3. 接着编译 pg_documentdb_extended_rum（扩展索引）
4. 最后编译 pg_documentdb_distributed（分布式支持）

每个组件都有独立的编译配置，支持并行编译。编译时通过 `PG_CONFIG` 环境变量指定 PostgreSQL 的配置工具路径，确保扩展与 PostgreSQL 版本匹配。

### 8.3 扩展安装

**在 PostgreSQL 中安装扩展**：

扩展安装使用 CASCADE 选项自动安装依赖扩展，包括 documentdb_core、pg_cron、vector、postgis、tsm_system_rows 等。安装过程会自动处理依赖关系和版本兼容性。

**安装步骤**：

1. **创建数据库**（如需要）：
   ```sql
   CREATE DATABASE documentdb;
   \c documentdb
   ```

2. **创建扩展**：
   ```sql
   CREATE EXTENSION IF NOT EXISTS pg_documentdb CASCADE;
   ```

3. **配置 PostgreSQL**（修改 postgresql.conf）：
    - 添加预加载库：`shared_preload_libraries = 'pg_cron,pg_documentdb_core,pg_documentdb'`
    - 配置 pg_cron 数据库：`cron.database_name = 'documentdb'`
    - 重启 PostgreSQL 服务使配置生效

4. **验证安装**：
   ```sql
   SELECT * FROM pg_extension WHERE extname LIKE 'pg_documentdb%';
   ```

**扩展依赖关系**：

- `pg_documentdb` 依赖 `pg_documentdb_core`
- `pg_documentdb` 依赖 `pg_cron`（用于后台任务）
- `pg_documentdb` 依赖 `vector`（用于向量搜索）
- `pg_documentdb` 依赖 `postgis`（用于地理空间查询）
- `pg_documentdb` 依赖 `tsm_system_rows`（用于采样）

使用 CASCADE 选项会自动安装所有依赖扩展。

### 8.4 PostgreSQL 与 DocumentDB 的共存关系

**重要特性：DocumentDB 扩展不会影响 PostgreSQL 原有功能**

安装 DocumentDB 扩展后，PostgreSQL 的使用方式完全不变，两种使用方式可以共存：

**PostgreSQL 原有功能（完全不受影响）**：

- 所有原有的 SQL 查询、表、索引、函数等完全正常工作
- 原有的 PostgreSQL 数据类型（int, text, jsonb 等）和操作符不受影响
- 原有的 PostgreSQL 扩展（如 pg_stat_statements）可以正常使用
- 原有的备份、复制、监控工具完全兼容

**DocumentDB 功能（通过特定方式访问）**：

DocumentDB 通过以下方式提供功能，与 PostgreSQL 原有功能完全隔离：

1. **独立的 Schema**：
    - `documentdb_core`: 核心 BSON 类型和操作符
    - `documentdb_api`: 公开的 API 函数
    - `documentdb_data`: 数据存储表（每个集合对应一个表）
    - `documentdb_api_catalog`: 元数据表

2. **独立的数据存储**：
    - DocumentDB 集合数据存储在 `documentdb_data.documents_<collection_id>` 表中
    - 这些表使用标准的 PostgreSQL Heap 存储，但结构独立
    - 不会与用户创建的普通 PostgreSQL 表冲突

3. **访问方式**：
    - **通过 SQL 函数**：使用 `documentdb_api.*` 函数访问 DocumentDB 功能
      ```sql
      SELECT document FROM documentdb_api.collection('db', 'collection');
      SELECT documentdb_api.insert_one('db', 'collection', '{"key": "value"}');
      ```
    - **通过 MongoDB 协议**：通过网关使用 MongoDB 客户端连接（端口 10260 或 27017）
    - **混合使用**：可以在同一个事务中混合使用 PostgreSQL SQL 和 DocumentDB 函数

**使用场景示例**：

- **纯 PostgreSQL 使用**：创建普通表，使用标准 SQL，完全不受 DocumentDB 影响
- **纯 DocumentDB 使用**：通过 MongoDB 协议或 DocumentDB SQL 函数操作文档集合
- **混合使用**：在同一个数据库中既有关系型表，也有文档集合，甚至可以在 SQL 中 JOIN 两者

**技术实现**：

- DocumentDB 扩展通过 PostgreSQL 的扩展机制安装，只添加新的数据类型、函数和操作符
- 不修改 PostgreSQL 核心代码，不改变 PostgreSQL 原有行为
- 使用 PostgreSQL 标准的扩展接口，确保兼容性和稳定性

**BSON 类型作为普通列类型使用**：

BSON 类型是 DocumentDB 扩展（`pg_documentdb_core`）添加的自定义数据类型，不是 PostgreSQL 原生的数据类型。安装 `pg_documentdb_core` 扩展后，BSON 类型会在 `documentdb_core` schema 中创建，可以在任何 PostgreSQL 表中使用，并且可以与其他 PostgreSQL 列类型混合使用。

**重要说明**：

- BSON 类型由 DocumentDB 扩展提供，位于 `documentdb_core` schema 中
- 使用前必须先安装 `pg_documentdb_core` 扩展
- 类型全名为 `documentdb_core.bson`，在 `documentdb_core` schema 中可以直接使用 `bson`
- 在普通表中使用时，需要指定完整的 schema 路径或设置 search_path

**示例：创建混合类型的表**：

```sql
CREATE TABLE my_table (
    id SERIAL PRIMARY KEY,
    name TEXT,
    metadata documentdb_core.bson,
    created_at TIMESTAMPTZ DEFAULT NOW(),
    config JSONB
);
```

在这个示例中：
- `id`: PostgreSQL 原生的 SERIAL 类型
- `name`: PostgreSQL 原生的 TEXT 类型
- `metadata`: DocumentDB 扩展提供的 BSON 类型，可以存储 MongoDB 兼容的文档数据
- `created_at`: PostgreSQL 原生的 TIMESTAMPTZ 类型
- `config`: PostgreSQL 原生的 JSONB 类型

**BSON 类型的特性**：

- BSON 类型实现了完整的 PostgreSQL 类型接口（input, output, send, receive），符合 PostgreSQL 扩展类型规范
- 支持标准的 SQL 操作：INSERT, SELECT, UPDATE, DELETE
- 支持索引：可以在 BSON 列上创建 B-Tree、GIN 等索引
- 支持 TOAST：大文档自动使用 TOAST 机制
- 支持事务：完全支持 ACID 事务
- 支持复制：支持 PostgreSQL 的流复制和逻辑复制

**BSON 类型与 JSONB 的查询语法差异**：

BSON 类型的查询语法与 PostgreSQL 的 JSONB 类型**不完全相同**，虽然支持部分相似的操作符，但整体上使用不同的操作符集和函数集。

**支持的操作符（与 JSONB 相似）**：

- `->` 操作符：提取路径值，返回 BSON 类型
  ```sql
  SELECT metadata->'user' FROM my_table;
  ```
- `->>` 操作符：提取路径值，返回 text 类型
  ```sql
  SELECT metadata->>'name' FROM my_table;
  ```

**不支持的操作符（JSONB 支持但 BSON 不支持）**：

- `@>` 操作符：BSON 类型虽然定义了 `@>` 操作符，但语义不同（用于比较而非包含检查）
- `#>` 和 `#>>` 操作符：不支持 JSON 路径数组语法
- JSON Path 查询：不支持 PostgreSQL 的 JSON Path 查询语言（如 `jsonb_path_query`）

**BSON 类型特有的操作符**：

BSON 类型使用自定义的操作符集，主要用于 MongoDB 兼容的查询语义：

- `@=`, `@!=`：等于、不等于比较
- `@<`, `@<=`, `@>`, `@>=`：小于、小于等于、大于、大于等于比较
- `@*=`, `@!*=`：成员检查（in, not in）
- `@~`：正则表达式匹配
- `@?`：存在性检查
- `@@#`：数组大小检查
- `@#`：类型检查
- `@&=`, `@!&`：数组包含检查
- `@#?`：数组元素匹配（elemMatch）

**BSON 类型特有的函数**：

- `bson_get_value(bson, text)`：提取路径值，返回 BSON（对应 `->` 操作符）
- `bson_get_value_text(bson, text)`：提取路径值，返回 text（对应 `->>` 操作符）
- `bson_object_keys(bson)`：返回文档的所有键（类似 `jsonb_object_keys`）
- `bson_to_json_string(bson)`：将 BSON 转换为 JSON 字符串
- `bson_json_to_bson(text)`：将 JSON 字符串转换为 BSON
- `bson_to_bson_hex(bson)`：将 BSON 转换为十六进制字符串
- `bson_hex_to_bson(cstring)`：将十六进制字符串转换为 BSON

**不支持的功能**：

- PostgreSQL 的 JSON 函数（如 `jsonb_extract_path`, `jsonb_path_query`, `jsonb_path_query_array` 等）不能直接用于 BSON 类型
- JSON Path 查询语言（PostgreSQL 12+ 引入的 `jsonpath` 类型）
- JSONB 的 GIN 索引操作符类（BSON 使用自定义的 GIN 操作符类）

**总结**：

BSON 类型提供了与 JSONB 部分相似的操作符（`->` 和 `->>`），但整体上使用不同的操作符集和函数集，主要面向 MongoDB 兼容的查询语义。如果需要使用 PostgreSQL 的 JSON 函数和 JSON Path 查询，应该使用 JSONB 类型；如果需要 MongoDB 兼容性和 BSON 原生存储，则使用 BSON 类型。

### 2.1.4 BSON 类型与 JSONB 类型的对比

**为什么 DocumentDB 要自己开发 BSON 类型而不是直接使用 PostgreSQL 的 JSONB？**

这是 DocumentDB 的核心设计决策，主要原因包括数据类型兼容性、字段顺序保持、性能优化和 MongoDB 兼容性需求。

**数据类型差异（核心原因）**：

**JSONB 不支持或处理不当的 BSON 类型**：

1. **数值类型区分**：
    - **JSONB**：不区分 Int32、Int64 和 Double，所有数字统一存储为 numeric 类型
    - **BSON**：严格区分 Int32（32位整数）、Int64（64位整数）和 Double（64位浮点数）
    - **影响**：MongoDB 的查询和比较操作依赖精确的类型区分，例如 `{age: 30}` 和 `{age: 30.0}` 在 MongoDB 中是不同的

2. **ObjectId 类型**：
    - **JSONB**：不支持 ObjectId，只能存储为字符串
    - **BSON**：原生支持 ObjectId（12字节二进制标识符）
    - **影响**：ObjectId 是 MongoDB 的默认主键类型，包含时间戳、机器ID、进程ID和计数器信息，无法用字符串完全表示

3. **Date 和 Timestamp 类型**：
    - **JSONB**：日期时间只能存储为 ISO 8601 字符串或数字时间戳
    - **BSON**：原生支持 Date（毫秒时间戳）和 Timestamp（秒+递增计数器）
    - **影响**：MongoDB 的日期操作和时区处理依赖原生日期类型

4. **Binary 类型**：
    - **JSONB**：二进制数据需要 Base64 编码为字符串
    - **BSON**：原生支持 Binary 类型，包含子类型（subtype）信息
    - **影响**：二进制数据存储效率低，且无法保留子类型信息（如 UUID、MD5 等）

5. **Decimal128 类型**：
    - **JSONB**：不支持高精度十进制数，只能使用 numeric 类型（精度有限）
    - **BSON**：原生支持 Decimal128（128位十进制浮点数）
    - **影响**：金融和科学计算需要精确的十进制表示，避免浮点数精度损失

6. **其他特殊类型**：
    - **BSON 支持但 JSONB 不支持**：Regex（正则表达式）、Code（JavaScript 代码）、DBPointer（数据库指针）、Symbol（符号）、MinKey/MaxKey（最小/最大键值）

**字段顺序保持**：

- **JSONB**：存储时会重新排序字段，按字母顺序排列，丢失原始字段顺序
- **BSON**：严格保持字段的插入顺序
- **影响**：MongoDB 的某些操作（如 `$positional` 更新操作符）依赖字段顺序，JSONB 无法保证

**存储格式差异**：

- **JSONB**：使用 PostgreSQL 内部的二进制格式（类似 JSON 的树形结构），需要解析和序列化
- **BSON**：使用 MongoDB 标准的二进制格式，与 MongoDB 客户端完全兼容
- **影响**：
    - **性能**：BSON 可以直接与 MongoDB 客户端交换数据，无需格式转换
    - **兼容性**：数据可以直接导入/导出 MongoDB，保证完全兼容

**性能差异**：

1. **序列化/反序列化开销**：
    - **JSONB**：读取时需要从内部格式解析，写入时需要序列化
    - **BSON**：以原始二进制格式存储，读取时无需解析，直接操作二进制数据
    - **性能提升**：BSON 避免了格式转换开销，特别是在频繁读写场景下

2. **内存使用**：
    - **JSONB**：使用树形结构，需要额外的指针和元数据
    - **BSON**：紧凑的二进制格式，内存占用更小
    - **性能提升**：减少内存占用，提高缓存效率

3. **索引效率**：
    - **JSONB**：使用 GIN 索引，适合全文检索和包含查询
    - **BSON**：支持 B-Tree、GIN、RUM 等多种索引，针对 MongoDB 查询模式优化
    - **性能提升**：BSON 索引针对文档查询优化，支持路径索引、复合索引等

**使用场景对比**：

| 特性 | JSONB | BSON |
|------|-------|------|
| **适用场景** | PostgreSQL 原生 JSON 处理 | MongoDB 兼容性需求 |
| **数据类型** | 基础 JSON 类型 | 完整的 BSON 类型系统 |
| **字段顺序** | 不保持 | 严格保持 |
| **MongoDB 兼容** | 不兼容 | 完全兼容 |
| **性能** | 适合 PostgreSQL 生态 | 针对文档操作优化 |
| **查询语法** | PostgreSQL JSON 函数 | MongoDB 查询语义 |

**实际案例：FerretDB 的转型**：

FerretDB 早期使用 JSONB 实现 MongoDB 兼容性，但在 2.0 版本中全面转向 DocumentDB 的 BSON 实现。主要原因：

1. **类型丢失**：JSONB 无法准确表示 ObjectId、Decimal128 等类型
2. **兼容性问题**：字段顺序丢失导致某些 MongoDB 操作失败
3. **性能瓶颈**：频繁的格式转换影响性能
4. **维护成本**：需要在 JSONB 基础上实现大量兼容层代码

**总结**：

DocumentDB 开发 BSON 类型而非使用 JSONB 的核心原因：

1. **数据类型完整性**：BSON 支持 MongoDB 的所有数据类型，JSONB 无法满足
2. **MongoDB 兼容性**：字段顺序、类型精度、二进制格式等必须完全兼容
3. **性能优化**：原生二进制存储，避免序列化开销
4. **数据保真**：确保数据在存储和传输过程中不丢失信息
5. **生态兼容**：可以直接与 MongoDB 工具和客户端交互

因此，BSON 类型是 DocumentDB 实现 MongoDB 兼容性的技术基础，JSONB 虽然强大，但无法满足 MongoDB 兼容性的严格要求。

**使用场景**：

- **混合存储**：在关系型表中存储部分文档数据，结合关系型字段和文档字段的优势
- **元数据存储**：使用 BSON 存储灵活的元数据，同时使用关系型字段存储结构化数据
- **渐进式迁移**：从关系型表逐步迁移到文档存储，或反之
- **多模型数据库**：在同一个数据库中同时使用关系型和文档型数据模型

### 8.5 DocumentDB 双接口架构

DocumentDB 支持**两套接口**来操作集合，两套接口最终都调用相同的底层 SQL 函数，确保行为一致性和数据一致性。

**接口一：SQL API（自创的 SQL 函数接口）**

通过 `documentdb_api` schema 中的 SQL 函数直接操作集合，这是 DocumentDB 自创的 SQL 接口：

- 所有函数位于 `documentdb_api` schema 中
- 使用标准的 PostgreSQL SQL 语法调用函数
- 适合需要与 PostgreSQL 原生功能集成的场景
- 可以在 SQL 中直接使用，支持事务、JOIN 等 PostgreSQL 特性

**接口二：MongoDB 协议（通过网关）**

通过 `pg_documentdb_gw` 网关支持 MongoDB Wire Protocol，实现 MongoDB 客户端兼容：

- 网关监听端口（默认 10260 或 27017）
- 支持标准的 MongoDB 客户端（如 mongosh、pymongo 等）
- 网关解析 MongoDB Wire Protocol，将命令转换为 SQL 函数调用
- 适合需要 MongoDB 兼容性的场景，可以直接使用现有的 MongoDB 工具和驱动

**两套接口的关系**：

两套接口**共享相同的底层实现**：

1. **SQL API**：直接调用 `documentdb_api.*` 函数
   ```sql
   SELECT documentdb_api.insert_one('db', 'collection', '{"key": "value"}');
   ```

2. **MongoDB 协议**：网关将 MongoDB 命令转换为相同的 SQL 函数调用
   ```javascript
   // MongoDB 客户端
   db.collection.insertOne({key: "value"})
   // ↓ 网关转换
   // SELECT * FROM documentdb_api.insert($1, $2, $3, NULL)
   ```

**转换示例**：

- MongoDB `find` 命令 → 网关转换 → `documentdb_api.find_cursor_first_page` SQL 函数
- MongoDB `insert` 命令 → 网关转换 → `documentdb_api.insert` SQL 函数
- MongoDB `update` 命令 → 网关转换 → `documentdb_api.update` SQL 函数
- MongoDB `delete` 命令 → 网关转换 → `documentdb_api.delete` SQL 函数
- MongoDB `aggregate` 命令 → 网关转换 → `documentdb_api.aggregate_cursor_first_page` SQL 函数

**技术实现**：

网关内部维护一个查询目录（Query Catalog），将每个 MongoDB 命令映射到对应的 SQL 函数调用模板。当网关接收到 MongoDB 命令时：

1. 解析 MongoDB Wire Protocol，提取命令类型和参数
2. 根据命令类型查找对应的 SQL 模板（如 `find_cursor_first_page`）
3. 将 MongoDB 命令参数（BSON 格式）作为参数绑定到 SQL 函数
4. 执行参数化 SQL 查询，调用相同的底层函数
5. 将结果封装为 MongoDB 协议响应返回

**优势**：

- **统一实现**：两套接口共享相同的底层函数，确保行为一致
- **灵活选择**：用户可以根据场景选择 SQL API 或 MongoDB 协议
- **完全兼容**：MongoDB 协议接口完全兼容 MongoDB 客户端和工具
- **深度集成**：SQL API 可以充分利用 PostgreSQL 的特性（事务、JOIN、函数等）

### 8.6 DocumentDB SQL API 使用

DocumentDB 提供了完整的 SQL API，允许通过 SQL 语句直接操作集合，无需使用 MongoDB 协议。所有 API 函数位于 `documentdb_api` schema 中。

**重要说明**：

DocumentDB 不使用传统的 `CREATE TABLE` 语句创建集合，而是使用专门的 API 函数。集合在底层对应 PostgreSQL 表（位于 `documentdb_data` schema），但用户通过 `documentdb_api` 中的函数进行操作，这样可以保证 MongoDB 兼容性和数据一致性。

**创建集合**：

使用 `documentdb_api.create_collection` 函数创建集合：

```sql
SELECT documentdb_api.create_collection('database_name', 'collection_name');
```

示例：
```sql
SELECT documentdb_api.create_collection('documentdb', 'patient');
```

**插入文档（Create）**：

使用 `documentdb_api.insert_one` 函数插入单个文档：

```sql
SELECT documentdb_api.insert_one('database_name', 'collection_name', '{"field": "value"}');
```

示例：
```sql
SELECT documentdb_api.insert_one('documentdb', 'patient', 
    '{"patient_id": "P001", "name": "Alice Smith", "age": 30}');
```

也可以使用 `documentdb_api.insert` 函数批量插入多个文档。

**查询文档（Read）**：

方式一：使用 `documentdb_api.collection` 函数查询所有文档：

```sql
SELECT document FROM documentdb_api.collection('database_name', 'collection_name');
```

示例：
```sql
SELECT document FROM documentdb_api.collection('documentdb', 'patient');
```

方式二：使用 `documentdb_api.find_cursor_first_page` 函数进行条件查询：

```sql
SET search_path TO documentdb_api, documentdb_core;
SET documentdb_core.bsonUseEJson TO true;

SELECT cursorPage FROM documentdb_api.find_cursor_first_page(
    'documentdb', 
    '{"find": "patient", "filter": {"patient_id": "P001"}}'
);
```

支持 MongoDB 查询语法，包括范围查询、逻辑操作符等：

```sql
SELECT cursorPage FROM documentdb_api.find_cursor_first_page(
    'documentdb', 
    '{"find": "patient", "filter": {"$and": [{"age": {"$gte": 30}}, {"age": {"$lte": 50}}]}}'
);
```

**更新文档（Update）**：

使用 `documentdb_api.update` 函数更新文档：

```sql
SELECT documentdb_api.update('database_name', '{"update": "collection_name", "updates": [...]}');
```

更新单个文档示例：
```sql
SELECT documentdb_api.update('documentdb', 
    '{"update": "patient", "updates": [{"q": {"patient_id": "P001"}, "u": {"$set": {"age": 31}}}]}'
);
```

更新多个文档（使用 `multi: true`）：
```sql
SELECT documentdb_api.update('documentdb', 
    '{"update": "patient", "updates": [{"q": {}, "u": {"$set": {"status": "active"}}, "multi": true}]}'
);
```

支持 MongoDB 更新操作符，如 `$set`、`$unset`、`$inc` 等。

**删除文档（Delete）**：

使用 `documentdb_api.delete` 函数删除文档：

```sql
SELECT documentdb_api.delete('database_name', '{"delete": "collection_name", "deletes": [...]}');
```

删除单个文档示例：
```sql
SELECT documentdb_api.delete('documentdb', 
    '{"delete": "patient", "deletes": [{"q": {"patient_id": "P001"}, "limit": 1}]}'
);
```

删除多个文档（不设置 `limit` 或 `limit: 0`）：
```sql
SELECT documentdb_api.delete('documentdb', 
    '{"delete": "patient", "deletes": [{"q": {"age": {"$lt": 18}}}]}'
);
```

**其他常用操作**：

- **删除集合**：`documentdb_api.drop_collection('database_name', 'collection_name')`
- **列出集合**：`documentdb_api.list_collections_cursor_first_page(...)`
- **创建索引**：`documentdb_api.create_indexes_background(...)`
- **聚合查询**：`documentdb_api.aggregate_cursor_first_page(...)`

**与标准 SQL 的混合使用**：

可以在同一个事务中混合使用 DocumentDB API 和标准 PostgreSQL SQL：

```sql
BEGIN;

-- 使用 DocumentDB API 插入文档
SELECT documentdb_api.insert_one('mydb', 'users', '{"name": "John", "age": 30}');

-- 使用标准 SQL 操作普通表
INSERT INTO regular_table (id, name) VALUES (1, 'John');

-- 甚至可以在 SQL 中 JOIN DocumentDB 集合和普通表
SELECT u.document, r.name 
FROM documentdb_api.collection('mydb', 'users') u
JOIN regular_table r ON (u.document->>'name') = r.name;

COMMIT;
```

**注意事项**：

- 所有文档数据以 JSON 字符串形式传入，DocumentDB 会自动转换为 BSON 格式存储
- 查询结果默认返回 BSON 格式，可以通过设置 `documentdb_core.bsonUseEJson` 为 `true` 来以 JSON 格式显示
- 更新和删除操作使用 MongoDB 命令格式的 JSON 字符串
- 所有操作都支持事务，可以与其他 PostgreSQL 操作混合使用

### 8.7 网关部署

网关是独立的 Rust 服务：

- **监听端口**: 默认 10260（可配置为 27017），支持自定义端口
- **配置**: 通过环境变量或配置文件进行配置
- **TLS**: 支持 TLS 加密连接，保证数据传输安全

## 9. 测试与验证

### 9.1 回归测试

PostgreSQL 回归测试框架：

- **SQL 测试**: 覆盖 BSON 操作、聚合管道、索引等核心功能
- **预期输出**: 对比实际输出与预期输出，验证功能正确性
- **覆盖范围**: 包括数据类型、操作符、索引、查询等各个方面

### 9.2 集成测试

网关集成测试：

- **协议测试**: 验证协议解析和命令路由的正确性
- **命令处理**: 测试各种 MongoDB 命令的处理逻辑
- **事务测试**: 验证事务管理的正确性和一致性

## 10. 技术亮点总结

1. **原生 BSON 存储**: 二进制格式存储，避免转换开销，保证数据完整性
2. **深度 PostgreSQL 集成**: 充分利用查询优化器、并行执行、JIT 等特性，获得最佳性能
3. **智能查询编译**: 聚合管道编译为优化的 SQL，利用基于代价的优化器（CBO）
4. **多类型索引**: B-Tree、GIN、RUM、向量索引的统一支持，满足各种查询需求
5. **高性能网关**: Rust 实现，异步 I/O，连接池优化，支持高并发
6. **完整事务支持**: ACID 事务，多隔离级别，保证数据一致性
7. **向量搜索**: 原生 pgvector 集成，混合检索优化，支持 AI 应用
8. **分布式就绪**: Citus 集成，支持水平扩展，满足大规模部署需求

## 11. 技术债务与限制

1. **连接模型**: PostgreSQL 进程模型限制并发连接数，需要配合连接池使用
2. **大文档**: 超大文档（超过 1MB）可能影响性能，建议拆分或使用外部存储
3. **分片体验**: 分布式配置不如 MongoDB 原生分片简单，需要更多配置
4. **兼容性**: 某些边缘 MongoDB 特性可能不完全支持，需要持续改进
5. **内存使用**: BSON 文档在内存中需要完整加载，大文档会占用较多内存

## 12. 未来发展方向

1. **性能优化**: 进一步优化 BSON 操作性能，减少 CPU 和内存开销
2. **兼容性**: 持续提升 MongoDB API 兼容性，支持更多 MongoDB 特性
3. **分布式**: 改进分布式部署体验，简化配置和管理
4. **新特性**: 支持更多 MongoDB 新特性，保持与 MongoDB 的同步
5. **工具生态**: 完善管理工具和监控支持，提升运维体验
