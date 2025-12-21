# DocumentDB 开源项目深度研究报告：重塑文档数据库标准的架构、生态与战略解析

## 1. 执行摘要与项目背景

在云原生数据基础设施的演进历程中，关系型数据库与 NoSQL 数据库的边界日益模糊。DocumentDB（GitHub: documentdb/documentdb）的出现，标志着这一融合趋势达到了新的高度。该项目是一个基于 PostgreSQL 构建的开源文档数据库引擎，其核心愿景是提供与 MongoDB API 完全兼容的原生文档存储与处理能力，同时继承 PostgreSQL 坚若磐石的稳定性、扩展性及庞大的生态系统。

必须首先明确的是，本报告所探讨的"DocumentDB"特指由 Microsoft 最初发起、现已捐赠给 Linux 基金会（Linux Foundation）托管的开源项目，而非 Amazon Web Services (AWS) 提供的同名托管服务"Amazon DocumentDB"。尽管两者名称相似且均致力于兼容 MongoDB 协议，但其技术路线、开源属性及治理模式存在本质区别。开源 DocumentDB 采用极其宽松的 MIT 许可证，旨在打破当前文档数据库市场因 MongoDB 修改开源协议（转向 SSPL）而形成的"技术孤岛"与"供应商锁定"局面。

本报告将基于详尽的技术文档、社区讨论及行业分析，全方位剖析 DocumentDB 项目。我们将深入探讨其从 Microsoft Azure 内部孵化到成为 Linux 基金会顶级项目的历史沿革；解构其"三位一体"的技术架构——即如何在关系型数据库内核中实现原生的 BSON 存储与计算；评估其在复杂聚合查询与高并发写入下的性能表现；并将其置于宏观的数据库竞争格局中，分析其与 MongoDB、FerretDB、Amazon DocumentDB 及 YugabyteDB 等竞品的错综关系。通过这些分析，本报告旨在揭示 DocumentDB 如何通过标准化和开放生态，重新定义现代应用的数据层架构。

---

## 2. 产品定位与核心价值主张

### 2.1 填补"后 SSPL 时代"的市场真空

DocumentDB 的产品定位不仅是一个技术解决方案，更是对当前开源软件许可环境的一种战略回应。自 MongoDB 将其开源许可证更改为服务器端公共许可证（SSPL）以来，开源社区、云服务提供商以及对合规性要求严格的企业陷入了两难境地：SSPL 并非开放源代码促进会（OSI）认可的开源协议，这限制了其在某些商业场景下的自由使用和分发。

DocumentDB 选择 **MIT 许可证** 这一极具包容性的协议，精准地切入了这一市场痛点。MIT 协议赋予了用户几乎无限的自由——无论是个人开发者、初创企业还是超大规模的云厂商，均可自由地使用、修改、分发甚至将其集成到商业产品中，而无需担心复杂的法律风险或开源回馈义务。这种彻底的开放性（Developer Freedom）是 DocumentDB 区别于 MongoDB 及其他"源码可用（Source Available）"产品的核心护城河。

### 2.2 真正的"PostgreSQL 原生"多模引擎

不同于早期的"SQL 转换器"方案（如早期的 FerretDB 版本仅作为 SQL 翻译层），DocumentDB 的定位是 PostgreSQL 的原生扩展（Native Extension）。它并非简单地将 JSON 数据存储在文本字段中，而是在 PostgreSQL 内部实现了完整的 BSON（Binary JSON）数据类型及其操作符族。

这意味着 DocumentDB 将 PostgreSQL 转变为了一个真正的多模数据库（Multi-Model Database）。用户可以在同一个数据库实例中，既运行严格模式的关系型 SQL 业务，又运行灵活模式的文档型 NoSQL 业务。这种定位带来了两大核心价值：

1. **架构简化：** 企业无需维护两套独立的数据库栈（RDBMS + NoSQL），降低了运维复杂度和总拥有成本（TCO）。
2. **数据互操作性：** 虽然目前主要关注兼容性，但基于 Postgres 的架构为未来实现 SQL 与 NoSQL 数据的直接 JOIN 提供了理论基础。

### 2.3 任务关键型（Mission-Critical）文档数据库

DocumentDB 的另一个关键定位是面向"任务关键型"应用。借助 PostgreSQL 三十余年的工程积累，DocumentDB 从诞生的第一天起就继承了 ACID 事务、多版本并发控制（MVCC）、时间点恢复（PITR）以及经过实战检验的复制与高可用机制。这使得它不仅适用于像内容管理系统这样的简单读写场景，更能胜任金融交易、库存管理等对数据一致性要求极高的核心业务场景。

