#pragma once

#include <QAbstractListModel>
#include <QList>
#include <QString>

struct ProcessInfo {
    int pid = 0;
    int ppid = 0;
    QString name;
    QString command;
    QString user;
    char state = '?';
    int threads = 0;
    double cpu = 0;        // Percent of one core, like htop and top.
    quint64 memory = 0;    // Resident bytes.
    double memoryShare = 0; // Fraction of physical memory.
    // Bytes per second, or -1 when /proc/<pid>/io isn't ours to read.
    double readRate = -1;
    double writeRate = -1;
    bool kernel = false;
};

// The process table, filtered and sorted in C++ so the list in QML stays a
// plain view. Updates keep the rows the view already has and report a sort
// as a layout change, so the list keeps its scroll position and selection
// while the numbers move under it.
class ProcessModel : public QAbstractListModel {
    Q_OBJECT
    Q_PROPERTY(int count READ rowCount NOTIFY countChanged)
    Q_PROPERTY(int total READ total NOTIFY countChanged)
    Q_PROPERTY(Column sortColumn READ sortColumn WRITE setSortColumn NOTIFY sortChanged)
    Q_PROPERTY(bool descending READ descending WRITE setDescending NOTIFY sortChanged)
    Q_PROPERTY(QString filter READ filter WRITE setFilter NOTIFY filterChanged)
    Q_PROPERTY(bool showKernelThreads READ showKernelThreads WRITE setShowKernelThreads NOTIFY filterChanged)

public:
    enum Column { Pid, Name, User, Cpu, Memory, Read, Write, Threads };
    Q_ENUM(Column)

    enum Role {
        PidRole = Qt::UserRole + 1,
        PpidRole,
        NameRole,
        CommandRole,
        UserRole,
        StateRole,
        ThreadsRole,
        CpuRole,
        MemoryRole,
        MemoryShareRole,
        ReadRateRole,
        WriteRateRole,
        KernelRole,
    };

    explicit ProcessModel(QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    int total() const { return m_all.size(); }
    Column sortColumn() const { return m_column; }
    bool descending() const { return m_descending; }
    QString filter() const { return m_filter; }
    bool showKernelThreads() const { return m_showKernel; }

    void setSortColumn(Column column);
    void setDescending(bool descending);
    void setFilter(const QString &filter);
    void setShowKernelThreads(bool show);

    // Clicking a header sorts by it; clicking the same header again flips it.
    Q_INVOKABLE void sortBy(Column column);
    Q_INVOKABLE void cycleSort();
    Q_INVOKABLE int rowOf(int pid) const;
    Q_INVOKABLE int pidAt(int row) const;
    Q_INVOKABLE QVariantMap get(int row) const;

    void update(const QList<ProcessInfo> &processes);

signals:
    void countChanged();
    void sortChanged();
    void filterChanged();

private:
    QList<ProcessInfo> visible() const;
    void apply();

    QList<ProcessInfo> m_all;
    QList<ProcessInfo> m_rows;
    Column m_column = Cpu;
    bool m_descending = true;
    QString m_filter;
    bool m_showKernel = false;
};
