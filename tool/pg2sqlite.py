#!/usr/bin/env python3
"""
pg2sqlite.py — PostgreSQL → SQLite3 数据迁移工具

从 PostgreSQL 读取表结构和指定数据, 初始化到 SQLite3 数据库

迁移策略:
  - stkprice        K线数据表      → 只建表结构 (数据由 market_server 实时写入)
  - orders          订单表          → 只建表结构 (不迁移历史委托)
  - sno             序号发生器      → 建表 + 从 PG 同步当前序号
  - executions      执行记录表      → 只建表结构
  - parameter_table 交易参数表      → 建表 + 迁移全部数据

用法:
  python pg2sqlite.py                              # 使用默认配置
  python pg2sqlite.py --pg-conn "host=..." --sqlite-path ./data/kline.db
  python pg2sqlite.py --config ../config.ini       # 从 config.ini 读取连接信息

依赖:
  pip install psycopg2-binary   (或 psycopg2)
"""

import os
import sys
import sqlite3
import argparse
import configparser

try:
    import psycopg2
except ImportError:
    print("错误: 需要安装 psycopg2")
    print("  pip install psycopg2-binary")
    sys.exit(1)

import decimal
import datetime
import json


# ============================================================================
# SQLite3 建表语句 (与 database.h EnsureTables() 完全一致)
# ============================================================================

SQLITE_SCHEMA = {
    "stkprice": """
        CREATE TABLE IF NOT EXISTS stkprice (
            timestamp    BIGINT,
            timeframe    VARCHAR(10),
            currency_pair VARCHAR(20),
            currency     VARCHAR(20) DEFAULT '',
            market       CHAR(1) DEFAULT '',
            openprice    DECIMAL,
            highprice    DECIMAL,
            lowprice     DECIMAL,
            closeprice   DECIMAL,
            matchqty     DECIMAL,
            matchamt     DECIMAL,
            change_per   DECIMAL,
            PRIMARY KEY (timestamp, timeframe, currency_pair)
        )
    """,

    "orders": """
        CREATE TABLE IF NOT EXISTS orders (
            id                 INTEGER PRIMARY KEY,
            timestamp          BIGINT,
            currency_pair      VARCHAR(20),
            price              DECIMAL,
            quantity           DECIMAL,
            amount             DECIMAL,
            type               VARCHAR(8),
            order_type         INTEGER,
            take_profit_price  DECIMAL,
            stop_loss_price    DECIMAL,
            status             VARCHAR(16),
            matchstatus        INTEGER,
            matchtimestamp     BIGINT,
            profit             DECIMAL,
            remark             TEXT,
            old_orderid        INTEGER
        )
    """,

    "sno": """
        CREATE TABLE IF NOT EXISTS sno (
            name VARCHAR(64) PRIMARY KEY,
            num  BIGINT NOT NULL
        )
    """,

    "executions": """
        CREATE TABLE IF NOT EXISTS executions (
            currency_pair    VARCHAR(20) PRIMARY KEY,
            timestamp_records BIGINT
        )
    """,
    "parameter_table1": """
        drop table parameter_table
          
    """,
    "parameter_table": """
       CREATE TABLE  IF NOT EXISTS  parameter_table (
    id INT ,  -- 主键，自增
    currency_pair VARCHAR(20) NOT NULL,  -- 币种对，例如 BTC/USD, ETH/USD 等
    coefficient DECIMAL(18, 8) NOT NULL,  -- 系数，精确到小数点后8位
    up_signal_count INT DEFAULT 0,  -- 上涨信号累计，默认值为0
    down_signal_count INT DEFAULT 0,  -- 下跌信号累计，默认值为0
    oscillation_signal_count INT DEFAULT 0,  -- 震荡信号累计，默认值为0
    take_profit_ratio DECIMAL(18, 8),  -- 止盈比例，精确到小数点后8位
    stop_loss_ratio  DECIMAL(18, 8),  -- 止损比例，精确到小数点后8位
    pup_signal_count INT DEFAULT 0,  -- 平仓上涨信号累计，默认值为0
    pdown_signal_count INT DEFAULT 0,  -- 平仓下跌信号累计，默认值为0
    poscillation_signal_count INT DEFAULT 0,  -- 平仓震荡信号累计，默认值为0
    pzy_signal_count INT DEFAULT 0,  -- 平仓止盈信号累计，默认值为0
    czzy_signal_count DECIMAL(18, 8),  -- 平仓止盈差值信号，用于记录回测最大值用于平仓
    sxzy_signal_count INT DEFAULT 0  -- 平仓止盈信号启动差值的上线
        )
    """,
}

# 需要从 PG 迁移数据的表
TABLES_WITH_DATA = {"parameter_table"}