---

## 3. 历史发展与治理架构演进

### 3.1 孵化阶段：Azure Cosmos DB 的内核革新

DocumentDB 的代码库源于 Microsoft Azure 数据团队的内部创新。Microsoft 在运营 Azure Cosmos DB 的过程中，不仅需要支持自研的 NoSQL API，也需要响应大量用户对 MongoDB 协议的强烈需求。早期的 Cosmos DB 采用的是专有的后端存储引擎，虽然性能强劲，但在与开源生态（尤其是 PostgreSQL 生态）的融合上存在隔阂。

为了构建下一代"Azure Cosmos DB for MongoDB (vCore)"服务，Microsoft 决定不再依赖专有黑盒，而是拥抱 PostgreSQL。这一决策基于 PostgreSQL 强大的扩展机制——它允许开发者通过挂钩（Hooks）和自定义数据类型，深度介入数据库的解析、规划和执行过程。经过数年的内部研发与打磨，这个基于 Postgres 的文档引擎在 Azure 内部不仅支撑了 vCore 服务的高速增长，也验证了其在大规模生产环境下的稳定性。

### 3.2 开源阶段：从私有代码到社区资产

2024 年至 2025 年初，Microsoft 逐步推进该引擎的开源化进程。2025 年 1 月 23 日，Microsoft 正式宣布 DocumentDB 项目全面开源，并在 GitHub 上建立了 documentdb 组织。这一举措不仅是对代码的公开，更是开发模式的根本性转变。项目明确了基于 PostgreSQL 17 构建的技术路线，并邀请社区参与贡献。

此时，DocumentDB 已经不仅仅是 Azure 的一个组件，它开始吸引其他数据库厂商的目光。最引人注目的事件是 **FerretDB 的战略转型**。FerretDB 本是一个旨在提供 MongoDB 兼容性的开源项目，早期采用 Go 语言编写代理层，后端通过 JSONB 存储。然而，JSONB 在处理 MongoDB 特有的数据类型（如 Decimal128、ObjectId）和排序规则时存在天然的阻抗失配。2025 年 2 月，FerretDB 宣布在其 2.0 版本中全面采用 DocumentDB 的扩展作为底层引擎，放弃了原有的 JSONB 实现路径。这一"分久必合"的趋势表明，DocumentDB 正在成为开源文档数据库领域的事实标准内核。

### 3.3 治理成熟：加入 Linux 基金会

2025 年 8 月 25 日，在阿姆斯特丹举行的开源峰会上，DocumentDB 项目迎来了一个历史性的转折点——正式加入 **Linux 基金会**。这一举动具有深远的战略意义：

* **中立性确立：** 通过将项目所有权移交给中立的非营利组织，彻底消除了"Microsoft 控制"的标签。这对于吸引竞争对手（如 AWS、Google）参与贡献至关重要。
* **全行业背书：** 随着加入 Linux 基金会，一个全明星阵容的技术指导委员会（TSC）随之成立。成员不仅包括发起者 Microsoft，还囊括了 Amazon Web Services (AWS)、Google Cloud、Yugabyte、Cockroach Labs、Rippling 等行业巨头。这种跨越云厂商竞争关系的合作在数据库历史上极为罕见，表明各方已达成共识：建立一个统一、开放的文档数据库标准符合所有人的利益。
* **开放标准愿景：** 项目不再满足于"兼容 MongoDB"，而是提出了建立"文档数据库开放标准"（Open Standard for Document Databases）的宏大愿景，意图在 NoSQL 领域复制 SQL 在关系型数据库领域的标准化成功。

---

## 4. 技术架构深度解析

DocumentDB 的架构设计展示了极高的工程水准，它没有选择简单的"中间件"模式，而是采用了"数据库内扩展（In-Database Extension）"的深度集成模式。整个系统由三个核心组件构成，它们协同工作，在 PostgreSQL 的框架内实现了完整的文档数据库功能。

### 4.1 架构总览与组件交互

系统采用了经典的分层架构，实现了协议接入、查询转换与底层存储的解耦。这种设计不仅保证了性能，还提供了极高的部署灵活性。

