#include "Export.h"

#include "Actions.h"
#include "Blocks.h"
#include "Calendar.h"
#include "Catalog.h"
#include "Mitre.h"
#include "Session.h"
#include "DateTime.h"
#include "loghunter_version.h"

#include <miniz.h>

#include <QDateTime>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QTemporaryDir>

#include <stdexcept>

namespace lh::exporter {

namespace {

struct ExportRow
{
    QString timestamp;
    qint64 tsUtc = 0;
    bool tsUtcKnown = false;
    QString user;
    bool isSystem = false;
    QString action;
    QString category;
    QString file;
    QString folder;
    QString process;
    QString access;
    qint64 eventId = 0;
    qint64 recordId = 0;
    QString machine;
    QString log;
    int level = 4;
    int audit = 0;
    QString channel;
    QString provider;
    QString sourceIp;
    QString destinationIp;
    QString detail;
    QList<QPair<QString, QString>> data;

    [[nodiscard]] QString description() const
    {
        return eventDescription(static_cast<quint32>(eventId), channel);
    }
    [[nodiscard]] QString readableLevel() const { return levelLabel(level, audit); }
};

class Reader
{
public:
    Reader(sql::Connection& connection, const Filters& filters, bool data) : m_data(data)
    {
        if (data)
            m_blockRead = connection.prepare(QStringLiteral("SELECT data FROM blocks WHERE id = ?"));
        const Clause clause = filters.clause();
        m_total = connection.integer(QStringLiteral("SELECT COUNT(*) FROM event_view") + clause.where(), clause.parameters);
        m_statement = connection.prepare(
            QStringLiteral("SELECT timestamp, ts_utc, user, is_system, action, action_cat, file_path, dir_path, process, "
                           "access, event_id, record_id, machine, "
                           "(SELECT s.name FROM sources s WHERE s.id = event_view.source_id), "
                           "level, audit, channel, provider, source_ip, destination_ip, detail, %1 "
                           "FROM event_view%2 ORDER BY ts_utc, id")
                .arg(data ? QStringLiteral("block, rank") : QStringLiteral("0, 0"), clause.where()));
        m_statement.bindAll(clause.parameters);
    }

    [[nodiscard]] qint64 total() const noexcept { return m_total; }

    bool next(ExportRow& row)
    {
        if (!m_statement.step())
            return false;
        row.timestamp = m_statement.text(0);
        row.tsUtcKnown = !m_statement.isNull(1);
        row.tsUtc = m_statement.integer(1);
        row.user = m_statement.text(2);
        row.isSystem = m_statement.integer(3) != 0;
        row.action = m_statement.text(4);
        row.category = m_statement.text(5);
        row.file = m_statement.text(6);
        row.folder = m_statement.text(7);
        row.process = m_statement.text(8);
        row.access = m_statement.text(9);
        row.eventId = m_statement.integer(10);
        row.recordId = m_statement.integer(11);
        row.machine = m_statement.text(12);
        row.log = m_statement.text(13);
        row.level = static_cast<int>(m_statement.integer(14));
        row.audit = static_cast<int>(m_statement.integer(15));
        row.channel = m_statement.text(16);
        row.provider = m_statement.text(17);
        row.sourceIp = m_statement.text(18);
        row.destinationIp = m_statement.text(19);
        row.detail = m_statement.text(20);
        if (m_data)
            row.data = fields(m_statement.integer(21), m_statement.integer(22));
        return true;
    }

private:
    using Fields = QList<QPair<QString, QString>>;
    static constexpr qsizetype CACHED_BLOCKS = 32;

    Fields fields(qint64 block, qint64 rank)
    {
        if (block <= 0)
            return {};
        auto found = m_cache.constFind(block);
        if (found == m_cache.cend()) {
            if (m_cache.size() >= CACHED_BLOCKS)
                m_cache.clear();
            QList<Fields> events;
            m_blockRead.reset();
            m_blockRead.bind(1, block);
            if (m_blockRead.step())
                blocks::scan(m_blockRead.blob(0), false, [&](const evtx::DecodedEvent& e) {
                    Fields& list = events.emplaceBack();
                    for (const evtx::DataField& field : e.data)
                        list.append({field.name, field.value});
                    return true;
                });
            found = m_cache.insert(block, std::move(events));
        }
        return found.value().value(rank);
    }