def parse_config_ini(config_path: str):
    """从 config.ini 解析数据库连接信息"""
    cfg = configparser.ConfigParser()
    cfg.read(config_path, encoding="utf-8")
    pg_conn = cfg.get("database", "db_conn",
                       fallback="host=localhost port=5432 dbname=hhjrdata user=hhjr password=hhjr")
    sqlite_path = cfg.get("database", "db_path", fallback="./bin/data.db")
    return pg_conn, sqlite_path


def connect_pg(conn_str: str):
    """连接 PostgreSQL"""
    print(f"[PG] 连接: {conn_str}")
    conn = psycopg2.connect(conn_str)
    conn.set_client_encoding("UTF8")
    print("[PG] 连接成功")
    return conn


def create_sqlite(db_path: str):
    """创建/打开 SQLite3 数据库"""
    # 确保目录存在
    db_dir = os.path.dirname(db_path)
    if db_dir and not os.path.exists(db_dir):
        os.makedirs(db_dir, exist_ok=True)
        print(f"[SQLite] 创建目录: {db_dir}")

    conn = sqlite3.connect(db_path)
    conn.execute("PRAGMA journal_mode=WAL")
    conn.execute("PRAGMA synchronous=NORMAL")
    print(f"[SQLite] 打开: {db_path}")
    return conn


def check_pg_table_exists(pg_conn, table_name: str) -> bool:
    """检查 PG 表是否存在"""
    cur = pg_conn.cursor()
    cur.execute(
        "SELECT EXISTS (SELECT 1 FROM information_schema.tables "
        "WHERE table_name = %s)", (table_name,)
    )
    exists = cur.fetchone()[0]
    cur.close()
    return exists


def get_pg_row_count(pg_conn, table_name: str) -> int:
    """获取 PG 表行数"""
    cur = pg_conn.cursor()
    cur.execute(f"SELECT COUNT(*) FROM {table_name}")
    count = cur.fetchone()[0]
    cur.close()
    return count


def migrate_table_data(pg_conn, sqlite_conn, table_name: str):
    """从 PG 读取数据并写入 SQLite"""
    pg_cur = pg_conn.cursor()
    pg_cur.execute(f"SELECT id,currency_pair,coefficient,up_signal_count,down_signal_count,oscillation_signal_count \
,take_profit_ratio,stop_loss_ratio,pup_signal_count,pdown_signal_count,poscillation_signal_count,\
pzy_signal_count,czzy_signal_count,sxzy_signal_count FROM {table_name}")

    if pg_cur.description is None:
        print(f"  [SKIP] {table_name}: 无法读取")
        pg_cur.close()
        return 0

    col_names = [desc[0] for desc in pg_cur.description]
    rows = pg_cur.fetchall()
    pg_cur.close()

    if not rows:
        print(f"  [INFO] {table_name}: PG 中无数据")
        return 0

    # ---------- 规范化 PG 返回的值为 SQLite 可绑定类型 ----------
    def _normalize_value(v):
        # None 保持 None
        if v is None:
            return None
        # Decimal -> int (if integral) 或 float
        if isinstance(v, decimal.Decimal):
            try:
                if v == v.to_integral():
                    return int(v)
            except Exception:
                pass
            try:
                return float(v)
            except Exception:
                return str(v)
        # memoryview/bytearray -> bytes
        if isinstance(v, (memoryview, bytearray)):
            return bytes(v)
        # dict/list/json -> JSON 字符串
        if isinstance(v, (dict, list)):
            try:
                return json.dumps(v, ensure_ascii=False)
            except Exception:
                return str(v)
        # datetime -> 毫秒时间戳
        if isinstance(v, datetime.datetime):
            try:
                return int(v.timestamp() * 1000)
            except Exception:
                return v.isoformat()
        if isinstance(v, datetime.date):
            return v.isoformat()
        # 其他基本类型 (int/float/str/bytes/bool) 直接返回
        return v

    normalized_rows = []
    for ridx, row in enumerate(rows):
        try:
            nr = tuple(_normalize_value(x) for x in row)
            normalized_rows.append(nr)
        except Exception as e:
            print(f"  [ERROR] 规范化第 {ridx} 行失败: {e}")
            print(f"    原始行: {row}")
            raise

    placeholders = ",".join(["?"] * len(col_names))
    cols = "id,currency_pair,coefficient,up_signal_count,down_signal_count,oscillation_signal_count\
,take_profit_ratio,stop_loss_ratio,pup_signal_count,pdown_signal_count,poscillation_signal_count,\
pzy_signal_count,czzy_signal_count,sxzy_signal_count"
    insert_sql = f"INSERT OR REPLACE INTO {table_name} ({cols}) VALUES ({placeholders})"

    sqlite_cur = sqlite_conn.cursor()
    try:
        sqlite_cur.executemany(insert_sql, normalized_rows)
    except sqlite3.InterfaceError as e:
        # 打印导致绑定失败的行信息以便排查
        print(f"  [ERROR] executemany 绑定参数失败: {e}")
        for i, nr in enumerate(normalized_rows):
            try:
                # 尝试单行插入以定位具体哪一行失败
                sqlite_cur.execute(insert_sql, nr)
            except Exception as e2:
                print(f"    失败行索引: {i}")
                print(f"    值: {nr}")
                print(f"    值类型: {[type(x) for x in nr]}")
                raise
        raise
    sqlite_conn.commit()

    print(f"  [OK] {table_name}: 迁移 {len(rows)} 行")
    return len(rows)