| 组件名称 | 类型 | 核心职责 | 技术栈 |
| :---- | :---- | :---- | :---- |
| **pg_documentdb_gw** | 网关服务 | 协议转换、连接管理、查询路由 | Rust 语言 |
| **pg_documentdb** | Postgres 扩展 (API) | 用户接口、聚合管道编译、逻辑规划 | SQL / PL/pgSQL |
| **pg_documentdb_core** | Postgres 扩展 (内核) | BSON 数据类型存储、低级操作符、索引访问方法 | C 语言 |

交互流程解析：  
　　当一个客户端（如使用 Python 的 pymongo）发起请求时，数据流经如下：

1. **协议握手：** 客户端连接到 pg_documentdb_gw 监听的端口（默认为 10260 或 27017）。网关模拟 MongoDB 的 Wire Protocol，处理握手、认证（SASL/SCRAM）等初始交互。
2. **命令解析：** 网关接收到 BSON 格式的命令（如 insert, find, aggregate），对其进行解析，并根据内部逻辑将其映射为对应的 PostgreSQL SQL 查询。例如，一个 db.collection.find({x: 1}) 请求会被转换为调用 pg_documentdb 扩展函数的 SQL 语句。
3. **查询下推：** SQL 语句被发送到 PostgreSQL 后端。
4. **执行规划：** PostgreSQL 的查询优化器介入，结合 pg_documentdb 提供的统计信息和索引定义，生成最优的执行计划。
5. **核心处理：** 执行计划调用 pg_documentdb_core 中的 C 函数，直接在 PostgreSQL 的堆表（Heap）和索引中操作二进制 BSON 数据，完成数据的读取、过滤和聚合。
6. **结果回传：** 结果集以二进制形式返回给网关，网关将其封装为 MongoDB 协议响应包，发送回客户端。

### 4.2 核心组件一：pg_documentdb_core（存储引擎基石）

pg_documentdb_core 是整个系统的地基，它通过 C 语言扩展在 PostgreSQL 中引入了原生的 BSON 支持。

* **原生 BSON 类型：** PostgreSQL 默认的 jsonb 类型虽然强大，但在处理 MongoDB 特有的数据类型时存在缺陷。例如，jsonb 不区分 32 位整数和 64 位浮点数，不支持 ObjectId、Date、Binary 等 BSON 特有类型，且在序列化时会打乱字段顺序（MongoDB 的某些操作依赖字段顺序）。DocumentDB 引入了全新的 bson 数据类型，确保数据以完全保真的二进制格式存储，无需在存储层进行有损转换。
* **TOAST 机制优化：** 针对大文档存储，扩展利用了 PostgreSQL 的 TOAST（The Oversized-Attribute Storage Technique）机制，自动对超过页面大小的文档进行压缩和切片存储，保证了主表的扫描效率。
* **低级操作符优化：** 为了支撑高性能查询，核心层实现了一系列汇编级或 C 级优化的操作符，如 `bson_get_value`、`bson_compare` 等。这些函数能够直接在压缩的二进制数据上进行操作，极大减少了 CPU 开销。

### 4.3 核心组件二：pg_documentdb（逻辑与查询编译器）

如果说 Core 层是肌肉，那么 API 层就是大脑。pg_documentdb 扩展主要负责逻辑层面的处理。

* **聚合管道转译（Aggregation Pipeline Compilation）：** 这是 DocumentDB 技术含量最高的部分之一。它包含了一个复杂的编译器，能够将 MongoDB 的聚合管道（Aggregation Pipeline）——一种基于阶段的数据处理流——动态编译为 PostgreSQL 的查询树（Query Tree）。
    * 例如，MongoDB 的 $match 阶段会直接映射为 SQL 的 WHERE 子句。
    * $group 阶段映射为 SQL 的 GROUP BY。
    * $lookup（左连接）映射为 SQL 的 LEFT JOIN。
    * 这种转译使得 DocumentDB 能够利用 PostgreSQL 极其成熟的基于代价的优化器（CBO），在执行复杂的 JOIN 和聚合操作时，往往能获得比 MongoDB 原生引擎更好的执行计划（例如自动选择 Hash Join 或 Nested Loop Join）。
* **索引管理接口：** 提供了创建和管理索引的接口。它不仅支持标准的 B-Tree 索引，还集成了 RUM 索引（一种增强的 GIN 索引）来支持高效的全文检索和数组包含查询。

### 4.4 核心组件三：pg_documentdb_gw（协议网关）

网关层解决了"最后一公里"的兼容性问题。