    sql::Statement m_statement;
    sql::Statement m_blockRead;
    QHash<qint64, QList<Fields>> m_cache;
    qint64 m_total = 0;
    bool m_data = false;
};

QStringList headers(const QString& timeZone)
{
    return {qtTrId("export.column_timestamp").arg(timeZone),
            qtTrId("export.column_timestamp_utc"),
            qtTrId("common.level"),
            qtTrId("export.column_event_id"),
            qtTrId("common.description"),
            qtTrId("common.windows_log"),
            qtTrId("common.machine"),
            qtTrId("common.user"),
            qtTrId("export.column_system_account"),
            qtTrId("export.column_process"),
            qtTrId("common.source_ip"),
            qtTrId("common.destination_ip"),
            qtTrId("common.detail"),
            qtTrId("common.action"),
            qtTrId("export.column_category"),
            qtTrId("common.file"),
            qtTrId("common.folder"),
            qtTrId("export.column_access_rights"),
            qtTrId("export.column_record_id"),
            qtTrId("export.column_source_log")};
}

QString readableTimestamp(const QString& iso)
{
    return iso.size() >= 19 ? iso.left(10) + QLatin1Char(' ') + iso.mid(11, 8) : iso;
}

QString readableUtc(const ExportRow& row)
{
    return row.tsUtcKnown ? formatUtc(row.tsUtc) : QString();
}

bool cancelled(const std::atomic_bool* cancellation)
{
    return cancellation && cancellation->load(std::memory_order_relaxed);
}

void reportProgress(const ProgressCallback& progress, qint64 rows, qint64 total)
{
    if (progress && (rows & 0x3fff) == 0)
        progress(rows, total);
}

// ── CSV ──────────────────────────────────────────────────────────────────────

QString csvField(const QString& value)
{
    const QString neutralized = neutralizeFormula(value);
    if (neutralized.contains(QLatin1Char(';')) || neutralized.contains(QLatin1Char('"')) || neutralized.contains(QLatin1Char('\n'))
        || neutralized.contains(QLatin1Char('\r'))) {
        QString escaped = neutralized;
        escaped.replace(QLatin1Char('"'), QLatin1String("\"\""));
        return QLatin1Char('"') + escaped + QLatin1Char('"');
    }
    return neutralized;
}

Result exportCsv(Reader& reader, const QStringList& columns, QSaveFile& file, const ProgressCallback& progress,
                 const std::atomic_bool* cancellation)
{
    Result result;
    QByteArray buffer;
    buffer.reserve(1 << 20);
    buffer.append("\xEF\xBB\xBF");
    QStringList fields;
    for (const QString& column : columns)
        fields << csvField(column);
    buffer.append(fields.join(QLatin1Char(';')).toUtf8()).append("\r\n");

    const QString yes = qtTrId("export.yes");
    const QString no = qtTrId("export.no");
    ExportRow row;
    while (reader.next(row)) {
        const QString text = QStringList{readableTimestamp(row.timestamp),
                                         readableUtc(row),
                                         row.readableLevel(),
                                         QString::number(row.eventId),
                                         csvField(row.description()),
                                         csvField(row.channel),
                                         csvField(row.machine),
                                         csvField(row.user),
                                         row.isSystem ? yes : no,
                                         csvField(row.process),
                                         csvField(row.sourceIp),
                                         csvField(row.destinationIp),
                                         csvField(row.detail),
                                         csvField(row.action.isEmpty() ? QString() : actionLabel(row.action)),
                                         row.category.isEmpty() ? QString() : categoryLabel(row.category),
                                         csvField(row.file),
                                         csvField(row.folder),
                                         csvField(row.access),
                                         QString::number(row.recordId),
                                         csvField(row.log)}
                                  .join(QLatin1Char(';'));
        buffer.append(text.toUtf8()).append("\r\n");
        ++result.rows;
        if (buffer.size() > (1 << 20)) {
            file.write(buffer);
            buffer.clear();
        }
        reportProgress(progress, result.rows, reader.total());
        if ((result.rows & 0xfff) == 0 && cancelled(cancellation)) {
            result.cancelled = true;
            return result;
        }
    }
    file.write(buffer);
    return result;
}

// ── JSON ─────────────────────────────────────────────────────────────────────

void appendJsonText(QByteArray& output, const QString& text)
{
    static constexpr char HEX[] = "0123456789abcdef";
    const QByteArray utf8 = text.toUtf8();
    output.append('"');
    for (const char c : utf8) {
        const auto byte = static_cast<unsigned char>(c);
        if (c == '"' || c == '\\') {
            output.append('\\').append(c);
        } else if (byte < 0x20) {
            switch (c) {
            case '\n':
                output.append("\\n");
                break;
            case '\r':
                output.append("\\r");
                break;
            case '\t':
                output.append("\\t");
                break;
            default:
                output.append("\\u00").append(HEX[byte >> 4]).append(HEX[byte & 0xf]);
            }
        } else {
            output.append(c);
        }
    }
    output.append('"');
}

void addPair(QByteArray& output, const char* key, const QString& value, bool isFirst = false)
{
    if (!isFirst)
        output.append(',');
    output.append('"').append(key).append("\":");
    appendJsonText(output, value);
}


Result exportJson(Reader& reader, QSaveFile& file, const QByteArray& timeZone, const ProgressCallback& progress,
                  const std::atomic_bool* cancellation)
{
    Result result;
    QByteArray buffer;
    buffer.reserve(1 << 20);
    const QJsonObject header{{QStringLiteral("application"), QStringLiteral(LOGHUNTER_NAME_VERSION)},
                             {QStringLiteral("generated_at"), QDateTime::currentDateTimeUtc().toString(Qt::ISODate)},
                             {QStringLiteral("time_zone"), QString::fromLatin1(timeZone)}};
    buffer.append("{\"export\":").append(QJsonDocument(header).toJson(QJsonDocument::Compact)).append(",\n\"events\":[\n");
    ExportRow row;
    QStringList views;
    while (reader.next(row)) {
        if (result.rows > 0)
            buffer.append(",\n");
        buffer.append('{');
        addPair(buffer, "timestamp", row.timestamp, true);
        buffer.append(",\"timestamp_utc\":");
        if (row.tsUtcKnown)
            appendJsonText(buffer, formatUtc(row.tsUtc));
        else
            buffer.append("null");
        buffer.append(",\"level\":").append(QByteArray::number(row.level));
        addPair(buffer, "audit",
                row.audit == 2   ? QStringLiteral("failure")
                : row.audit == 1 ? QStringLiteral("success")
                                 : QString());
        buffer.append(",\"event_id\":").append(QByteArray::number(row.eventId));
        addPair(buffer, "description", row.description());
        addPair(buffer, "channel", row.channel);
        addPair(buffer, "provider", row.provider);
        addPair(buffer, "machine", row.machine);
        addPair(buffer, "user", row.user);
        buffer.append(",\"system_account\":").append(row.isSystem ? "true" : "false");
        addPair(buffer, "process", row.process);
        addPair(buffer, "source_ip", row.sourceIp);
        addPair(buffer, "destination_ip", row.destinationIp);
        addPair(buffer, "detail", row.detail);
        addPair(buffer, "action", row.action);
        addPair(buffer, "category", row.category);
        addPair(buffer, "file", row.file);
        addPair(buffer, "folder", row.folder);
        addPair(buffer, "access", row.access);
        buffer.append(",\"record_id\":").append(QByteArray::number(row.recordId));
        addPair(buffer, "log", row.log);
        buffer.append(",\"data\":{");
        views.clear();
        for (const auto& [name, value] : std::as_const(row.data)) {
            QString key = name;
            for (int rank = 2; views.contains(key); ++rank)
                key = QStringLiteral("%1_%2").arg(name).arg(rank);
            views << key;
            if (views.size() > 1)
                buffer.append(',');
            appendJsonText(buffer, key);
            buffer.append(':');
            appendJsonText(buffer, value);
        }
        buffer.append("}}");
        ++result.rows;
        if (buffer.size() > (1 << 20)) {
            file.write(buffer);
            buffer.clear();
        }
        reportProgress(progress, result.rows, reader.total());
        if ((result.rows & 0xfff) == 0 && cancelled(cancellation)) {
            result.cancelled = true;
            return result;
        }
    }
    buffer.append("\n]}\n");
    file.write(buffer);
    return result;
}

// ── Excel (.xlsx) ────────────────────────────────────────────────────────────

void appendXml(QByteArray& output, const QString& text)
{
    bool simple = true;
    for (const QChar c : text) {
        const char16_t u = c.unicode();
        if (u == u'&' || u == u'<' || u == u'>' || u == u'"' || u < 0x20 || u >= 0xFFFE || c.isSurrogate()) {
            simple = false;
            break;
        }
    }
    if (simple) {
        output.append(text.toUtf8());
        return;
    }
    QString cleaned;
    cleaned.reserve(text.size() + 16);
    for (qsizetype i = 0; i < text.size(); ++i) {
        const QChar c = text[i];
        const char16_t u = c.unicode();
        if (c.isHighSurrogate() && i + 1 < text.size() && text[i + 1].isLowSurrogate()) {
            cleaned.append(c).append(text[++i]);
            continue;
        }
        if (c.isSurrogate() || u == 0xFFFE || u == 0xFFFF || (u < 0x20 && u != u'\t' && u != u'\n' && u != u'\r'))
            continue;
        switch (u) {
        case u'&':
            cleaned.append(QLatin1String("&amp;"));
            break;
        case u'<':
            cleaned.append(QLatin1String("&lt;"));
            break;
        case u'>':
            cleaned.append(QLatin1String("&gt;"));
            break;
        case u'"':
            cleaned.append(QLatin1String("&quot;"));
            break;
        default:
            cleaned.append(c);
        }
    }
    output.append(cleaned.toUtf8());
}

void textCell(QByteArray& output, const QString& text, int style = 0)
{
    if (text.isEmpty()) {
        output.append("<c/>");
        return;
    }
    output.append(style ? "<c t=\"inlineStr\" s=\"1\"><is><t" : "<c t=\"inlineStr\"><is><t");
    if (text.front().isSpace() || text.back().isSpace())
        output.append(" xml:space=\"preserve\"");
    output.append('>');
    appendXml(output, text);
    output.append("</t></is></c>");
}

void numberCell(QByteArray& output, qint64 value)
{
    output.append("<c><v>").append(QByteArray::number(value)).append("</v></c>");
}

void dateCell(QByteArray& output, const QString& iso)
{
    if (iso.size() < 19) {
        textCell(output, iso);
        return;
    }
    bool ok[6];
    const int year = iso.left(4).toInt(&ok[0]);
    const int month = iso.mid(5, 2).toInt(&ok[1]);
    const int day = iso.mid(8, 2).toInt(&ok[2]);
    const int hour = iso.mid(11, 2).toInt(&ok[3]);
    const int minute = iso.mid(14, 2).toInt(&ok[4]);
    const int second = iso.mid(17, 2).toInt(&ok[5]);
    for (bool b : ok) {
        if (!b) {
            textCell(output, iso);
            return;
        }
    }
    const double series = static_cast<double>(daysFromDate(year, static_cast<unsigned>(month),
                                                            static_cast<unsigned>(day)) + 25569)
                          + (hour * 3600 + minute * 60 + second) / 86400.0;
    output.append("<c s=\"2\"><v>").append(QByteArray::number(series, 'f', 8)).append("</v></c>");
}

class XlsxSheet
{
public:
    XlsxSheet(const QString& path, const QStringList& columns, const QList<int>& widths) : m_file(path)
    {
        if (!m_file.open(QIODevice::WriteOnly))
            throw std::runtime_error(qtTrId("export.error_temporary_file").toStdString());
        m_buffer.append(
            "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n"
            "<worksheet xmlns=\"http://schemas.openxmlformats.org/spreadsheetml/2006/main\" "
            "xmlns:r=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships\">"
            "<sheetViews><sheetView workbookViewId=\"0\"><pane ySplit=\"1\" topLeftCell=\"A2\" activePane=\"bottomLeft\" "
            "state=\"frozen\"/></sheetView></sheetViews><cols>");
        for (int i = 0; i < columns.size(); ++i)
            m_buffer.append(QStringLiteral("<col min=\"%1\" max=\"%1\" width=\"%2\" customWidth=\"1\"/>")
                                .arg(i + 1)
                                .arg(i < widths.size() ? widths[i] : 24)
                                .toUtf8());
        m_buffer.append("</cols><sheetData><row>");
        for (const QString& column : columns)
            textCell(m_buffer, column, 1);
        m_buffer.append("</row>");
        m_columns = static_cast<int>(columns.size());
    }