def main():
    parser = argparse.ArgumentParser(
        description="PostgreSQL → SQLite3 数据迁移工具")
    parser.add_argument("--config", default=None,
                        help="config.ini 路径 (自动读取数据库配置)")
    parser.add_argument("--pg-conn",
                        default="host=localhost port=5432 dbname=hhjrdata user=hhjr password=hhjr",
                        help="PostgreSQL 连接字符串")
    parser.add_argument("--sqlite-path", default="./data/kline.db",
                        help="SQLite3 数据库文件路径")
    parser.add_argument("--force", action="store_true",
                        help="如果 SQLite 文件已存在, 删除后重建")
    args = parser.parse_args()

    # 从 config.ini 读取配置 (如果指定)
    if args.config:
        pg_conn_str, sqlite_path = parse_config_ini(args.config)
    else:
        pg_conn_str = args.pg_conn
        sqlite_path = args.sqlite_path

    print("=" * 60)
    print("PPQ PostgreSQL → SQLite3 迁移工具")
    print("=" * 60)
    print(f"  PG 连接:    {pg_conn_str}")
    print(f"  SQLite 路径: {sqlite_path}")
    print()

    # 处理 --force
    if args.force and os.path.exists(sqlite_path):
        os.remove(sqlite_path)
        print(f"[SQLite] 已删除旧文件: {sqlite_path}")

    # 连接数据库
    pg_conn = connect_pg(pg_conn_str)
    sqlite_conn = create_sqlite(sqlite_path)

    # 建表
    print()
    print("[Step 1] 创建 SQLite 表结构")
    print("-" * 40)
    for table_name, ddl in SQLITE_SCHEMA.items():
        sqlite_conn.execute(ddl)
        print(f"  [OK] {table_name}")
    sqlite_conn.commit()

    # 初始化 sno 默认值 (如果 PG 中没有 sno 表, 用默认值)
    sqlite_conn.execute(
        "INSERT OR IGNORE INTO sno (name, num) VALUES ('orderid', 0)")
    sqlite_conn.commit()

    # 迁移数据
    print()
    print("[Step 2] 从 PostgreSQL 迁移数据")
    print("-" * 40)

    total_rows = 0
    for table_name in SQLITE_SCHEMA:
        if table_name not in TABLES_WITH_DATA:
            print(f"  [SKIP] {table_name}: 只建表结构, 不迁移数据")
            continue

        if not check_pg_table_exists(pg_conn, table_name):
            print(f"  [WARN] {table_name}: PG 中不存在, 跳过")
            continue

        pg_count = get_pg_row_count(pg_conn, table_name)
        print(f"  [PG] {table_name}: {pg_count} 行")
        total_rows += migrate_table_data(pg_conn, sqlite_conn, table_name)

    # 汇总
    print()
    print("[Step 3] 验证结果")
    print("-" * 40)
    sqlite_cur = sqlite_conn.cursor()
    for table_name in SQLITE_SCHEMA:
        sqlite_cur.execute(f"SELECT COUNT(*) FROM {table_name}")
        count = sqlite_cur.fetchone()[0]
        print(f"  {table_name:20s} : {count} 行")

    # 显示 parameter_table 内容
    sqlite_cur.execute("""SELECT id,currency_pair,coefficient,up_signal_count,down_signal_count,oscillation_signal_count
,take_profit_ratio,stop_loss_ratio,pup_signal_count,pdown_signal_count,poscillation_signal_count,
pzy_signal_count,czzy_signal_count,sxzy_signal_count FROM parameter_table""")
    param_rows = sqlite_cur.fetchall()
    if param_rows:
        print()
        print("  parameter_table 数据:")
        col_names = [desc[0] for desc in sqlite_cur.description]
        print(f"    {col_names}")
        for row in param_rows:
            print(f"    {list(row)}")

    # 显示 sno 内容
    sqlite_cur.execute("SELECT * FROM sno")
    sno_rows = sqlite_cur.fetchall()
    if sno_rows:
        print()
        print("  sno 数据:")
        for row in sno_rows:
            print(f"    {row[0]} = {row[1]}")

    # 关闭连接
    pg_conn.close()
    sqlite_conn.close()

    print()
    print("=" * 60)
    print(f"迁移完成! SQLite 文件: {sqlite_path}")
    print(f"共迁移 {total_rows} 行数据")
    print("=" * 60)


if __name__ == "__main__":
    main()