* **无状态设计：** 网关本身设计为无状态（Stateless），这使得它可以轻松地在 Kubernetes 中进行水平扩展（ReplicaSet），通过负载均衡器分发流量。
* **连接池管理：** 网关内置了智能的连接池逻辑，能够复用后端 PostgreSQL 的连接，减少连接建立的开销（Connection Storm），支持高并发的短连接请求。
* **游标管理：** 实现了 MongoDB 的游标（Cursor）逻辑，支持 getMore 操作，允许客户端分批次拉取大量数据。

---

## 5. 关键技术特性与实现细节

### 5.1 索引机制的革新

DocumentDB 并没有简单沿用 Postgres 的 GIN 索引，而是针对文档查询模式进行了深度定制。

* **RUM 索引集成：** GIN（Generalized Inverted Index）索引在处理全文检索时表现优异，但在需要按排名（Ranking）或其它字段排序时效率较低。DocumentDB 引入了 RUM 索引支持，这种索引结构在倒排列表中存储了额外的位置信息和元数据，使得带排序的全文检索性能大幅提升。
* **通配符索引（Wildcard Indexes）：** 针对文档结构不固定的场景，DocumentDB 支持 MongoDB 风格的通配符索引。用户可以对 `**` 建立索引，系统会自动遍历文档中的所有字段并将其键值对插入索引中。这对动态 Schema 的应用至关重要。
* **部分索引（Partial Indexes）：** 利用 Postgres 原生的部分索引能力，DocumentDB 允许用户仅对满足特定过滤器（如 status: "active"）的文档建立索引，从而显著降低索引大小和写入开销。

### 5.2 向量搜索（Vector Search）与 AI 就绪

在生成式 AI 爆发的背景下，向量数据库成为基础设施的新宠。DocumentDB 通过集成 **pgvector** 扩展，提供了原生的向量搜索能力，使其成为构建 RAG（检索增强生成）应用的理想选择。

* **语法融合：** DocumentDB 扩展了聚合管道语法，支持 $search 阶段（或类似的自定义阶段），允许用户直接在文档查询中嵌入向量相似度搜索。
* **混合检索（Hybrid Search）：** 得益于 Postgres 的执行引擎，用户可以在同一个查询中极其高效地组合向量搜索（HNSW 索引）和标量过滤（B-Tree/GIN 索引）。例如，"查找与查询向量最相似且属于'科技'类别的最近 10 篇文章"。这种混合检索在纯向量数据库中往往实现复杂且性能不佳，而在 DocumentDB 中则是自然而然的能力。

### 5.3 事务与一致性（ACID）

DocumentDB 天然继承了 PostgreSQL 严格的 ACID 事务特性。

* **多文档事务：** 与早期 MongoDB 仅支持单文档原子性不同，DocumentDB 从底层存储上就支持跨集合（Table）、跨文档（Row）的原子事务。
* **隔离级别：** 支持 PostgreSQL 的所有隔离级别（Read Committed, Repeatable Read, Serializable），为金融级应用提供了坚实的一致性保障。
* **实现方式：** 网关层负责追踪事务状态，将 MongoDB 的 startSession, startTransaction, commitTransaction 命令映射为 SQL 的 BEGIN, COMMIT, ROLLBACK。

### 5.4 变更流（Change Streams）

实时数据处理是现代应用的核心需求。DocumentDB 实现了 MongoDB 的 Change Streams 功能，允许应用实时订阅数据变更。

* **基于 WAL 的实现：** 该功能通过 PostgreSQL 的逻辑解码（Logical Decoding）插件实现。系统监听 PostgreSQL 的预写式日志（WAL），将其中的变更事件解析并转换为 BSON 格式的变更通知流。
* **零性能损耗：** 这种基于日志的机制将变更捕获的开销从主事务路径中剥离，极大地降低了对数据库写入性能的影响。

---

## 6. 性能特性与优化机制

DocumentDB 的性能优势源于其基于 PostgreSQL 的架构设计，通过原生 BSON 存储、查询优化器集成和高效的索引机制，在多个维度实现了优异的性能表现。

### 6.1 查询性能优化

**基于代价的查询优化器（CBO）：** DocumentDB 将 MongoDB 聚合管道编译为 PostgreSQL 查询树，充分利用 PostgreSQL 成熟的基于代价的优化器。优化器能够根据统计信息、索引定义和数据分布，自动选择最优的执行策略。对于复杂的 JOIN 操作（如 $lookup），优化器会根据数据量大小自动选择 Hash Join、Nested Loop Join 或 Merge Join，这比 MongoDB 的固定执行策略更加灵活高效。

