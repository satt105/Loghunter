// Background execution of queries.
#pragma once

#include "core/Scan.h"
#include "core/Sqlite.h"

#include <QHash>
#include <QMetaObject>
#include <QMutex>
#include <QObject>
#include <QPointer>
#include <QThreadPool>
#include <QtConcurrentRun>

#include <any>
#include <atomic>
#include <functional>
#include <memory>
#include <optional>
#include <type_traits>
#include <vector>

namespace lh::ui {

class Token
{
public:
    void cancel();
    [[nodiscard]] bool cancelled() const noexcept { return m_cancelled.load(std::memory_order_relaxed); }
    [[nodiscard]] const std::atomic_bool& flag() const noexcept { return m_cancelled; }

private:
    friend class Executor;
    std::atomic_bool m_cancelled{false};
    QMutex m_lock;
    sql::Connection* m_connection = nullptr;
};
using TokenPtr = std::shared_ptr<Token>;

inline void cancel(TokenPtr& token)
{
    if (token)
        token->cancel();
    token.reset();
}

class Executor : public QObject
{
    Q_OBJECT

public:
    explicit Executor(QObject* parent = nullptr);
    ~Executor() override;

    void setDatabase(const QString& path);
    [[nodiscard]] Caches& caches() noexcept { return m_caches; }

    template <typename Work, typename Done>
    TokenPtr run(QObject* receiver, Work work, Done done, std::function<void(const QString&)> failure = {});

    template <typename Work, typename Done>
    TokenPtr runCached(QObject* receiver, const QString& key, Work work, Done done,
                       std::function<void(const QString&)> failure = {});

    void clearCache();

signals:
    void error(const QString& message);

private:
    struct Lease
    {
        std::unique_ptr<sql::Connection> connection;
        quint64 generation = 0;
    };
    Lease borrow();
    void release(Lease lease);
    struct CacheEntry
    {
        std::any value;
        qint64 cost = 1;
        quint64 usage = 0;
    };
    static constexpr qint64 CACHE_BUDGET = 400;

    void track(const TokenPtr& token);
    static void attach(Token& token, sql::Connection* connection);
    void remember(const QString& key, std::any value, qint64 cost);

    QThreadPool m_pool;
    QMutex m_lock;
    QString m_path;
    quint64 m_generation = 0;
    std::vector<std::unique_ptr<sql::Connection>> m_idle;
    std::vector<std::weak_ptr<Token>> m_tokens;
    Caches m_caches;
    QHash<QString, CacheEntry> m_cache;
    qint64 m_cacheCost = 0;
    quint64 m_clock = 0;
    quint64 m_cacheGeneration = 0;
};

// ── Template implementation ──────────────────────────────────────────────────

template <typename Work, typename Done>
TokenPtr Executor::run(QObject* receiver, Work work, Done done, std::function<void(const QString&)> failure)
{
    using Result = std::invoke_result_t<Work&, Context&>;
    static_assert(!std::is_void_v<Result>, "The task must return a result.");

    auto token = std::make_shared<Token>();
    track(token);
    QPointer<QObject> guard(receiver);
    const quint64 generation = m_generation;

    (void)QtConcurrent::run(&m_pool, [this, token, guard, generation, work = std::move(work),
                                      done = std::move(done), failure = std::move(failure)]() mutable {
        if (token->cancelled())
            return;
        std::optional<Result> result;
        QString message;
        try {
            Lease lease = borrow();
            attach(*token, lease.connection.get());
            try {
                Context context{*lease.connection, m_caches, &token->flag()};
                result.emplace(work(context));
            } catch (...) {
                attach(*token, nullptr);
                release(std::move(lease));
                throw;
            }
            attach(*token, nullptr);
            release(std::move(lease));
        } catch (const sql::Error& e) {
            if (!e.isInterruption())
                message = e.message();
        } catch (const std::exception& e) {
            message = QString::fromUtf8(e.what());
        }
        if (token->cancelled() || (!result && message.isEmpty()))
            return;
        QMetaObject::invokeMethod(
            this,
            [this, token, guard, generation, done = std::move(done), failure = std::move(failure),
             result = std::move(result), message = std::move(message)]() mutable {
                if (token->cancelled() || !guard || generation != m_generation)
                    return;
                if (result) {
                    done(std::move(*result));
                } else if (failure) {
                    failure(message);
                } else {
                    emit error(message);
                }
            },
            Qt::QueuedConnection);
    });
    return token;
}

template <typename T>
qint64 cacheCost(const T&)
{
    return 1;
}

inline qint64 cacheCost(const std::shared_ptr<const std::vector<qint64>>& identifiers)
{
    return 1 + static_cast<qint64>(identifiers ? identifiers->size() / 50'000 : 0);
}

template <typename Work, typename Done>
TokenPtr Executor::runCached(QObject* receiver, const QString& key, Work work, Done done,
                             std::function<void(const QString&)> failure)
{
    using Result = std::invoke_result_t<Work&, Context&>;
    using Shared = std::shared_ptr<const Result>;
    if (const auto found = m_cache.find(key); found != m_cache.end()) {
        found->usage = ++m_clock;
        auto token = std::make_shared<Token>();
        QPointer<QObject> guard(receiver);
        QMetaObject::invokeMethod(
            this,
            [token, guard, done = std::move(done), shared = std::any_cast<Shared>(found->value)]() mutable {
                if (!token->cancelled() && guard)
                    done(*shared);
            },
            Qt::QueuedConnection);
        return token;
    }
    const quint64 generation = m_cacheGeneration;
    return run(
        receiver, std::move(work),
        [this, key, generation, done = std::move(done)](Result result) mutable {
            if (generation == m_cacheGeneration)
                remember(key, std::any(std::make_shared<const Result>(result)), cacheCost(result));
            done(std::move(result));
        },
        std::move(failure));
}

} // namespace lh::ui
