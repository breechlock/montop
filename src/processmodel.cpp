#include "processmodel.h"

#include <QSet>
#include <algorithm>

ProcessModel::ProcessModel(QObject *parent) : QAbstractListModel(parent) {}

int ProcessModel::rowCount(const QModelIndex &parent) const {
    return parent.isValid() ? 0 : m_rows.size();
}

QVariant ProcessModel::data(const QModelIndex &index, int role) const {
    if (!index.isValid() || index.row() >= m_rows.size())
        return {};
    const ProcessInfo &p = m_rows.at(index.row());
    switch (role) {
    case PidRole: return p.pid;
    case PpidRole: return p.ppid;
    case Qt::DisplayRole:
    case NameRole: return p.name;
    case CommandRole: return p.command;
    case UserRole: return p.user;
    case StateRole: return QString(QChar(p.state));
    case ThreadsRole: return p.threads;
    case CpuRole: return p.cpu;
    case MemoryRole: return double(p.memory);
    case MemoryShareRole: return p.memoryShare;
    case ReadRateRole: return p.readRate;
    case WriteRateRole: return p.writeRate;
    case KernelRole: return p.kernel;
    }
    return {};
}

QHash<int, QByteArray> ProcessModel::roleNames() const {
    return {
        {PidRole, "pid"}, {PpidRole, "ppid"}, {NameRole, "name"}, {CommandRole, "command"},
        {UserRole, "user"}, {StateRole, "state"}, {ThreadsRole, "threads"}, {CpuRole, "cpu"},
        {MemoryRole, "memory"}, {MemoryShareRole, "memoryShare"}, {ReadRateRole, "readRate"},
        {WriteRateRole, "writeRate"}, {KernelRole, "kernel"},
    };
}

void ProcessModel::setSortColumn(Column column) {
    if (column == m_column)
        return;
    m_column = column;
    // Names and users read naturally A to Z; numbers are interesting largest first.
    m_descending = !(column == Name || column == User || column == Pid);
    emit sortChanged();
    apply();
}

void ProcessModel::setDescending(bool descending) {
    if (descending == m_descending)
        return;
    m_descending = descending;
    emit sortChanged();
    apply();
}

void ProcessModel::setFilter(const QString &filter) {
    if (filter == m_filter)
        return;
    m_filter = filter;
    emit filterChanged();
    apply();
}

void ProcessModel::setShowKernelThreads(bool show) {
    if (show == m_showKernel)
        return;
    m_showKernel = show;
    emit filterChanged();
    apply();
}

void ProcessModel::sortBy(Column column) {
    if (column == m_column)
        setDescending(!m_descending);
    else
        setSortColumn(column);
}

void ProcessModel::cycleSort() {
    static const QList<Column> order{Cpu, Memory, Read, Write, Pid, Name, User, Threads};
    setSortColumn(order.at((order.indexOf(m_column) + 1) % order.size()));
}

int ProcessModel::rowOf(int pid) const {
    for (int i = 0; i < m_rows.size(); ++i)
        if (m_rows[i].pid == pid)
            return i;
    return -1;
}

int ProcessModel::pidAt(int row) const {
    return row >= 0 && row < m_rows.size() ? m_rows[row].pid : 0;
}

QVariantMap ProcessModel::get(int row) const {
    QVariantMap map;
    if (row < 0 || row >= m_rows.size())
        return map;
    const QHash<int, QByteArray> roles = roleNames();
    for (auto it = roles.begin(); it != roles.end(); ++it)
        map.insert(QString::fromLatin1(it.value()), data(index(row), it.key()));
    return map;
}

void ProcessModel::update(const QList<ProcessInfo> &processes) {
    m_all = processes;
    apply();
}

QList<ProcessInfo> ProcessModel::visible() const {
    QList<ProcessInfo> rows;
    rows.reserve(m_all.size());
    const QString needle = m_filter.trimmed();
    for (const ProcessInfo &p : m_all) {
        if (p.kernel && !m_showKernel)
            continue;
        if (!needle.isEmpty() && !p.name.contains(needle, Qt::CaseInsensitive)
            && !p.command.contains(needle, Qt::CaseInsensitive) && !p.user.contains(needle, Qt::CaseInsensitive)
            && !QString::number(p.pid).startsWith(needle))
            continue;
        rows.append(p);
    }

    auto key = [this](const ProcessInfo &a, const ProcessInfo &b) -> int {
        auto cmp = [](auto x, auto y) { return x < y ? -1 : y < x ? 1 : 0; };
        switch (m_column) {
        case Pid: return cmp(a.pid, b.pid);
        case Name: return a.name.compare(b.name, Qt::CaseInsensitive);
        case User: return a.user.compare(b.user, Qt::CaseInsensitive);
        case Cpu: return cmp(a.cpu, b.cpu);
        case Memory: return cmp(a.memory, b.memory);
        case Read: return cmp(a.readRate, b.readRate);
        case Write: return cmp(a.writeRate, b.writeRate);
        case Threads: return cmp(a.threads, b.threads);
        }
        return 0;
    };
    // Ties fall back to the PID, so equal rows don't shuffle on every sample.
    std::sort(rows.begin(), rows.end(), [&](const ProcessInfo &a, const ProcessInfo &b) {
        const int c = key(a, b);
        if (c != 0)
            return m_descending ? c > 0 : c < 0;
        return a.pid < b.pid;
    });
    return rows;
}

void ProcessModel::apply() {
    const QList<ProcessInfo> next = visible();

    QSet<int> wanted;
    for (const ProcessInfo &p : next)
        wanted.insert(p.pid);

    // Drop rows that are gone, in contiguous runs from the bottom up so the
    // indices still to be removed stay valid.
    for (int end = m_rows.size() - 1; end >= 0;) {
        if (wanted.contains(m_rows[end].pid)) {
            --end;
            continue;
        }
        int start = end;
        while (start > 0 && !wanted.contains(m_rows[start - 1].pid))
            --start;
        beginRemoveRows({}, start, end);
        m_rows.remove(start, end - start + 1);
        endRemoveRows();
        end = start - 1;
    }

    // Append newcomers; the reorder below moves them into place.
    QSet<int> present;
    for (const ProcessInfo &p : m_rows)
        present.insert(p.pid);
    QList<ProcessInfo> added;
    for (const ProcessInfo &p : next)
        if (!present.contains(p.pid))
            added.append(p);
    if (!added.isEmpty()) {
        beginInsertRows({}, m_rows.size(), m_rows.size() + added.size() - 1);
        m_rows.append(added);
        endInsertRows();
    }

    // Same rows, new order and values. A vertical sort hint makes the QML
    // view refresh its delegates in place instead of resetting.
    emit layoutAboutToBeChanged({}, QAbstractItemModel::VerticalSortHint);
    m_rows = next;
    emit layoutChanged({}, QAbstractItemModel::VerticalSortHint);

    // The total behind the filter changes too, so always tell the view.
    emit countChanged();
}