**聚合管道编译优化：** DocumentDB 的聚合管道编译器能够将多个阶段（$match、$group、$lookup 等）合并优化，减少中间结果集的大小。例如，$match 阶段会直接下推为 SQL 的 WHERE 子句，在数据扫描阶段就过滤掉不符合条件的文档，避免在内存中处理大量无用数据。

**查询计划缓存：** DocumentDB 实现了查询计划缓存机制，对于相同模式的查询可以复用已优化的执行计划，减少重复的优化开销，特别是在高并发场景下能够显著降低 CPU 使用率。

### 6.2 存储与 I/O 性能

**原生 BSON 存储：** DocumentDB 在 PostgreSQL 中实现了原生的 BSON 数据类型，数据以二进制格式直接存储在堆表中，避免了 JSONB 的序列化/反序列化开销。这种设计使得数据读取时无需进行格式转换，直接操作二进制数据，极大提升了查询效率。

**TOAST 机制优化：** 对于超过 PostgreSQL 页面大小（通常 8KB）的大文档，DocumentDB 利用 TOAST（The Oversized-Attribute Storage Technique）机制自动进行压缩和切片存储。这不仅减少了存储空间，还提高了主表的扫描效率，因为大文档被存储在独立的 TOAST 表中，主表扫描时只需读取文档的引用指针。

**二进制下推（Binary Pushdown）：** DocumentDB 尽可能将过滤条件和投影操作下推到存储层执行。BSON 操作符（如 `bson_get_value`、`bson_compare`）能够在二进制数据上直接操作，不满足条件的数据在从磁盘读取后立即被丢弃，无需进入上层处理逻辑，极大减少了 I/O 和内存带宽的浪费。

**缓冲区管理：** 继承自 PostgreSQL 的成熟缓冲区管理器（Buffer Manager），DocumentDB 能够智能地管理内存中的页面缓存。结合操作系统级别的 Page Cache，即使数据集超过可用内存，仍能通过 LRU 等策略保持较高的缓存命中率，保证查询性能的稳定性。

### 6.3 写入性能特性

**WAL 机制：** DocumentDB 利用 PostgreSQL 的预写式日志（WAL）机制，所有数据修改先写入 WAL，然后异步刷新到数据文件。这种设计使得事务提交时只需等待 WAL 写入完成，而不需要等待数据文件同步，大大降低了写入延迟。WAL 还支持流式复制，为高可用部署提供了高效的数据同步机制。

**追加写入优化：** PostgreSQL 的 Heap 存储模型采用追加写入（Append-only）策略，新数据直接追加到表的末尾，避免了随机写入带来的性能损耗。这种设计特别适合高吞吐的插入场景，能够充分利用顺序 I/O 的高性能。

**连接池管理：** pg_documentdb_gw 网关内置了智能连接池，能够复用后端 PostgreSQL 连接，减少连接建立和认证的开销。网关还支持连接预热和健康检查，确保高并发场景下的连接可用性。

### 6.4 索引性能机制

**多类型索引支持：** DocumentDB 不仅支持标准的 B-Tree 索引用于精确匹配和范围查询，还集成了 GIN 和 RUM 索引用于全文检索和数组查询。RUM 索引在 GIN 的基础上增加了位置信息存储，使得带排序的全文检索性能大幅提升，避免了 GIN 索引需要回表排序的开销。

**通配符索引优化：** 对于动态 Schema 的文档，DocumentDB 支持通配符索引（Wildcard Indexes），能够自动索引文档中的所有字段。这种索引在写入时会有一定开销，但能够显著提升对未知字段的查询性能，特别适合文档结构不固定的应用场景。

**部分索引：** DocumentDB 支持 PostgreSQL 的部分索引功能，允许仅对满足特定条件的文档建立索引。例如，可以只为 `status: "active"` 的文档建立索引，这样既减少了索引大小，又降低了写入开销，同时保持了查询性能。

### 6.5 并发与并行处理

**MVCC 并发控制：** DocumentDB 继承 PostgreSQL 的多版本并发控制（MVCC）机制，读操作不会阻塞写操作，写操作也不会阻塞读操作。这种设计使得系统在高并发读写混合负载下仍能保持稳定的性能，避免了传统锁机制带来的性能瓶颈。

**并行查询执行：** PostgreSQL 的并行查询引擎允许 DocumentDB 自动利用多核 CPU 来并行扫描和聚合数据。对于大表的全表扫描、排序和聚合操作，系统会自动启动多个工作进程并行处理，这在分析型查询（OLAP-like）中能够带来数倍的性能提升。