    QByteArray& row() { return m_buffer; }

    void lineEnd()
    {
        ++m_rows;
        if (m_buffer.size() > (1 << 20)) {
            m_file.write(m_buffer);
            m_buffer.clear();
        }
    }

    [[nodiscard]] qint64 rows() const noexcept { return m_rows; }

    void finish()
    {
        QString lastColumn;
        for (int n = m_columns; n > 0; n = (n - 1) / 26)
            lastColumn.prepend(QChar(u'A' + static_cast<char16_t>((n - 1) % 26)));
        m_buffer.append("</sheetData>");
        m_buffer.append(QStringLiteral("<autoFilter ref=\"A1:%1%2\"/>").arg(lastColumn).arg(m_rows + 1).toUtf8());
        m_buffer.append("</worksheet>");
        m_file.write(m_buffer);
        m_buffer.clear();
        m_file.close();
    }

    [[nodiscard]] QString path() const { return m_file.fileName(); }

private:
    QFile m_file;
    QByteArray m_buffer;
    qint64 m_rows = 0;
    int m_columns = 0;
};

size_t writeZip(void* opaque, mz_uint64, const void* data, size_t size)
{
    auto* file = static_cast<QSaveFile*>(opaque);
    return static_cast<size_t>(file->write(static_cast<const char*>(data), static_cast<qint64>(size)));
}

size_t readFile(void* opaque, mz_uint64 position, void* data, size_t size)
{
    auto* file = static_cast<QFile*>(opaque);
    if (!file->seek(static_cast<qint64>(position)))
        return 0;
    const qint64 readCount = file->read(static_cast<char*>(data), static_cast<qint64>(size));
    return readCount < 0 ? 0 : static_cast<size_t>(readCount);
}

QByteArray workbookXml(int sheets, const QString& sheetName)
{
    QByteArray xml(
        "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n"
        "<workbook xmlns=\"http://schemas.openxmlformats.org/spreadsheetml/2006/main\" "
        "xmlns:r=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships\"><sheets>");
    for (int i = 1; i <= sheets; ++i) {
        const QString name = sheets == 1 ? sheetName : qtTrId("export.sheet_number").arg(sheetName).arg(i);
        xml.append(QStringLiteral("<sheet name=\"%1\" sheetId=\"%2\" r:id=\"rId%2\"/>").arg(name).arg(i).toUtf8());
    }
    xml.append("</sheets></workbook>");
    return xml;
}

QByteArray workbookRelationsXml(int sheets)
{
    QByteArray xml("<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n"
                   "<Relationships xmlns=\"http://schemas.openxmlformats.org/package/2006/relationships\">");
    for (int i = 1; i <= sheets; ++i)
        xml.append(QStringLiteral("<Relationship Id=\"rId%1\" "
                                  "Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/worksheet\" "
                                  "Target=\"worksheets/sheet%1.xml\"/>")
                       .arg(i)
                       .toUtf8());
    xml.append(QStringLiteral("<Relationship Id=\"rId%1\" "
                              "Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/styles\" "
                              "Target=\"styles.xml\"/></Relationships>")
                   .arg(sheets + 1)
                   .toUtf8());
    return xml;
}

QByteArray contentTypesXml(int sheets)
{
    QByteArray xml(
        "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n"
        "<Types xmlns=\"http://schemas.openxmlformats.org/package/2006/content-types\">"
        "<Default Extension=\"rels\" ContentType=\"application/vnd.openxmlformats-package.relationships+xml\"/>"
        "<Default Extension=\"xml\" ContentType=\"application/xml\"/>"
        "<Override PartName=\"/xl/workbook.xml\" "
        "ContentType=\"application/vnd.openxmlformats-officedocument.spreadsheetml.sheet.main+xml\"/>"
        "<Override PartName=\"/xl/styles.xml\" "
        "ContentType=\"application/vnd.openxmlformats-officedocument.spreadsheetml.styles+xml\"/>"
        "<Override PartName=\"/docProps/core.xml\" "
        "ContentType=\"application/vnd.openxmlformats-package.core-properties+xml\"/>");
    for (int i = 1; i <= sheets; ++i)
        xml.append(QStringLiteral("<Override PartName=\"/xl/worksheets/sheet%1.xml\" "
                                  "ContentType=\"application/vnd.openxmlformats-officedocument.spreadsheetml.worksheet+xml\"/>")
                       .arg(i)
                       .toUtf8());
    xml.append("</Types>");
    return xml;
}

const char XML_RELATIONS[] =
    "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n"
    "<Relationships xmlns=\"http://schemas.openxmlformats.org/package/2006/relationships\">"
    "<Relationship Id=\"rId1\" "
    "Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/officeDocument\" Target=\"xl/workbook.xml\"/>"
    "<Relationship Id=\"rId2\" "
    "Type=\"http://schemas.openxmlformats.org/package/2006/relationships/metadata/core-properties\" "
    "Target=\"docProps/core.xml\"/></Relationships>";

QByteArray stylesXml()
{
    QByteArray xml(
        "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n"
        "<styleSheet xmlns=\"http://schemas.openxmlformats.org/spreadsheetml/2006/main\">"
        "<numFmts count=\"1\"><numFmt numFmtId=\"164\" formatCode=\"");
    xml.append(displayedDateFormat().toLower().toHtmlEscaped().toUtf8());
    xml.append(
        " hh:mm:ss\"/></numFmts>"
        "<fonts count=\"2\"><font><sz val=\"11\"/><name val=\"Calibri\"/></font>"
        "<font><b/><sz val=\"11\"/><name val=\"Calibri\"/></font></fonts>"
        "<fills count=\"3\"><fill><patternFill patternType=\"none\"/></fill><fill><patternFill patternType=\"gray125\"/></fill>"
        "<fill><patternFill patternType=\"solid\"><fgColor rgb=\"FFE5E7EB\"/><bgColor indexed=\"64\"/></patternFill></fill></fills>"
        "<borders count=\"1\"><border><left/><right/><top/><bottom/><diagonal/></border></borders>"
        "<cellStyleXfs count=\"1\"><xf numFmtId=\"0\" fontId=\"0\" fillId=\"0\" borderId=\"0\"/></cellStyleXfs>"
        "<cellXfs count=\"3\"><xf numFmtId=\"0\" fontId=\"0\" fillId=\"0\" borderId=\"0\" xfId=\"0\"/>"
        "<xf numFmtId=\"0\" fontId=\"1\" fillId=\"2\" borderId=\"0\" xfId=\"0\" applyFont=\"1\" applyFill=\"1\"/>"
        "<xf numFmtId=\"164\" fontId=\"0\" fillId=\"0\" borderId=\"0\" xfId=\"0\" applyNumberFormat=\"1\"/></cellXfs>"
        "<cellStyles count=\"1\"><cellStyle name=\"Normal\" xfId=\"0\" builtinId=\"0\"/></cellStyles></styleSheet>");
    return xml;
}

QByteArray propertiesXml()
{
    QByteArray xml(
        "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n"
        "<cp:coreProperties xmlns:cp=\"http://schemas.openxmlformats.org/package/2006/metadata/core-properties\" "
        "xmlns:dc=\"http://purl.org/dc/elements/1.1/\" xmlns:dcterms=\"http://purl.org/dc/terms/\" "
        "xmlns:xsi=\"http://www.w3.org/2001/XMLSchema-instance\">"
        "<dc:title>LogHunter export</dc:title><dc:creator>" LOGHUNTER_NAME_VERSION "</dc:creator>"
        "<dcterms:created xsi:type=\"dcterms:W3CDTF\">");
    xml.append(QDateTime::currentDateTimeUtc().toString(Qt::ISODate).toUtf8());
    xml.append("</dcterms:created></cp:coreProperties>");
    return xml;
}

const QList<int> EVENT_WIDTHS{20, 28, 16, 10, 36, 22, 18, 26, 10, 40, 16, 16, 60, 22, 13, 70, 50, 30, 16, 26};

void writeWorkbook(QSaveFile& file, const std::vector<std::unique_ptr<XlsxSheet>>& sheets,
                   const QString& sheetName)
{
    const int count = static_cast<int>(sheets.size());
    mz_zip_archive zip{};
    zip.m_pWrite = writeZip;
    zip.m_pIO_opaque = &file;
    if (!mz_zip_writer_init_v2(&zip, 0, 0))
        throw std::runtime_error(qtTrId("export.error_excel_initialization").toStdString());
    bool ok = true;
    auto add = [&](const char* name, const QByteArray& content) {
        ok = ok && mz_zip_writer_add_mem(&zip, name, content.constData(), static_cast<size_t>(content.size()),
                                         MZ_DEFAULT_LEVEL);
    };
    add("[Content_Types].xml", contentTypesXml(count));
    add("_rels/.rels", QByteArray(XML_RELATIONS));
    add("docProps/core.xml", propertiesXml());
    add("xl/workbook.xml", workbookXml(count, sheetName));
    add("xl/_rels/workbook.xml.rels", workbookRelationsXml(count));
    add("xl/styles.xml", stylesXml());
    for (int i = 0; ok && i < count; ++i) {
        QFile source(sheets[static_cast<size_t>(i)]->path());
        if (!source.open(QIODevice::ReadOnly)) {
            ok = false;
            break;
        }
        const QByteArray name = QStringLiteral("xl/worksheets/sheet%1.xml").arg(i + 1).toUtf8();
        ok = mz_zip_writer_add_read_buf_callback(&zip, name.constData(), readFile, &source,
                                                 static_cast<mz_uint64>(source.size()), nullptr, nullptr, 0,
                                                 MZ_BEST_SPEED, nullptr, 0, nullptr, 0);
    }
    ok = ok && mz_zip_writer_finalize_archive(&zip);
    mz_zip_writer_end(&zip);
    if (!ok)
        throw std::runtime_error(qtTrId("export.error_excel_archive").toStdString());
}

Result exportExcel(Reader& reader, const QStringList& columns, QSaveFile& file,
                   const ProgressCallback& progress, const std::atomic_bool* cancellation)
{
    Result result;
    QTemporaryDir folder;
    if (!folder.isValid())
        throw std::runtime_error(qtTrId("export.error_temporary_folder").toStdString());

    std::vector<std::unique_ptr<XlsxSheet>> sheets;
    auto newSheet = [&] {
        sheets.push_back(std::make_unique<XlsxSheet>(
            folder.filePath(QStringLiteral("sheet%1.xml").arg(sheets.size() + 1)), columns, EVENT_WIDTHS));
    };
    newSheet();

    const QString yes = qtTrId("export.yes");
    const QString no = qtTrId("export.no");
    ExportRow row;
    while (reader.next(row)) {
        if (sheets.back()->rows() >= ROWS_PER_SHEET) {
            sheets.back()->finish();
            newSheet();
        }
        QByteArray& x = sheets.back()->row();
        x.append("<row>");
        dateCell(x, row.timestamp);
        textCell(x, readableUtc(row));
        textCell(x, row.readableLevel());
        numberCell(x, row.eventId);
        textCell(x, row.description());
        textCell(x, row.channel);
        textCell(x, row.machine);
        textCell(x, row.user);
        textCell(x, row.isSystem ? yes : no);
        textCell(x, row.process);
        textCell(x, row.sourceIp);
        textCell(x, row.destinationIp);
        textCell(x, row.detail);
        textCell(x, row.action.isEmpty() ? QString() : actionLabel(row.action));
        textCell(x, row.category.isEmpty() ? QString() : categoryLabel(row.category));
        textCell(x, row.file);
        textCell(x, row.folder);
        textCell(x, row.access);
        numberCell(x, row.recordId);
        textCell(x, row.log);
        x.append("</row>");
        sheets.back()->lineEnd();
        ++result.rows;
        reportProgress(progress, result.rows, reader.total());
        if ((result.rows & 0xfff) == 0 && cancelled(cancellation)) {
            result.cancelled = true;
            return result;
        }
    }
    sheets.back()->finish();
    result.sheets = static_cast<int>(sheets.size());
    writeWorkbook(file, sheets, qtTrId("common.events"));
    return result;
}

// ── Alerts ───────────────────────────────────────────────────────────────────

struct AlertRow
{
    QString severity;
    QString title;
    QString origin;
    QString tactics;
    QString techniques;
    QString count;
    QString detail;
};

QList<AlertRow> alertRows(const QList<Detection>& detections)
{
    QList<AlertRow> rows;
    for (const Detection& d : detections) {
        if (d.count <= 0)
            continue;
        const mitre::Mapping match = mitre::parse(d.tags);
        QStringList tactics;
        for (const QString& tactic : match.tactics)
            tactics << mitre::tacticLabel(tactic);
        AlertRow base{severityLabel(d.severity),
                      d.title,
                      d.sigma ? qtTrId("export.origin_sigma") : qtTrId("export.origin_builtin"),
                      tactics.join(QStringLiteral(", ")),
                      match.techniques.join(QStringLiteral(", ")),
                      d.quantity(),
                      {}};
        if (d.rows.isEmpty()) {
            rows.append(base);
            continue;
        }
        for (const QVariantList& values : d.rows) {
            QStringList parts;
            for (qsizetype c = 0; c < values.size() && c < d.columns.size(); ++c) {
                const QString text = formatCell(values[c], d.columns[c].type);
                if (!text.isEmpty())
                    parts << qtTrId("common.label_value").arg(d.columns[c].title, text);
            }
            AlertRow row = base;
            row.detail = parts.join(QStringLiteral(" · "));
            rows.append(row);
        }
    }
    return rows;
}

QStringList alertHeaders()
{
    return {qtTrId("report.severity"),        qtTrId("common.alert"),        qtTrId("export.column_origin"),
            qtTrId("export.column_tactics"), qtTrId("export.column_techniques"), qtTrId("report.findings"),
            qtTrId("common.detail")};
}

QByteArray alertsJson(const QList<Detection>& detections, const QString& scope)
{
    QJsonArray alerts;
    for (const Detection& d : detections) {
        if (d.count <= 0)
            continue;
        const mitre::Mapping match = mitre::parse(d.tags);
        QJsonArray findings;
        for (const QVariantList& values : d.rows) {
            QJsonObject finding;
            for (qsizetype c = 0; c < values.size() && c < d.columns.size(); ++c)
                finding.insert(d.columns[c].title, formatCell(values[c], d.columns[c].type));
            findings.append(finding);
        }
        alerts.append(QJsonObject{
            {QStringLiteral("title"), d.title},
            {QStringLiteral("description"), d.description},
            {QStringLiteral("severity"), severityLabel(d.severity)},
            {QStringLiteral("origin"), d.sigma ? QStringLiteral("sigma") : QStringLiteral("builtin")},
            {QStringLiteral("rule"), d.rule},
            {QStringLiteral("tactics"), QJsonArray::fromStringList(match.tactics)},
            {QStringLiteral("techniques"), QJsonArray::fromStringList(match.techniques)},
            {QStringLiteral("count"), d.count},
            {QStringLiteral("findings"), findings},
        });
    }
    const QJsonObject document{
        {QStringLiteral("export"),
         QJsonObject{{QStringLiteral("application"), QStringLiteral(LOGHUNTER_NAME_VERSION)},
                     {QStringLiteral("generated_at"), QDateTime::currentDateTimeUtc().toString(Qt::ISODate)},
                     {QStringLiteral("scope"), scope}}},
        {QStringLiteral("risk_score"), riskScore(detections)},
        {QStringLiteral("alerts"), alerts},
    };
    return QJsonDocument(document).toJson(QJsonDocument::Indented);
}

} // namespace

QString neutralizeFormula(const QString& value)
{
    if (!value.isEmpty()) {
        const QChar first = value.front();
        if (first == u'=' || first == u'+' || first == u'-' || first == u'@' || first == u'\t'
            || first == u'\r')
            return QLatin1Char('\'') + value;
    }
    return value;
}

Result exportEvents(sql::Connection& connection, const Filters& filters, Format format, const QString& path,
                    const ProgressCallback& progress, const std::atomic_bool* cancellation)
{
    const QByteArray timeZone = db::readMeta(connection, QStringLiteral("time_zone"), QStringLiteral("UTC")).toLatin1();
    Reader reader(connection, filters, format == Format::Json);
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly))
        throw std::runtime_error(qtTrId("export.error_file_creation").arg(file.errorString()).toStdString());

    const QStringList columns = headers(QString::fromLatin1(timeZone));
    Result result;
    switch (format) {
    case Format::Csv:
        result = exportCsv(reader, columns, file, progress, cancellation);
        break;
    case Format::Json:
        result = exportJson(reader, file, timeZone, progress, cancellation);
        break;
    case Format::Excel:
        result = exportExcel(reader, columns, file, progress, cancellation);
        break;
    }
    if (result.cancelled) {
        file.cancelWriting();
        return result;
    }
    if (!file.commit())
        throw std::runtime_error(qtTrId("export.error_write").arg(file.errorString()).toStdString());
    if (progress)
        progress(result.rows, reader.total());
    return result;
}

