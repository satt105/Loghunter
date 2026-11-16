#include "TabularLog.h"

#include "core/Blocks.h"
#include "core/Calendar.h"
#include "core/DateTime.h"

#include <QLocale>

#include <algorithm>

namespace lh::logs {

namespace {

constexpr uint64_t SUCCESS_KEYWORDS = 0x8020000000000000ULL;
constexpr uint64_t FAILURE_KEYWORDS = 0x8010000000000000ULL;
constexpr char DELIMITERS[] = {'\t', ',', ';'};
constexpr int SAMPLED_ROWS = 2;
constexpr size_t COLUMNS = 5;
constexpr size_t COLUMN_LEVEL = 0;
constexpr size_t COLUMN_DATE = 1;
constexpr size_t COLUMN_SOURCE = 2;
constexpr size_t COLUMN_NUMBER = 3;
constexpr size_t COLUMN_CATEGORY = 4;

struct RawDate
{
    int year = 0;
    int firstNumber = 0;
    int secondNumber = 0;
    int hour = 0;
    int minute = 0;
    int second = 0;
    bool yearFirst = false;
    bool twelveHour = false;
};

bool isAsciiDigit(char c) noexcept
{
    return c >= '0' && c <= '9';
}

bool readRecord(QByteArrayView t, qsizetype& p, char delimiter, std::vector<QByteArray>& fields)
{
    fields.clear();
    const qsizetype n = t.size();
    if (p >= n)
        return false;
    while (true) {
        QByteArray field;
        if (p < n && t[p] == '"') {
            ++p;
            while (p < n) {
                const qsizetype quote = t.indexOf('"', p);
                if (quote < 0) {
                    field.append(t.sliced(p));
                    p = n;
                    break;
                }
                field.append(t.sliced(p, quote - p));
                p = quote + 1;
                if (p >= n || t[p] != '"')
                    break;
                field.append('"');
                ++p;
            }
        }
        qsizetype end = p;
        while (end < n && t[end] != delimiter && t[end] != '\n' && t[end] != '\r')
            ++end;
        field.append(t.sliced(p, end - p));
        p = end;
        fields.push_back(std::move(field));
        if (p >= n)
            return true;
        if (t[p] == delimiter) {
            ++p;
            continue;
        }
        if (t[p] == '\r')
            ++p;
        if (p < n && t[p] == '\n')
            ++p;
        return true;
    }
}

bool readDate(QByteArrayView field, RawDate& date)
{
    int numbers[6] = {};
    int digits[6] = {};
    int count = 0;
    qsizetype i = 0;
    qsizetype digitsEnd = 0;
    while (i < field.size() && count < 6) {
        if (!isAsciiDigit(field[i])) {
            ++i;
            continue;
        }
        int value = 0;
        int length = 0;
        while (i < field.size() && isAsciiDigit(field[i]) && length < 9) {
            value = value * 10 + (field[i] - '0');
            ++length;
            ++i;
        }
        numbers[count] = value;
        digits[count] = length;
        ++count;
        digitsEnd = i;
    }
    if (count < 5)
        return false;
    date.yearFirst = digits[0] == 4;
    if (date.yearFirst) {
        date.year = numbers[0];
        date.firstNumber = numbers[1];
        date.secondNumber = numbers[2];
    } else {
        date.firstNumber = numbers[0];
        date.secondNumber = numbers[1];
        date.year = numbers[2] < 100 ? 2000 + numbers[2] : numbers[2];
    }
    date.hour = numbers[3];
    date.minute = numbers[4];
    date.second = count > 5 ? numbers[5] : 0;

    QByteArray suffix = field.sliced(digitsEnd).toByteArray().toLower();
    suffix.replace('.', "");
    suffix.replace(' ', "");
    const bool afternoon = suffix.contains("pm");
    if (afternoon || suffix.contains("am")) {
        date.twelveHour = true;
        if (afternoon && date.hour < 12)
            date.hour += 12;
        else if (!afternoon && date.hour == 12)
            date.hour = 0;
    }

    if (date.year < 1601 || date.year > 9999 || date.hour > 23 || date.minute > 59 || date.second > 60)
        return false;
    if (date.firstNumber < 1 || date.firstNumber > 31 || date.secondNumber < 1 || date.secondNumber > 31)
        return false;
    return date.yearFirst ? date.firstNumber <= 12 : std::min(date.firstNumber, date.secondNumber) <= 12;
}

bool isEvent(const std::vector<QByteArray>& fields, RawDate& date)
{
    if (fields.size() < COLUMNS)
        return false;
    const QByteArrayView number = QByteArrayView(fields[COLUMN_NUMBER]).trimmed();
    if (number.isEmpty() || number.size() > 10 || !std::all_of(number.cbegin(), number.cend(), isAsciiDigit))
        return false;
    return readDate(fields[COLUMN_DATE], date);
}

char findDelimiter(QByteArrayView text)
{
    std::vector<QByteArray> fields;
    for (const char delimiter : DELIMITERS) {
        qsizetype position = 0;
        for (int rowIndex = 0; rowIndex < SAMPLED_ROWS && readRecord(text, position, delimiter, fields); ++rowIndex) {
            RawDate date;
            if (isEvent(fields, date))
                return delimiter;
        }
    }
    return 0;
}

bool localeMonthFirst()
{
    const QString format = QLocale::system().dateFormat(QLocale::ShortFormat);
    const qsizetype month = format.indexOf(QLatin1Char('M'));
    const qsizetype day = format.indexOf(QLatin1Char('d'));
    return month >= 0 && day >= 0 && month < day;
}

int64_t localSeconds(const RawDate& date, bool monthFirst)
{
    int month = date.yearFirst || monthFirst ? date.firstNumber : date.secondNumber;
    int day = date.yearFirst || monthFirst ? date.secondNumber : date.firstNumber;
    if (month > 12)
        std::swap(month, day);
    return daysFromDate(date.year, static_cast<unsigned>(month), static_cast<unsigned>(day)) * 86400
           + date.hour * 3600 + date.minute * 60 + date.second;
}

uint64_t timestamp(const RawDate& date, bool monthFirst, TimeZoneConverter& timeZone)
{
    return evtx::unixMicrosecondsToFileTime(timeZone.toUtc(localSeconds(date, monthFirst) * 1'000'000));
}

void applyLevel(const QString& text, evtx::DecodedEvent& event)
{
    QString t = text.trimmed().toLower();
    t.replace(QChar(0x2019), QLatin1Char('\''));
    if (t.contains(QLatin1String("audit"))) {
        const bool failure = t.contains(QLatin1String("fail")) || t.contains(QLatin1String("chec"));
        event.keywords = failure ? FAILURE_KEYWORDS : SUCCESS_KEYWORDS;
        return;
    }
    if (t.startsWith(QLatin1String("crit")))
        event.level = 1;
    else if (t.startsWith(QLatin1String("err")))
        event.level = 2;
    else if (t.startsWith(QLatin1String("warn")) || t.startsWith(QLatin1String("avert")))
        event.level = 3;
    else if (t.startsWith(QLatin1String("verb")) || t.startsWith(QLatin1String("comment")))
        event.level = 5;
    else
        event.level = 4;
}

QString rawLine(QByteArrayView row)
{
    while (!row.isEmpty() && (row.back() == '\n' || row.back() == '\r'))
        row.chop(1);
    return QString::fromUtf8(row);
}

} // namespace

bool TabularLog::recognize(QByteArrayView text)
{
    return findDelimiter(text) != 0;
}

bool TabularLog::open(const QString& path, QString* error)
{
    if (!m_file.open(path, error))
        return false;
    m_text = utf8Text(m_file.bytes(), true, m_converted);
    m_delimiter = findDelimiter(m_text);
    if (m_delimiter == 0) {
        if (error)
            *error = qtTrId("log.unknown_format");
        return false;
    }

    std::vector<QByteArray> fields;
    bool firstGreater = false;
    bool secondGreater = false;
    bool twelveHour = false;
    RawDate earliest;
    RawDate latest;
    qsizetype position = 0;
    while (true) {
        const qsizetype start = position;
        if (!readRecord(m_text, position, m_delimiter, fields))
            break;
        RawDate date;
        if (!isEvent(fields, date))
            continue;
        if (m_records == 0)
            earliest = date;
        latest = date;
        ++m_records;
        if (!date.yearFirst) {
            firstGreater = firstGreater || date.firstNumber > 12;
            secondGreater = secondGreater || date.secondNumber > 12;
        }
        twelveHour = twelveHour || date.twelveHour;
        if (m_batches.empty() || start - m_batches.back() >= BATCH_SIZE)
            m_batches.push_back(start);
    }
    m_monthFirst = firstGreater != secondGreater ? secondGreater : twelveHour || localeMonthFirst();
    m_descending = m_records > 1
                   && localSeconds(earliest, m_monthFirst) > localSeconds(latest, m_monthFirst);
    return true;
}

Batch TabularLog::decodeBatch(int index, const Callback& callback) const
{
    Batch batch;
    const auto i = static_cast<size_t>(index);
    if (index < 0 || i >= m_batches.size())
        return batch;
    const qsizetype end = i + 1 < m_batches.size() ? m_batches[i + 1] : m_text.size();

    TimeZoneConverter timeZone(m_timeZone);
    QString xml;
    evtx::DecodedEvent current;
    QString message;
    bool isRunning = false;
    const auto finish = [&] {
        if (!isRunning)
            return;
        while (!message.isEmpty() && message.back().isSpace())
            message.chop(1);
        if (!message.isEmpty())
            current.data.push_back({QStringLiteral("Message"), message});
        writeEvent(xml, current);
        isRunning = false;
    };

    std::vector<QByteArray> fields;
    qsizetype position = m_batches[i];
    while (position < end) {
        const qsizetype start = position;
        if (!readRecord(m_text, position, m_delimiter, fields))
            break;
        RawDate date;
        if (!isEvent(fields, date)) {
            if (isRunning)
                message += QLatin1Char('\n') + rawLine(m_text.sliced(start, position - start));
            continue;
        }
        finish();
        current = evtx::DecodedEvent();
        isRunning = true;
        current.eventId = QByteArrayView(fields[COLUMN_NUMBER]).trimmed().toUInt();
        current.eventIdKnown = true;
        current.fileTime = timestamp(date, m_monthFirst, timeZone);
        current.provider = QString::fromUtf8(fields[COLUMN_SOURCE]).trimmed();
        if (current.provider == QLatin1String("Microsoft-Windows-Security-Auditing"))
            current.channel = QStringLiteral("Security");
        applyLevel(QString::fromUtf8(fields[COLUMN_LEVEL]), current);
        const QString category = QString::fromUtf8(fields[COLUMN_CATEGORY]).trimmed();
        if (!category.isEmpty())
            current.data.push_back({QStringLiteral("TaskCategory"), category});
        message.clear();
        for (size_t field = COLUMNS; field < fields.size(); ++field) {
            if (field > COLUMNS)
                message += QLatin1Char(m_delimiter);
            message += QString::fromUtf8(fields[field]);
        }
    }
    finish();

    const QByteArray utf8 = xml.toUtf8();
    const ScanResult scan = scanEvents(utf8, 0, utf8.size(), false, callback);
    batch.readCount = scan.readCount;
    batch.errors = scan.errors;
    if (scan.kept > 0)
        batch.block = blocks::compressText(utf8);
    return batch;
}

} // namespace lh::logs