**JIT 编译优化：** 对于复杂的 BSON 表达式和聚合函数，DocumentDB 可以利用 PostgreSQL 的 LLVM JIT 功能，将表达式编译为机器码执行。这在处理大量文档的计算密集型任务中能够带来显著的性能提升，特别是对于包含复杂条件判断和数学运算的查询。

### 6.6 向量搜索性能

**pgvector 集成：** DocumentDB 通过集成 pgvector 扩展提供了高效的向量搜索能力。pgvector 支持 HNSW（Hierarchical Navigable Small World）索引，这是一种近似最近邻搜索算法，能够在高维向量空间中快速找到相似向量。HNSW 索引的查询复杂度接近对数级别，即使对于百万级别的向量数据也能保持毫秒级的响应时间。

**混合检索优化：** 得益于 PostgreSQL 的执行引擎，DocumentDB 能够在同一个查询中高效地组合向量搜索和标量过滤。优化器能够智能地选择先执行向量搜索还是先执行标量过滤，或者并行执行两者后再合并结果，这种混合检索能力在纯向量数据库中往往实现复杂且性能不佳。

### 6.7 性能瓶颈与优化建议

**BSON 转换开销：** 尽管实现了原生 BSON 存储，但在网关层和 PostgreSQL 之间传输数据时，以及在 PostgreSQL 内部进行复杂函数调用时，仍存在一定的序列化/反序列化开销。对于高频的简单查询，这种开销相对明显，建议通过查询优化和索引设计来减少数据传输量。

**连接数限制：** PostgreSQL 传统的进程模型（Process-based）在处理大量空闲连接时资源消耗较高。虽然 DocumentDB 网关内置了连接池，但在极端高并发场景下，仍建议配合 PgBouncer 等连接池中间件来进一步优化连接管理。

**大文档处理：** 虽然 TOAST 机制能够处理大文档，但过大的文档（如超过 1MB）仍会影响查询性能，特别是在需要全文档扫描的场景下。建议将大文档拆分为多个小文档，或者将大字段存储在独立的集合中，通过引用关联。

**索引维护开销：** 通配符索引和全文索引在写入时会有一定的性能开销，特别是在高并发写入场景下。建议根据实际查询模式选择性创建索引，避免过度索引导致的写入性能下降。

---

## 7. 与其他数据库的竞争格局与关系

DocumentDB 的出现重塑了文档数据库的竞争版图。

### 7.1 vs. Amazon DocumentDB (with MongoDB compatibility)

这是最容易混淆的一组关系，但两者有着天壤之别。

* **内核差异：**
    * **Amazon DocumentDB：** 是 AWS 的**专有（Proprietary）**闭源服务。其底层基于 AWS Aurora 分布式存储引擎，上层是一个模拟 MongoDB 3.6/4.0/5.0 协议的仿真层。用户无法获取其源码，也无法在本地运行。
    * **DocumentDB (开源)：** 是**完全开源**的项目，基于标准的 PostgreSQL。它运行在任何支持 Postgres 的地方——笔记本、物理机、虚拟机、Kubernetes 或任何云平台。
* **战略关系：** 极具讽刺意味但也合乎逻辑的是，**AWS 已经正式加入开源 DocumentDB 项目**。这意味着 AWS 可能会在未来利用该开源引擎来增强其托管服务，或者为了降低维护专有模拟层的成本而转向共建开源标准。这也表明开源 DocumentDB 的技术路线得到了云巨头的认可。

### 7.2 vs. MongoDB (Official)

* **许可协议：** MongoDB 采用 SSPL 协议，限制了云厂商的竞争，也给企业内部合规带来了不确定性。DocumentDB 采用 MIT 协议，彻底解放了开发者。
* **生态兼容：** DocumentDB 的目标是 100% 兼容 MongoDB 驱动和工具（如 Compass, mongosh）。虽然目前在某些边缘操作符和极新版本特性上仍有差距，但对于主流应用已基本透明。
* **分布式能力：** MongoDB 的原生分片（Sharding）体验极其流畅。而 DocumentDB 虽然可以利用 Citus 或 Postgres 分区实现扩展，但在配置和管理分布式集群的易用性上，目前仍落后于 MongoDB 的原生体验。

### 7.3 vs. FerretDB

