#pragma once
// database_engine.h — 数据库引擎工厂
// 根据配置文件选择 SQLite3 或 PostgreSQL 后端
// 统一接口: IDatabase*
//
// 用法:
//   auto db = DatabaseEngine::Create(config);
//   db->Open(conn_str);
//   db->InsertBar(...);

#include <string>
#include <memory>
#include "database_interface.h"
#include "database.h"
#include "database_pg.h"
#include "config_parser.h"
#include "logger.h"

class DatabaseEngine {
public:
    // 根据配置创建数据库实例
    // config 中 [database] 段:
    //   db_type = sqlite3 | postgresql
    //   db_path = ./data/kline.db          (SQLite3)
    //   db_conn = host=... port=... ...    (PostgreSQL)
    static std::unique_ptr<IDatabase> Create(const ConfigParser& config) {
        std::string db_type = config.Get("database", "db_type", "sqlite3");

        if (db_type == "postgresql" || db_type == "postgres" || db_type == "pg") {
            LOG_I("[DatabaseEngine] Using PostgreSQL backend");
            auto db = std::make_unique<PostgresDatabase>();
            std::string conn_str = config.Get("database", "db_conn",
                "host=localhost port=5432 dbname=hhjrdata user=hhjr password=hhjr");
            if (!db->Open(conn_str)) {
                LOG_E("[DatabaseEngine] PostgreSQL connection failed");
                return nullptr;
            }
            return db;
        } else {
            LOG_I("[DatabaseEngine] Using SQLite3 backend");
            auto db = std::make_unique<SQLiteDatabase>();
            std::string db_path = config.Get("database", "db_path", "./data/kline.db");
            if (!db->Open(db_path)) {
                LOG_E("[DatabaseEngine] SQLite3 open failed: %s", db_path.c_str());
                return nullptr;
            }
            return db;
        }
    }

    // 简化版: 直接指定类型和连接串
    static std::unique_ptr<IDatabase> Create(const std::string& db_type,
                                              const std::string& conn_str) {
        if (db_type == "postgresql" || db_type == "postgres" || db_type == "pg") {
            auto db = std::make_unique<PostgresDatabase>();
            if (!db->Open(conn_str)) return nullptr;
            return db;
        } else {
            auto db = std::make_unique<SQLiteDatabase>();
            if (!db->Open(conn_str)) return nullptr;
            return db;
        }
    }
};