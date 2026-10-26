<h1 align="center">aiodrivers</h1>

<p align="center">
  Database drivers for async Python.
</p>

<p align="center">
  <img alt="Python: async" src="https://img.shields.io/badge/python-async-3776AB?style=for-the-badge&amp;logo=python&amp;logoColor=white">
  <img alt="Tests" src="https://img.shields.io/github/actions/workflow/status/cqlsh/aiodrivers/tests.yml?style=for-the-badge&amp;label=tests">
  <img alt="Status: early development" src="https://img.shields.io/badge/status-early%20development-F59E0B?style=for-the-badge">
  <img alt="License: MIT" src="https://img.shields.io/badge/license-MIT-22C55E?style=for-the-badge">
</p>

Every driver is written from scratch and speaks its database's wire protocol
directly.

```python
import asyncio

from aiodrivers.adapters import SQLite

async def main():
    database = SQLite('app.db')

    async with await database.connect() as connection:
        await connection.execute('CREATE TABLE IF NOT EXISTS users (name TEXT)')
        await connection.execute('INSERT INTO users VALUES (?)', 'ada')

        print(await connection.fetchval('SELECT count(*) FROM users'))

asyncio.run(main())
```

The SQLite driver works and needs Python 3.12 or newer. There is no release on
PyPI yet, and the API will still change.

## Roadmap

SQL databases come first, then NoSQL. SQLite is done, PostgreSQL is next, and
everything after that is planned. Many databases speak another database's wire
protocol, so one driver covers several of them.

### SQL

| #   | Database             | Protocol                      | Also covers                                                                                                                       |
| --- | -------------------- | ----------------------------- | --------------------------------------------------------------------------------------------------------------------------------- |
| 1   | SQLite               | none, uses stdlib `sqlite3`   |                                                                                                                                   |
| 2   | PostgreSQL           | frontend/backend protocol 3.0 | CockroachDB, YugabyteDB, TimescaleDB, Amazon Redshift, Amazon Aurora PostgreSQL, Google AlloyDB, Greenplum, QuestDB, Materialize |
| 3   | MySQL                | client/server protocol        | MariaDB, Percona Server, Amazon Aurora MySQL, TiDB, SingleStore, Vitess, Apache Doris, StarRocks, OceanBase                      |
| 4   | Microsoft SQL Server | TDS                           | Azure SQL Database, Azure Synapse Analytics                                                                                       |
| 5   | Oracle Database      | Oracle Net (TNS)              |                                                                                                                                   |
| 6   | ClickHouse           | native TCP protocol           |                                                                                                                                   |
| 7   | IBM Db2              | DRDA                          | Informix, Apache Derby                                                                                                            |
| 8   | SAP HANA             | SQL Command Network Protocol  |                                                                                                                                   |
| 9   | SAP ASE (Sybase)     | TDS 5.0                       |                                                                                                                                   |
| 10  | Firebird             | Firebird wire protocol        |                                                                                                                                   |
| 11  | Vertica              | Vertica wire protocol         |                                                                                                                                   |
| 12  | Teradata             | proprietary                   |                                                                                                                                   |
| 13  | Snowflake            | HTTPS API                     |                                                                                                                                   |
| 14  | Google BigQuery      | HTTPS API                     |                                                                                                                                   |
| 15  | Google Cloud Spanner | gRPC                          |                                                                                                                                   |
| 16  | Trino                | HTTP API                      | Presto                                                                                                                            |
| 17  | Apache Hive          | HiveServer2 (Thrift)          | Spark SQL, Apache Impala, Databricks SQL                                                                                          |
| 18  | Exasol               | WebSocket API                 |                                                                                                                                   |
| 19  | Apache Druid         | HTTP API                      |                                                                                                                                   |

### NoSQL

| #   | Database           | Protocol                                    | Also covers                                                                         |
| --- | ------------------ | ------------------------------------------- | ----------------------------------------------------------------------------------- |
| 20  | Redis              | RESP2 and RESP3                             | Valkey, KeyDB, DragonflyDB, Garnet, Amazon ElastiCache, Amazon MemoryDB             |
| 21  | MongoDB            | wire protocol (OP_MSG)                      | Amazon DocumentDB, Azure Cosmos DB for MongoDB, FerretDB                            |
| 22  | Apache Cassandra   | CQL binary protocol                         | ScyllaDB, DataStax Astra DB, Amazon Keyspaces, Azure Cosmos DB for Apache Cassandra |
| 23  | Memcached          | text and meta protocol                      |                                                                                     |
| 24  | Elasticsearch      | HTTP API                                    | OpenSearch                                                                          |
| 25  | Neo4j              | Bolt                                        | Memgraph, Amazon Neptune                                                            |
| 26  | Amazon DynamoDB    | HTTPS API                                   | ScyllaDB Alternator                                                                 |
| 27  | Couchbase          | memcached binary protocol, HTTP for queries |                                                                                     |
| 28  | Apache CouchDB     | HTTP API                                    | IBM Cloudant                                                                        |
| 29  | InfluxDB           | HTTP API                                    |                                                                                     |
| 30  | Apache HBase       | HBase RPC (Protocol Buffers)                |                                                                                     |
| 31  | etcd               | gRPC                                        |                                                                                     |
| 32  | Apache ZooKeeper   | ZooKeeper wire protocol                     |                                                                                     |
| 33  | Aerospike          | Aerospike wire protocol                     |                                                                                     |
| 34  | Azure Cosmos DB    | HTTPS API                                   |                                                                                     |
| 35  | Google Firestore   | gRPC                                        |                                                                                     |
| 36  | Google Bigtable    | gRPC                                        |                                                                                     |
| 37  | Apache Solr        | HTTP API                                    |                                                                                     |
| 38  | ArangoDB           | HTTP API                                    |                                                                                     |
| 39  | Qdrant             | HTTP API                                    |                                                                                     |
| 40  | Milvus             | gRPC                                        |                                                                                     |
| 41  | Weaviate           | HTTP API                                    |                                                                                     |
| 42  | Pinecone           | HTTPS API                                   |                                                                                     |
| 43  | Chroma             | HTTP API                                    |                                                                                     |
| 44  | Tarantool          | IPROTO                                      |                                                                                     |
| 45  | Hazelcast          | Open Binary Client Protocol                 |                                                                                     |
| 46  | Apache Ignite      | thin client protocol                        |                                                                                     |
| 47  | SurrealDB          | WebSocket RPC                               |                                                                                     |

Embedded engines other than SQLite, such as DuckDB, RocksDB and LMDB, are not
planned. They run inside the process and need their own native library, which
a package without dependencies cannot bring along.

## Design

- Every database gets an adapter under `aiodrivers.adapters`. Adapters load on
  first use, so importing one does not import the others.
- Each driver is written in pure Python. The CPU-heavy parts, mostly parsing
  replies and converting values, will also ship as optional C++ extensions.
- If an extension is missing or fails to build, the Python implementation runs
  instead.
- SQLite has no wire protocol, so its adapter runs `sqlite3` in a worker thread.

## Tests

```bash
python -m unittest discover -s tests -t .
```

The tests use the standard library's `unittest`, so there is nothing to
install.

## License

MIT. See [LICENSE](LICENSE).