* **由竞转合：** FerretDB 早期是 DocumentDB 的直接竞争对手，试图用 Go + JSONB 解决同样的问题。
* **技术融合：** 随着 FerretDB 2.0 的发布，两者关系转变为"上下游"或"发行版与内核"的关系。FerretDB 2.0 放弃了自研的 JSONB 存储，转而使用 Microsoft 开源的 pg_documentdb 扩展作为其核心引擎。现在的 FerretDB 更像是一个易于使用的打包方案（Distribution），它简化了 DocumentDB 的部署，并提供了额外的工具支持，而核心的数据处理能力则完全依赖 DocumentDB。

### 7.4 vs. Azure Cosmos DB for MongoDB (vCore)

* **同源关系：** **Azure Cosmos DB for MongoDB (vCore) 本质上就是 DocumentDB 的商业托管版本。**
* **商业模式：** 用户在 Azure 上购买该服务，实际上就是在购买由 Microsoft 运维的 DocumentDB 实例。这为 DocumentDB 项目提供了长期的商业造血能力和经过大规模验证的稳定性背书。

---

## 8. 安装、配置与运维指南

### 8.1 部署前置要求

DocumentDB 作为一个 PostgreSQL 扩展，其部署依赖于底层的 Postgres 环境。

* **操作系统：** Linux (RHEL 8/9, Ubuntu 20.04/22.04) 或 macOS。
* **PostgreSQL 版本：** 强烈推荐 PostgreSQL 15 及以上版本，以利用最新的性能优化；v17 是目前开源构建的主要目标版本。
* **硬件架构：** 支持 x86_64 及 ARM64（包括 Apple Silicon），这意味着它可以在从树莓派到高性能服务器的各种硬件上运行。

### 8.2 核心配置详解

要启用 DocumentDB，必须在 postgresql.conf 中进行特定的配置，主要涉及预加载库和扩展管理。

```ini
# postgresql.conf 关键配置

# 1. 预加载库：必须加载 pg_cron（用于定时任务）和 DocumentDB 核心库  
# 注意顺序：pg_documentdb_core 必须在 pg_documentdb 之前  
shared_preload_libraries = 'pg_cron,pg_documentdb_core,pg_documentdb'

# 2. 配置 pg_cron 运行的数据库名称  
# DocumentDB 依赖 pg_cron 来执行后台维护任务（如 TTL 索引清理）  
cron.database_name = 'documentdb'

# 3. 搜索路径（可选但推荐）  
# 将 documentdb_core 和 documentdb_api_catalog 加入搜索路径，方便 SQL 访问  
search_path = 'documentdb_core,documentdb_api_catalog,public'
```

**安装步骤摘要：**

1. 安装 PostgreSQL 及开发头文件。
2. 通过源码编译或包管理器安装 pg_documentdb 扩展。
3. 修改 postgresql.conf 并重启 PostgreSQL 服务。
4. 在目标数据库中执行 CREATE EXTENSION 命令：

```sql
CREATE DATABASE documentdb;
\c documentdb
CREATE EXTENSION IF NOT EXISTS pg_documentdb CASCADE;
-- CASCADE 会自动安装 pg_documentdb_core, pg_cron, vector, postgis 等依赖扩展
```

### 8.3 运维最佳实践

* **高可用（HA）：** 推荐使用 **Patroni** 配合 etcd 来管理 DocumentDB 的高可用集群。Patroni 能够完美识别 PostgreSQL 的状态，并处理主从切换。由于 DocumentDB 的数据存储在标准 Heap 表中，物理流复制（Physical Streaming Replication）完全适用，这比 MongoDB 的逻辑复制集（Replica Set）在数据同步效率上更高。
* **备份恢复：** 可以直接使用 **pgBackRest** 或 **WAL-G** 进行全量和增量备份。这意味着现有的 Postgres 备份策略和工具链可以直接复用，无需为文档数据引入新的备份机制。
* **监控：** 除了通过 MongoDB 协议使用 db.serverStatus() 监控外，还可以直接使用 Prometheus 的 postgres_exporter 监控底层指标（如 Buffer Hit Rate, Checkpoint latency），这提供了比 MongoDB 原生工具更底层的系统洞察。

---

## 9. 结论与未来展望

DocumentDB 开源项目的崛起，不仅仅是多了一个数据库选项，它代表了数据库技术栈的一次重要整合。通过将 MongoDB 优秀的开发者体验（API、文档模型）与 PostgreSQL 卓越的运行时环境（存储引擎、查询优化器、生态系统）相结合，DocumentDB 成功地融合了 NoSQL 的灵活性与 RDBMS 的可靠性。

**主要结论：**

