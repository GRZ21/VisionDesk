//
// Created by GRZ on 2026/7/9.
//

#include "DatabaseManager.h"
#include <QFileDialog>
#include <QDir>
#include <QSqlQuery>
#include <QSqlError>
#include <QDebug>

// 外部访问入口
DatabaseManager& DatabaseManager::instance()
{
    static DatabaseManager manager;
    return manager;
}

// 初始化数据库
QString DatabaseManager::initDatabase() {
    // 获取当前文件夹的路径
    QString dirPath = QDir::currentPath();
    QStringList filters;    // 创建文件筛选器
    filters << "*.db3"<<"*.db"; // 添加文件筛选器中的文件格式
    bool noExistingDB = QDir(dirPath).entryList(filters).isEmpty(); // 检查是否存在匹配的文件
    if (!noExistingDB) {    // 如果存在匹配的文件
        dbName = QFileDialog::getOpenFileName(nullptr, "Open Database", "", "SQLite Database (*.db3 *.db)");
    }else {
        dbName = QDir(dirPath).absoluteFilePath("experiments.db");
    }
    return dbName;
}

// 打开数据库，并确保新数据库和已有的空数据库都包含实验表。
bool DatabaseManager::openDatabase() {
    m_lastError.clear();
    if (dbName.isEmpty()) {
        m_lastError = QStringLiteral("No database selected.");
        return false;
    }
    if (!db.isValid())
        db = QSqlDatabase::addDatabase("QSQLITE");
    if (db.databaseName() != dbName) {
        db.close();
        db.setDatabaseName(dbName);
    }
    if (!db.isOpen() && !db.open()) {
        m_lastError = db.lastError().text();
        qWarning() << "Failed to open database:" << dbName << m_lastError;
        return false;
    }

    QSqlQuery query(db);
    if (!query.exec(R"(
        CREATE TABLE IF NOT EXISTS experiments (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            name TEXT NOT NULL,
            csv_path TEXT NOT NULL UNIQUE,
            best_epoch INTEGER,
            precision_value REAL,
            recall_value REAL,
            best_map50 REAL,
            best_map5095 REAL,
            params REAL,
            GFLOPs REAL,
            created_time TEXT DEFAULT (datetime('now', 'localtime')),
            updated_time TEXT DEFAULT (datetime('now', 'localtime')),
            note TEXT
        )
    )")) {
        m_lastError = query.lastError().text();
        qWarning() << "Failed to create experiments table:" << dbName << m_lastError;
        db.close();
        return false;
    }
    qDebug() << "Experiment database ready:" << dbName;
    return true;
}

// 通过id进行删除
bool DatabaseManager::deleteExperiment(int id) {
    m_lastError.clear();
    QSqlQuery query(db);
    if (!query.prepare("DELETE FROM experiments WHERE id = :id")) {
        m_lastError = query.lastError().text();
        qWarning() << "Failed to prepare experiment deletion:" << m_lastError;
        return false;
    }
    query.bindValue(":id", id);
    if (!query.exec()) {
        m_lastError = query.lastError().text();
        qWarning() << "Failed to delete experiment:" << m_lastError;
        return false;
    }
    if (query.numRowsAffected() != 1) {
        m_lastError = QStringLiteral("Experiment record no longer exists.");
        return false;
    }
    return true;
}

// 插入实验数据
bool DatabaseManager::insertExperiment(const ExperimentRecord &record) {
    m_lastError.clear();
    QSqlQuery query(db);
    if (!query.prepare("INSERT INTO experiments "
                            "(name, csv_path, best_epoch, "
                            "precision_value, recall_value, best_map50, "
                            "best_map5095, params, GFLOPs, "
                            "created_time, updated_time, note) "
                            "VALUES (:name, :csv_path, :best_epoch,"
                            " :precision_value, :recall_value, :best_map50,"
                            " :best_map5095, :params, :GFLOPs, "
                            ":created_time, :updated_time, :note)")) {
        m_lastError = query.lastError().text();
        qWarning() << "Failed to prepare experiment insert:" << m_lastError;
        return false;
    }
    query.bindValue(":name", record.name);
    query.bindValue(":csv_path", record.csvPath);
    query.bindValue(":best_epoch", record.bestEpoch);
    query.bindValue(":precision_value", record.precision);
    query.bindValue(":recall_value", record.recall);
    query.bindValue(":best_map50", record.bestMap50);
    query.bindValue(":best_map5095", record.bestMap5095);
    query.bindValue(":params", record.params);
    query.bindValue(":GFLOPs", record.GFLOPs);
    query.bindValue(":created_time", record.createdTime);
    query.bindValue(":updated_time", record.updatedTime);
    query.bindValue(":note", record.note);
    if (!query.exec()) {
        m_lastError = query.lastError().text();
        qWarning() << "Failed to insert experiment:" << m_lastError;
        return false;
    }
    return true;
}

// 更新实验数据
bool DatabaseManager::updateExperiment(int id, const ExperimentRecord &record) {
    m_lastError.clear();
    QSqlQuery query(db);
    if (!query.prepare("UPDATE experiments "
                  "SET name = :name, params = :params, GFLOPs = :GFLOPs, note = :note "
                  "WHERE id = :id")) {
        m_lastError = query.lastError().text();
        qWarning() << "Failed to prepare experiment update:" << m_lastError;
        return false;
    }
    query.bindValue(":id", id);
    query.bindValue(":name", record.name);
    query.bindValue(":params", record.params);
    query.bindValue(":GFLOPs", record.GFLOPs);
    query.bindValue(":note", record.note);
    if (!query.exec()) {
        m_lastError = query.lastError().text();
        qWarning() << "Failed to update experiment:" << m_lastError;
        return false;
    }
    if (query.numRowsAffected() != 1) {
        m_lastError = QStringLiteral("Experiment record no longer exists.");
        return false;
    }
    return true;
}

// 获取实验数据总页数（未完成）
int DatabaseManager::experimentCount(int currentPage, int pageSize) {
    Q_UNUSED(currentPage);
    QSqlQuery query(db);
    if (!query.exec("SELECT COUNT(*) FROM experiments")) {
        qDebug() << "Failed to count experiments:" << query.lastError().text();
        return false;
    }

    int counts = 0;
    if (query.next())
        counts = query.value(0).toInt();

    int totalPages = (counts + pageSize - 1) / pageSize;
    return totalPages;
}

