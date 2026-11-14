#include "processmodel.h"

#include <algorithm>

ProcessModel::ProcessModel(QObject *parent) : QAbstractListModel(parent) {}

int ProcessModel::rowCount(const QModelIndex &parent) const {
    return parent.isValid() ? 0 : m_rows.size();
}

int ProcessModel::total() const {
    if (m_showKernel)
        return m_all.size();
    return int(std::count_if(m_all.begin(), m_all.end(), [](const ProcessInfo &p) { return !p.kernel; }));
}

int ProcessModel::groupCount() const {
    if (m_showKernel)
        return m_groups.size();
    return int(std::count_if(m_groups.begin(), m_groups.end(), [](const GroupInfo &g) { return g.kind != "kernel"; }));
}

QVariant ProcessModel::data(const QModelIndex &index, int role) const {
    if (!index.isValid() || index.row() >= m_rows.size())
        return {};
    const Row &row = m_rows.at(index.row());
    if (row.index < 0)
        return {};
    switch (role) {
    case KeyRole: return row.key;
    case IsGroupRole: return row.group;
    case DepthRole: return row.depth;
    }

    if (row.group) {
        const GroupInfo &g = m_groups.at(row.index);
        switch (role) {
        case ExpandedRole: return m_expanded.contains(g.key);
        case ProcessCountRole: return int(g.pids.size());
        case KindRole: return g.kind;
        case UnitRole: return g.units.value(0);
        case PidRole: return 0;
        case PpidRole: return 0;
        case Qt::DisplayRole:
        case NameRole: return g.name;
        case CommandRole: return g.units.join(", ");
        case UserRole: return g.user;
        case StateRole: return QString();
        case ThreadsRole: return g.threads;
        case CpuRole: return g.cpu;
        case MemoryRole: return double(g.memory);
        case MemoryShareRole: return g.memoryShare;
        case ReadRateRole: return g.readRate;
        case WriteRateRole: return g.writeRate;
        case KernelRole: return g.kind == "kernel";
        case GroupKeyRole: return g.key;
        case FrozenRole: return g.frozen;
        case CpuLimitRole: return g.cpuLimit;
        case ControlRole: return g.control;
        case GpuRole: return g.gpu;
        case GpuMemoryRole: return double(g.gpuMemory);
        }
        return {};
    }

    const ProcessInfo &p = m_all.at(row.index);
    switch (role) {
    case ExpandedRole: return false;
    case ProcessCountRole: return 1;
    case KindRole: return QString();
    case UnitRole: return p.group;
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
    case GroupKeyRole: return p.group;
    case GpuRole: return p.gpu;
    case GpuMemoryRole: return double(p.gpuMemory);
    }
    // A process is paused or limited with its app.
    const auto group = m_groupIndex.constFind(p.group);
    const GroupInfo *g = group == m_groupIndex.constEnd() ? nullptr : &m_groups.at(*group);
    switch (role) {
    case FrozenRole: return g && g->frozen;
    case CpuLimitRole: return g ? g->cpuLimit : 0.0;
    case ControlRole: return g ? g->control : QString("none");
    }
    return {};
}