1. **架构的胜利：** 采用"数据库内扩展"而非"外部转换层"的架构，确保了 DocumentDB 在性能和功能深度上能够对齐甚至超越原版 MongoDB。
2. **生态的重构：** 随着 AWS、Google、Microsoft 和 FerretDB 的加入，DocumentDB 有望结束文档数据库领域的碎片化状态，建立一个真正开放、非供应商锁定的行业标准。
3. **未来的主流：** 对于绝大多数新项目，特别是那些需要混合工作负载（Hybrid Workloads）、强一致性保障以及希望避免许可陷阱的项目，DocumentDB 正在成为比 MongoDB 更具吸引力的默认选择。

随着项目的持续演进，特别是未来在分布式分片（Sharding）易用性上的改进，DocumentDB 有望成为云原生时代数据层的核心基石，推动"Postgres for Everything"的理念真正落地。

### 引用的著作

1. [DocumentDB - Open Source Document Database](https://documentdb.io/)
2. [documentdb/documentdb: MongoDB-compatible database engine for cloud-native and open-source workloads. Built for scalability, performance, and developer productivity. - GitHub](https://github.com/documentdb/documentdb)
3. [DocumentDB: Open-Source Announcement](https://opensource.microsoft.com/blog/2025/01/23/documentdb-open-source-announcement)
4. [FerretDB, an Open-Source Alternative to MongoDB, Releases Version 2.0 - InfoQ](https://www.infoq.com/news/2025/02/ferretdb-documentdb/)
5. [Linux Foundation Welcomes DocumentDB to Advance Open, Developer-First NoSQL Innovation](https://www.linuxfoundation.org/press/linux-foundation-welcomes-documentdb-to-advance-open-developer-first-nosql-innovation)
6. [DocumentDB joins the Linux Foundation - Microsoft Open Source Blog](https://opensource.microsoft.com/blog/2025/08/25/documentdb-joins-the-linux-foundation)
7. [Azure DocumentDB: Open source, MongoDB-Compatible Database for Modern Apps](https://rkniyer999.medium.com/azure-documentdb-open-source-mongodb-compatible-database-for-modern-apps-0ace448ec927)
8. [Microsoft CosmosDB: RUM instead of GIN but same limitations on JSON paths](https://dev.to/franckpachot/microsoft-documentdb-rum-instead-of-gin-but-same-limitations-on-json-paths-48kn)
9. [postgres-documentdb 16-0.106.0-ferretdb-2.5.0 Public Latest - GitHub](https://github.com/-/ferretdb/packages/container/package/postgres-documentdb)
10. [Service release notes - Azure DocumentDB - Microsoft Learn](https://learn.microsoft.com/en-us/azure/documentdb/release-notes)
11. [vCore-based Azure Cosmos DB for MongoDB vs MongoDB Atlas - Cazton](https://cazton.com/blogs/technical/vcore-based-azure-cosmos-db-for-mongodb-vs-mongodb-atlas)
12. [Performance Testing of PostgreSQL with JSON Data: 1 to 1000 Concurrency with Data up to 50 Million Rows | by Chalindu Kodikara | Medium](https://medium.com/@chalindu/comprehensive-performance-testing-of-postgresql-with-jsonb-data-1-to-1000-concurrency-with-data-up-636029d8e9d4)
13. [AWS joins the DocumentDB project to build interoperable, open source document database technology](https://aws.amazon.com/blogs/opensource/aws-joins-the-documentdb-project-to-build-interoperable-open-source-document-database-technology/)
14. [How to deploy FerretDB with CloudNativePG on Kubernetes](https://blog.ferretdb.io/run-ferretdb-postgres-documentdb-extension-cnpg-kubernetes/)
15. [Tanzu for Postgres - TechDocs - Broadcom Inc.](https://techdocs.broadcom.com/content/dam/broadcom/techdocs/us/en/pdf/vmware-tanzu/data-solutions/tanzu-for-postgres/16-11/tnz-postgres/tnz-postgres.pdf)
16. [Tanzu for Postgres - TechDocs - Broadcom Inc.](https://techdocs.broadcom.com/content/dam/broadcom/techdocs/us/en/pdf/vmware-tanzu/data-solutions/tanzu-for-postgres/17-5/tnz-postgres/tnz-postgres.pdf)
17. [Tanzu for Postgres - TechDocs - Broadcom Inc.](https://techdocs.broadcom.com/content/dam/broadcom/techdocs/us/en/pdf/vmware-tanzu/data-solutions/tanzu-for-postgres/17-7/tnz-postgres/tnz-postgres.pdf)
