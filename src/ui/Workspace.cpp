#include "Workspace.h"

#include "Settings.h"
#include "core/DateTime.h"

#include <QFileInfo>
#include <QJsonDocument>
#include <QLoggingCategory>

namespace lh::ui {

namespace {

Q_LOGGING_CATEGORY(log, "loghunter.workspace")

AlertParameters readParameters()
{
    const QByteArray json = settings::read(settings::key::DETECTION_PARAMETERS).toByteArray();
    const QJsonDocument document = QJsonDocument::fromJson(json);
    return document.isObject() ? AlertParameters::fromJson(document.object()) : AlertParameters();
}

} // namespace

Workspace::Workspace(QObject* parent) : QObject(parent), m_parameters(readParameters())
{
    newSession();
}

Workspace::~Workspace()
{
    m_executor.setDatabase({});
    m_session.reset();
}

void Workspace::replaceSession(std::unique_ptr<Session> session)
{
    invalidateDetections();
    m_executor.setDatabase(session->workPath());
    m_session = std::move(session);
    m_filters = Filters();
    refreshCaches();
    emit sessionChanged();
    emit filtersChanged();
    emit dataModified();
    emit documentStateChanged();
}

void Workspace::newSession()
{
    replaceSession(Session::create(defaultTimeZone()));
}

QString Workspace::documentName() const
{
    const QString path = m_session->projectPath();
    return path.isEmpty() ? qtTrId("workspace.untitled") : QFileInfo(path).completeBaseName();
}

bool Workspace::isModified() const
{
    return m_session->isModified() && (!isEmpty() || !m_session->projectPath().isEmpty());
}

void Workspace::markSaved(const QString& path)
{
    m_session->setProjectPath(path);
    m_session->markModified(false);
    emit documentStateChanged();
}

QByteArray Workspace::defaultTimeZone()
{
    const QByteArray timeZone = settings::read(settings::key::TIME_ZONE).toByteArray();
    return isValidTimeZone(timeZone) ? timeZone : QByteArray();
}

void Workspace::setDefaultTimeZone(const QByteArray& timeZone)
{
    settings::write(settings::key::TIME_ZONE, timeZone);
}

void Workspace::notifyDataModified(bool modifiesDocument)
{
    if (modifiesDocument)
        m_session->markModified(true);
    m_executor.clearCache();
    invalidateDetections();
    refreshCaches();
    emit dataModified();
    emit documentStateChanged();
}

void Workspace::refreshCaches()
{
    sql::Connection& connection = m_session->mainConnection();
    try {
        m_eventCount = db::eventCount(connection);
        m_sources = db::sources(connection);
        m_firstDate = QDate::fromString(
            connection.text(QStringLiteral("SELECT lh_timestamp(MIN(ts_utc)) FROM events")).left(10), Qt::ISODate);
        m_lastDate = QDate::fromString(
            connection.text(QStringLiteral("SELECT lh_timestamp(MAX(ts_utc)) FROM events")).left(10), Qt::ISODate);
        m_timeZone = m_session->timeZone();
    } catch (const std::exception& e) {
        qCWarning(log) << "Cannot read the database state:" << e.what();
    }
}

void Workspace::setFilters(const Filters& filters)
{
    if (filters == m_filters)
        return;
    m_filters = filters;
    invalidateDetections();
    emit filtersChanged();
}

void Workspace::modifyFilters(const std::function<void(Filters&)>& modification)
{
    Filters filters = m_filters;
    modification(filters);
    setFilters(filters);
}

void Workspace::resetFilters()
{
    setFilters(Filters());
}

void Workspace::explore(const Filters& filters)
{
    setFilters(filters);
    navigate({Navigation::Target::Events, {}});
}

void Workspace::setParameters(const AlertParameters& parameters)
{
    if (parameters == m_parameters)
        return;
    m_parameters = parameters;
    settings::write(settings::key::DETECTION_PARAMETERS,
                    QJsonDocument(parameters.toJson()).toJson(QJsonDocument::Compact));
    invalidateDetections();
    emit parametersChanged();
}

void Workspace::setBusy(bool busy)
{
    if (busy == m_busy)
        return;
    m_busy = busy;
    emit busyChanged(busy);
}

void Workspace::suspendAccess()
{
    m_executor.setDatabase({});
    m_session->closeConnection();
}

void Workspace::resumeAccess(bool dataModified)
{
    try {
        m_session->reopenConnection();
    } catch (const std::exception& e) {
        qCWarning(log) << "Cannot reopen the database:" << e.what();
    }
    m_executor.setDatabase(m_session->workPath());
    notifyDataModified(dataModified);
}

void Workspace::invalidateDetections()
{
    cancel(m_detectionsToken);
    m_detectionsValid = false;
    m_detections.clear();
    m_pending.clear();
    emit detectionsInvalidated();
}

void Workspace::detections(QObject* context, std::function<void(const QList<Detection>&)> callback)
{
    if (m_detectionsValid) {
        callback(m_detections);
        return;
    }
    m_pending.append({QPointer<QObject>(context), std::move(callback)});
    if (m_detectionsToken)
        return;
    auto finish = [this](QList<Detection> result) {
        m_detectionsToken.reset();
        m_detections = std::move(result);
        m_detectionsValid = true;
        emit detectionsAvailable(m_detections);
        const QList<Pending> pending = std::exchange(m_pending, {});
        for (const Pending& wait : pending)
            if (wait.context)
                wait.callback(m_detections);
    };
    const QString key = QStringLiteral("detections|%1|%2")
                            .arg(m_filters.key(),
                                 QString::fromUtf8(QJsonDocument(m_parameters.toJson()).toJson(QJsonDocument::Compact)));
    m_detectionsToken = m_executor.runCached(
        this, key,
        [filters = m_filters, parameters = m_parameters](Context& context) {
            return detect(context, filters, parameters);
        },
        finish,
        [this](const QString& message) {
            m_detectionsToken.reset();
            emit m_executor.error(qtTrId("workspace.detection_failed").arg(message));
            const QList<Pending> pending = std::exchange(m_pending, {});
            for (const Pending& wait : pending)
                if (wait.context)
                    wait.callback({});
        });
}

} // namespace lh::ui