QHash<int, QByteArray> ProcessModel::roleNames() const {
    return {
        {KeyRole, "key"}, {IsGroupRole, "isGroup"}, {DepthRole, "depth"}, {ExpandedRole, "expanded"},
        {ProcessCountRole, "processCount"}, {KindRole, "kind"}, {UnitRole, "unit"},
        {PidRole, "pid"}, {PpidRole, "ppid"}, {NameRole, "name"}, {CommandRole, "command"},
        {UserRole, "user"}, {StateRole, "state"}, {ThreadsRole, "threads"}, {CpuRole, "cpu"},
        {MemoryRole, "memory"}, {MemoryShareRole, "memoryShare"}, {ReadRateRole, "readRate"},
        {WriteRateRole, "writeRate"}, {KernelRole, "kernel"}, {GroupKeyRole, "groupKey"},
        {FrozenRole, "frozen"}, {CpuLimitRole, "cpuLimit"}, {ControlRole, "control"},
        {GpuRole, "gpu"}, {GpuMemoryRole, "gpuMemory"},
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

void ProcessModel::setGrouped(bool grouped) {
    if (grouped == m_grouped)
        return;
    m_grouped = grouped;
    emit groupedChanged();
    apply();
}

void ProcessModel::setExpanded(const QString &groupKey, bool expanded) {
    if (expanded == m_expanded.contains(groupKey))
        return;
    if (expanded)
        m_expanded.insert(groupKey);
    else
        m_expanded.remove(groupKey);
    apply();
}

void ProcessModel::sortBy(Column column) {
    if (column == m_column)
        setDescending(!m_descending);
    else
        setSortColumn(column);
}

void ProcessModel::cycleSort() {
    QList<Column> order{Cpu, Memory, Read, Write, Pid, Name, User, Threads};
    if (m_gpuAvailable)
        order.insert(1, Gpu);
    setSortColumn(order.at((order.indexOf(m_column) + 1) % order.size()));
}

int ProcessModel::rowOf(const QString &key) const {
    for (int i = 0; i < m_rows.size(); ++i)
        if (m_rows[i].key == key)
            return i;
    return -1;
}

QString ProcessModel::keyAt(int row) const {
    return row >= 0 && row < m_rows.size() ? m_rows[row].key : QString();
}

QString ProcessModel::parentKey(const QString &key) const {
    if (!m_grouped || !key.startsWith("p:"))
        return {};
    const auto it = m_byPid.constFind(key.mid(2).toInt());
    return it == m_byPid.constEnd() ? QString() : "g:" + m_all[*it].group;
}

QVariantMap ProcessModel::get(int row) const {
    QVariantMap map;
    if (row < 0 || row >= m_rows.size() || m_rows[row].index < 0)
        return map;
    const QHash<int, QByteArray> roles = roleNames();
    for (auto it = roles.begin(); it != roles.end(); ++it)
        map.insert(QString::fromLatin1(it.value()), data(index(row), it.key()));
    if (m_rows[row].group)
        map.insert("pids", QVariant::fromValue(m_groups[m_rows[row].index].pids));
    return map;
}

void ProcessModel::update(const QList<ProcessInfo> &processes, const QList<GroupInfo> &groups) {
    m_all = processes;
    m_groups = groups;
    m_byPid.clear();
    for (int i = 0; i < m_all.size(); ++i)
        m_byPid.insert(m_all[i].pid, i);
    m_groupIndex.clear();
    for (int i = 0; i < m_groups.size(); ++i)
        m_groupIndex.insert(m_groups[i].key, i);
    apply();
}

void ProcessModel::refresh(const QList<GroupInfo> &groups) {
    if (groups.size() != m_groups.size())
        return;
    m_groups = groups;
    if (!m_rows.isEmpty())
        emit dataChanged(index(0), index(int(m_rows.size()) - 1), {FrozenRole, CpuLimitRole, ControlRole});
}

namespace {
template <typename T> int compare(const T &a, const T &b) { return a < b ? -1 : b < a ? 1 : 0; }

// Ties fall back to a stable key, so equal rows don't shuffle every sample.
template <typename Key> bool ordered(int c, bool descending, const Key &a, const Key &b) {
    if (c != 0)
        return descending ? c > 0 : c < 0;
    return a < b;
}
} // namespace

bool ProcessModel::lessThan(const ProcessInfo &a, const ProcessInfo &b, Column column, bool descending) {
    int c = 0;
    switch (column) {
    case Pid: c = compare(a.pid, b.pid); break;
    case Name: c = a.name.compare(b.name, Qt::CaseInsensitive); break;
    case User: c = a.user.compare(b.user, Qt::CaseInsensitive); break;
    case Cpu: c = compare(a.cpu, b.cpu); break;
    case Memory: c = compare(a.memory, b.memory); break;
    case Read: c = compare(a.readRate, b.readRate); break;
    case Write: c = compare(a.writeRate, b.writeRate); break;
    case Threads: c = compare(a.threads, b.threads); break;
    case Gpu: c = compare(a.gpu, b.gpu); break;
    }
    return ordered(c, descending, a.pid, b.pid);
}

bool ProcessModel::lessThan(const GroupInfo &a, const GroupInfo &b, Column column, bool descending) {
    int c = 0;
    switch (column) {
    case Pid: c = compare(a.pids.size(), b.pids.size()); break;
    case Name: c = a.name.compare(b.name, Qt::CaseInsensitive); break;
    case User: c = a.user.compare(b.user, Qt::CaseInsensitive); break;
    case Cpu: c = compare(a.cpu, b.cpu); break;
    case Memory: c = compare(a.memory, b.memory); break;
    case Read: c = compare(a.readRate, b.readRate); break;
    case Write: c = compare(a.writeRate, b.writeRate); break;
    case Threads: c = compare(a.threads, b.threads); break;
    case Gpu: c = compare(a.gpu, b.gpu); break;
    }
    return ordered(c, descending, a.key, b.key);
}

bool ProcessModel::matches(const ProcessInfo &p) const {
    const QString needle = m_filter.trimmed();
    return needle.isEmpty() || p.name.contains(needle, Qt::CaseInsensitive)
           || p.command.contains(needle, Qt::CaseInsensitive) || p.user.contains(needle, Qt::CaseInsensitive)
           || QString::number(p.pid).startsWith(needle);
}

bool ProcessModel::matches(const GroupInfo &g) const {
    const QString needle = m_filter.trimmed();
    return needle.isEmpty() || g.name.contains(needle, Qt::CaseInsensitive)
           || g.units.join(' ').contains(needle, Qt::CaseInsensitive);
}

QList<ProcessModel::Row> ProcessModel::visible() const {
    QList<Row> rows;
    const bool filtering = !m_filter.trimmed().isEmpty();
    auto byColumn = [this](const ProcessInfo &a, const ProcessInfo &b) {
        return lessThan(a, b, m_column, m_descending);
    };

    if (!m_grouped) {
        QList<int> order;
        for (int i = 0; i < m_all.size(); ++i)
            if ((m_showKernel || !m_all[i].kernel) && matches(m_all[i]))
                order.append(i);
        std::sort(order.begin(), order.end(), [&](int a, int b) { return byColumn(m_all[a], m_all[b]); });
        for (int i : order)
            rows.append({"p:" + QString::number(m_all[i].pid), false, i, 0});
        return rows;
    }

    QList<int> groups;
    QHash<int, QList<int>> members; // Group index to the processes to list under it.
    for (int g = 0; g < m_groups.size(); ++g) {
        const GroupInfo &group = m_groups[g];
        if (group.kind == "kernel" && !m_showKernel)
            continue;
        // A group shows when its name matches, with all of its processes,
        // or when some of its processes do, with just those.
        const bool named = matches(group);
        QList<int> listed;
        for (int pid : group.pids) {
            const auto it = m_byPid.constFind(pid);
            if (it != m_byPid.constEnd() && (named || matches(m_all[*it])))
                listed.append(*it);
        }
        if (!named && listed.isEmpty())
            continue;
        groups.append(g);
        members.insert(g, listed);
    }
    std::sort(groups.begin(), groups.end(), [&](int a, int b) {
        return lessThan(m_groups[a], m_groups[b], m_column, m_descending);
    });
    for (int g : groups) {
        const GroupInfo &group = m_groups[g];
        rows.append({"g:" + group.key, true, g, 0});
        // While filtering, matching processes show without being asked for.
        if (!m_expanded.contains(group.key) && !(filtering && !matches(group)))
            continue;
        QList<int> listed = members.value(g);
        std::sort(listed.begin(), listed.end(), [&](int a, int b) { return byColumn(m_all[a], m_all[b]); });
        for (int i : listed)
            rows.append({"p:" + QString::number(m_all[i].pid), false, i, 1});
    }
    return rows;
}

void ProcessModel::apply() {
    const QList<Row> next = visible();

    QHash<QString, int> wanted;
    for (int i = 0; i < next.size(); ++i)
        wanted.insert(next[i].key, i);

    // The data behind the old rows has been replaced already, so point them
    // at the new data before the view gets a chance to ask about them.
    for (Row &row : m_rows) {
        const auto it = wanted.constFind(row.key);
        row.index = it == wanted.constEnd() ? -1 : next[*it].index;
    }

    // Drop rows that are gone, in contiguous runs from the bottom up so the
    // indices still to be removed stay valid.
    for (int end = m_rows.size() - 1; end >= 0;) {
        if (wanted.contains(m_rows[end].key)) {
            --end;
            continue;
        }
        int start = end;
        while (start > 0 && !wanted.contains(m_rows[start - 1].key))
            --start;
        beginRemoveRows({}, start, end);
        m_rows.remove(start, end - start + 1);
        endRemoveRows();
        end = start - 1;
    }

    // Append newcomers; the reorder below moves them into place.
    QSet<QString> present;
    for (const Row &row : m_rows)
        present.insert(row.key);
    QList<Row> added;
    for (const Row &row : next)
        if (!present.contains(row.key))
            added.append(row);
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

    // The totals behind the filter change too, so always tell the view.
    emit countChanged();
}
