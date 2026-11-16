// Shared analysis state: session, filters, detection parameters and navigation.
#pragma once

#include "Executor.h"
#include "core/Alerts.h"
#include "core/Filters.h"
#include "core/Session.h"

#include <QDate>
#include <QList>
#include <QObject>
#include <QPointer>

#include <functional>
#include <memory>

namespace lh::ui {

struct Navigation
{
    enum class Target { Home, Dashboard, Events, User, File, Folder, Alerts };
    Target target = Target::Home;
    QString value;
};

class Workspace : public QObject
{
    Q_OBJECT

public:
    explicit Workspace(QObject* parent = nullptr);
    ~Workspace() override;

    // ── Session ──────────────────────────────────────────────────────────────
    [[nodiscard]] Session& session() { return *m_session; }
    [[nodiscard]] Executor& executor() { return m_executor; }
    void replaceSession(std::unique_ptr<Session> session);
    void newSession();
    [[nodiscard]] QString documentName() const;
    [[nodiscard]] bool isModified() const;
    void markSaved(const QString& path);

    static QByteArray defaultTimeZone();
    static void setDefaultTimeZone(const QByteArray& timeZone);

    // ── Data (caches updated on every change) ────────────────────────────────
    [[nodiscard]] bool isEmpty() const { return m_eventCount == 0; }
    [[nodiscard]] qint64 eventCount() const { return m_eventCount; }
    [[nodiscard]] const QList<Source>& sources() const { return m_sources; }
    [[nodiscard]] QDate firstDate() const { return m_firstDate; }
    [[nodiscard]] QDate lastDate() const { return m_lastDate; }
    [[nodiscard]] const QByteArray& timeZone() const { return m_timeZone; }
    void notifyDataModified(bool modifiesDocument = true);

    // ── Filters ──────────────────────────────────────────────────────────────
    [[nodiscard]] const Filters& filters() const { return m_filters; }
    void setFilters(const Filters& filters);
    void modifyFilters(const std::function<void(Filters&)>& modification);
    void resetFilters();

    // ── Detection ────────────────────────────────────────────────────────────
    [[nodiscard]] const AlertParameters& parameters() const { return m_parameters; }
    void setParameters(const AlertParameters& parameters);
    void detections(QObject* context, std::function<void(const QList<Detection>&)> callback);

    // ── Exclusive activity (import, save, open) ──────────────────────────────
    [[nodiscard]] bool busy() const { return m_busy; }
    void setBusy(bool busy);
    void suspendAccess();
    void resumeAccess(bool dataModified);

    // ── Navigation ───────────────────────────────────────────────────────────
    void navigate(const Navigation& navigation) { emit navigationRequested(navigation); }
    void explore(const Filters& filters);

signals:
    void sessionChanged();
    void dataModified();
    void filtersChanged();
    void parametersChanged();
    void documentStateChanged();
    void busyChanged(bool busy);
    void detectionsAvailable(const QList<lh::Detection>& detections);
    void detectionsInvalidated();
    void navigationRequested(const lh::ui::Navigation& navigation);

private:
    void refreshCaches();
    void invalidateDetections();

    Executor m_executor;
    std::unique_ptr<Session> m_session;
    Filters m_filters;
    AlertParameters m_parameters;
    qint64 m_eventCount = 0;
    QList<Source> m_sources;
    QDate m_firstDate;
    QDate m_lastDate;
    QByteArray m_timeZone;
    bool m_busy = false;

    struct Pending
    {
        QPointer<QObject> context;
        std::function<void(const QList<Detection>&)> callback;
    };
    QList<Detection> m_detections;
    bool m_detectionsValid = false;
    TokenPtr m_detectionsToken;
    QList<Pending> m_pending;
};

} // namespace lh::ui
