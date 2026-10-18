#include "widgets.h"
#include "models.h"
#include <QPainter>
#include <QPainterPath>

QIcon lineIcon(const QString &name, const QColor &color)
{
    QPixmap image(48, 48); image.fill(Qt::transparent);
    QPainter p(&image); p.setRenderHint(QPainter::Antialiasing); p.scale(2, 2);
    p.setPen(QPen(color, 1.7, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    if (name == "grid") {
        for (int x : {4, 14}) for (int y : {4, 14}) p.drawRoundedRect(QRectF(x,y,6,6),1.5,1.5);
    } else if (name == "apps") {
        p.drawRoundedRect(QRectF(3,4,18,16),2,2); p.drawLine(3,9,21,9); p.drawPoint(6,6); p.drawLine(9,13,9,17); p.drawLine(13,13,17,13); p.drawLine(13,17,17,17);
    } else if (name == "files") {
        QPainterPath path; path.moveTo(6,3); path.lineTo(14,3); path.lineTo(19,8); path.lineTo(19,21); path.lineTo(6,21); path.closeSubpath();
        p.drawPath(path); p.drawLine(14,3,14,9); p.drawLine(14,9,19,9); p.drawLine(9,13,16,13); p.drawLine(9,17,14,17);
    } else if (name == "disk") {
        p.drawRoundedRect(QRectF(3,5,18,14),2,2); p.drawLine(3,14,21,14); p.drawPoint(17,17); p.drawPoint(14,17);
    } else if (name == "refresh") {
        p.drawArc(QRectF(5,5,14,14),35*16,285*16); p.drawLine(19,4,19,9); p.drawLine(19,9,14,9);
    } else if (name == "search") {
        p.drawEllipse(QRectF(4,3,12,12)); p.drawLine(15,14,21,20);
    } else if (name == "folder") {
        QPainterPath path; path.moveTo(3,8); path.lineTo(3,5); path.lineTo(10,5); path.lineTo(12,8); path.lineTo(21,8); path.lineTo(21,20); path.lineTo(3,20); path.closeSubpath(); p.drawPath(path);
    } else if (name == "trash") {
        p.drawLine(4,6,20,6); p.drawLine(9,3,15,3); p.drawLine(6,6,7,21); p.drawLine(7,21,17,21); p.drawLine(17,21,18,6); p.drawLine(10,10,10,17); p.drawLine(14,10,14,17);
    } else if (name == "copy") {
        p.drawRoundedRect(QRectF(8,8,12,13),1.5,1.5); p.drawLine(4,16,4,3); p.drawLine(4,3,16,3);
    } else if (name == "arrow") {
        p.drawLine(4,12,20,12); p.drawLine(15,7,20,12); p.drawLine(20,12,15,17);
    } else if (name == "stop") {
        p.drawRoundedRect(QRectF(6,6,12,12),2,2);
    }
    return QIcon(image);
}

DiskRing::DiskRing(QWidget *parent) : QWidget(parent) { setMinimumSize(146,146); setMaximumSize(164,164); }
void DiskRing::setSpace(quint64 total, quint64 used, quint64 reserved) { m_total=total; m_used=used; m_reserved=reserved; update(); }
void DiskRing::paintEvent(QPaintEvent *)
{
    QPainter p(this); p.setRenderHint(QPainter::Antialiasing);
    const double side = qMin(width(),height())-24;
    const QRectF ring((width()-side)/2, (height()-side)/2, side, side);
    p.setPen(QPen(QColor("#edf0f8"), 12, Qt::SolidLine, Qt::RoundCap)); p.drawEllipse(ring);
    if (m_total) {
        double fraction = qBound(0.0, double(m_used)/m_total, 1.0);
        if (m_reserved) {
            p.setPen(QPen(QColor("#c8d0e5"),12,Qt::SolidLine,Qt::FlatCap));
            p.drawArc(ring, 90*16, -int((double(m_used+m_reserved)/m_total)*360*16));
        }
        if (fraction > 0) {
            p.setPen(QPen(QColor("#5269eb"),12,Qt::SolidLine,Qt::RoundCap));
            p.drawArc(ring,90*16,-qMax(1,int(fraction*360*16)));
        }
    }
    QFont f = font(); f.setPointSize(24); f.setBold(true); p.setFont(f); p.setPen(QColor("#25324d"));
    p.drawText(rect().adjusted(0,-8,0,-8),Qt::AlignCenter,m_total ? QString::number(qRound(double(m_used)*100/m_total))+"%" : QStringLiteral("—"));
    f.setPointSize(9); f.setBold(false); p.setFont(f); p.setPen(QColor("#8c96aa"));
    p.drawText(rect().adjusted(0,52,0,0),Qt::AlignCenter,QStringLiteral("磁盘已使用"));
}

RankChart::RankChart(QWidget *parent) : QWidget(parent) { setMinimumHeight(316); setSizePolicy(QSizePolicy::Expanding,QSizePolicy::Expanding); }
void RankChart::setItems(QVector<RankItem> items) { m_items=std::move(items); update(); }
void RankChart::paintEvent(QPaintEvent *)
{
    QPainter p(this); p.setRenderHint(QPainter::Antialiasing);
    if (m_items.isEmpty()) { p.setPen(QColor("#8b96ac")); p.drawText(rect(),Qt::AlignCenter,QStringLiteral("正在读取已安装的软件…")); return; }
    const QColor colors[] = {QColor("#536bed"),QColor("#7586ef"),QColor("#96a3f4"),QColor("#40bba5"),QColor("#79cfbf"),QColor("#aadfd5")};
    const double maxBytes = qMax<quint64>(1,m_items.first().bytes);
    const int rowHeight = qMin(58,height()/qMax(1,int(m_items.size())));
    for (int i=0;i<m_items.size() && i<6;++i) {
        int y=i*rowHeight+3;
        QFont f=font(); f.setBold(false); p.setFont(f); p.setPen(QColor("#3c4963"));
        QString label = QFontMetrics(f).elidedText(m_items[i].name,Qt::ElideRight,qMax(40,width()-120));
        p.drawText(QRect(0,y,width()-110,24),Qt::AlignVCenter,label);
        p.setPen(QColor("#6f7b94")); p.drawText(QRect(width()-115,y,115,24),Qt::AlignRight|Qt::AlignVCenter,formatBytes(m_items[i].bytes));
        QRectF bar(0,y+31,width(),7); p.setPen(Qt::NoPen); p.setBrush(QColor("#f0f3f9")); p.drawRoundedRect(bar,3.5,3.5);
        bar.setWidth(qMax(7.0,width()*m_items[i].bytes/maxBytes)); p.setBrush(colors[i]); p.drawRoundedRect(bar,3.5,3.5);
    }
}

QString applicationStyle()
{
    return QStringLiteral(R"(
    QWidget { color: #29364f; font-size: 13px; }
    QMainWindow, QWidget#workspace { background: #f5f7fb; }
    QFrame#sidebar { background: #18243b; border: 0; }
    QLabel#brand { color: #f8faff; font-size: 23px; font-weight: 700; }
    QLabel#brandSub { color: #8492ad; font-size: 12px; }
    QLabel#sideCaption { color: #62738f; font-size: 11px; }
    QPushButton#navButton { text-align: left; background: transparent; color: #9eabc1; border: 0; padding: 13px 16px; border-radius: 9px; font-size: 14px; }
    QPushButton#navButton:hover { background: #22314d; color: white; }
    QPushButton#navButton:checked { background: #2b3c62; color: #ffffff; font-weight: 600; }
    QLabel#pageTitle { font-size: 25px; font-weight: 700; color: #202e48; }
    QLabel#subtitle, QLabel#muted { color: #8993a7; font-size: 12px; }
    QFrame#card { background: white; border: 1px solid #e6eaf2; border-radius: 13px; }
    QLabel#cardTitle { color: #34415c; font-size: 15px; font-weight: 600; }
    QLabel#metricValue { font-size: 29px; font-weight: 700; color: #263551; }
    QLabel#smallValue { font-size: 21px; font-weight: 700; color: #263551; }
    QLabel#tag { background: #edf8f3; border-radius: 5px; color: #29997a; padding: 4px 9px; font-size: 11px; }
    QPushButton { background: white; color: #66748e; border: 1px solid #dde3ef; border-radius: 7px; padding: 9px 15px; font-size: 12px; }
    QPushButton:hover { background: #f4f6ff; border-color: #b5c1e7; color: #435dd0; }
    QPushButton:pressed { background: #e9edfc; }
    QPushButton:disabled { color: #b9c0ce; background: #f7f8fb; border-color: #e9edf4; }
    QPushButton#primary { background: #5268e9; border-color: #5268e9; color: white; font-weight: 600; }
    QPushButton#primary:hover { background: #435bd9; }
    QPushButton#primary:disabled { background: #b2bdf4; border-color: #b2bdf4; }
    QPushButton#danger { color: #cd5263; border-color: #edcbd0; background: #fff8f9; }
    QPushButton#danger:hover { background: #ffecef; }
    QPushButton#danger:disabled { color: #c6b4b7; background: #faf8f8; border-color: #eee6e7; }
    QPushButton#link { border: 0; background: transparent; color: #6175d8; padding: 3px; }
    QLineEdit { background: #ffffff; border: 1px solid #e0e5ef; border-radius: 7px; padding: 9px 12px; selection-background-color: #dfe6ff; }
    QLineEdit:focus { border-color: #7d8dea; }
    QComboBox { background: #fff; border: 1px solid #e0e5ef; border-radius: 7px; padding: 8px 12px; min-height: 18px; }
    QComboBox::drop-down { border: 0; width: 24px; }
    QComboBox QAbstractItemView { background: white; border: 1px solid #dce2ef; selection-background-color: #eaf0ff; color: #35435c; padding: 5px; }
    QTableView { background: #fff; border: 0; outline: 0; gridline-color: #f2f4f8; selection-background-color: #edf2ff; }
    QHeaderView::section { background: #f9fafc; color: #929caf; border: 0; border-bottom: 1px solid #edf0f5; padding: 12px 16px; font-size: 11px; text-align: left; }
    QTableView::item { padding: 8px 16px; }
    QTreeView { background: white; border: 0; outline: 0; selection-background-color: #edf2ff; selection-color: #29364f; }
    QTreeView::item { padding: 7px 5px; }
    QTreeView::item:selected { color: #29364f; background: #edf2ff; }
    QTabWidget::pane { border: 1px solid #e6eaf2; border-radius: 9px; background: white; }
    QTabBar::tab { color: #66748e; background: #f1f4fa; padding: 10px 22px; border: 0; margin-right: 3px; }
    QTabBar::tab:selected { color: #435dd0; background: white; font-weight: 600; }
    QScrollBar:vertical { background: transparent; width: 8px; margin: 2px; }
    QScrollBar::handle:vertical { background: #dce2ed; min-height: 30px; border-radius: 3px; }
    QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0; }
    QScrollBar::add-page:vertical, QScrollBar::sub-page:vertical { background: transparent; }
    QProgressBar { border: 0; background: #e9edf8; border-radius: 2px; max-height: 4px; }
    QProgressBar::chunk { background: #6c7feb; border-radius: 2px; }
    QLabel#notice { background: #fff6e5; color: #a27c35; border-radius: 7px; padding: 9px 12px; font-size: 12px; }
    QTextEdit, QPlainTextEdit { border: 1px solid #e0e5ef; border-radius: 6px; background: #f8f9fd; color: #59677e; padding: 8px; }
    QToolTip { background: #263551; color: white; border: 0; padding: 7px; }
    QMessageBox, QDialog { background: #f7f9fc; }
    )");
}