Result exportAlerts(const QList<Detection>& detections, const QString& scope, Format format,
                    const QString& path)
{
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly))
        throw std::runtime_error(qtTrId("export.error_file_creation").arg(file.errorString()).toStdString());
    Result result;
    const QList<AlertRow> rows = alertRows(detections);
    result.rows = rows.size();
    const QStringList columns = alertHeaders();
    switch (format) {
    case Format::Json:
        file.write(alertsJson(detections, scope));
        break;
    case Format::Csv: {
        QByteArray buffer("\xEF\xBB\xBF");
        QStringList fields;
        for (const QString& column : columns)
            fields << csvField(column);
        buffer.append(fields.join(QLatin1Char(';')).toUtf8()).append("\r\n");
        for (const AlertRow& l : rows)
            buffer.append(QStringList{csvField(l.severity), csvField(l.title), csvField(l.origin),
                                      csvField(l.tactics), csvField(l.techniques), csvField(l.count),
                                      csvField(l.detail)}
                              .join(QLatin1Char(';'))
                              .toUtf8())
                .append("\r\n");
        file.write(buffer);
        break;
    }
    case Format::Excel: {
        QTemporaryDir folder;
        if (!folder.isValid())
            throw std::runtime_error(qtTrId("export.error_temporary_folder").toStdString());
        std::vector<std::unique_ptr<XlsxSheet>> sheets;
        sheets.push_back(std::make_unique<XlsxSheet>(folder.filePath(QStringLiteral("sheet1.xml")), columns,
                                                     QList<int>{12, 50, 18, 34, 22, 16, 120}));
        for (const AlertRow& l : rows) {
            QByteArray& x = sheets.back()->row();
            x.append("<row>");
            for (const QString* value : {&l.severity, &l.title, &l.origin, &l.tactics, &l.techniques, &l.count,
                                         &l.detail})
                textCell(x, *value);
            x.append("</row>");
            sheets.back()->lineEnd();
        }
        sheets.back()->finish();
        result.sheets = 1;
        writeWorkbook(file, sheets, qtTrId("common.alerts"));
        break;
    }
    }
    if (!file.commit())
        throw std::runtime_error(qtTrId("export.error_write").arg(file.errorString()).toStdString());
    return result;
}

} // namespace lh::exporter
