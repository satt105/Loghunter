#include "Executor.h"

#include "core/Session.h"

#include <QThread>

#include <algorithm>

namespace lh::ui {

void Token::cancel()
{
    m_cancelled.store(true, std::memory_order_relaxed);
    QMutexLocker lock(&m_lock);
    if (m_connection)
        m_connection->interrupt();
}

Executor::Executor(QObject* parent) : QObject(parent)
{
    m_pool.setMaxThreadCount(std::clamp(QThread::idealThreadCount() / 2, 2, 4));
    m_pool.setExpiryTimeout(60'000);
}

Executor::~Executor()
{
    setDatabase({});
}

void Executor::setDatabase(const QString& path)
{
    {
        QMutexLocker lock(&m_lock);
        for (const auto& low : m_tokens)
            if (const TokenPtr token = low.lock())
                token->cancel();
        m_tokens.clear();
    }
    m_pool.waitForDone();
    clearCache();
    QMutexLocker lock(&m_lock);
    m_idle.clear();
    m_path = path;
    ++m_generation;
}

void Executor::clearCache()
{
    m_cache.clear();
    m_cacheCost = 0;
    ++m_cacheGeneration;
    m_caches.invalidate();
}

void Executor::remember(const QString& key, std::any value, qint64 cost)
{
    cost = std::max<qint64>(1, cost);
    if (cost > CACHE_BUDGET)
        return;
    if (const auto existing = m_cache.constFind(key); existing != m_cache.cend())
        m_cacheCost -= existing->cost;
    m_cache.insert(key, {std::move(value), cost, ++m_clock});
    m_cacheCost += cost;
    while (m_cacheCost > CACHE_BUDGET && !m_cache.isEmpty()) {
        auto oldest = m_cache.begin();
        for (auto it = m_cache.begin(); it != m_cache.end(); ++it)
            if (it->usage < oldest->usage)
                oldest = it;
        m_cacheCost -= oldest->cost;
        m_cache.erase(oldest);
    }
}

void Executor::track(const TokenPtr& token)
{
    QMutexLocker lock(&m_lock);
    std::erase_if(m_tokens, [](const std::weak_ptr<Token>& low) { return low.expired(); });
    m_tokens.push_back(token);
}

void Executor::attach(Token& token, sql::Connection* connection)
{
    QMutexLocker lock(&token.m_lock);
    token.m_connection = connection;
    if (connection && token.cancelled())
        connection->interrupt();
}

Executor::Lease Executor::borrow()
{
    QString path;
    quint64 generation = 0;
    {
        QMutexLocker lock(&m_lock);
        if (m_path.isEmpty())
            throw sql::Error::interruption();
        generation = m_generation;
        if (!m_idle.empty()) {
            Lease lease{std::move(m_idle.back()), generation};
            m_idle.pop_back();
            return lease;
        }
        path = m_path;
    }
    return {std::make_unique<sql::Connection>(db::open(path)), generation};
}

void Executor::release(Lease lease)
{
    QMutexLocker lock(&m_lock);
    if (lease.generation == m_generation && lease.connection)
        m_idle.push_back(std::move(lease.connection));
}

} // namespace lh::ui
