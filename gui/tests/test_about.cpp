#include "aboutdialog.h"
#include <QClipboard>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QTabWidget>
#include <QTextBrowser>
#include <QtTest>

class AboutTests final : public QObject {
    Q_OBJECT
private slots:
    void embeddedNotes_data() {
        QTest::addColumn<bool>("turkish");
        QTest::newRow("English") << false;
        QTest::newRow("Turkish") << true;
    }
    void embeddedNotes() {
        QFETCH(bool, turkish);
        QCoreApplication::setApplicationVersion(QStringLiteral(MSI_MUX_VERSION));
        Mux::AboutDialog dialog(turkish ? Mux::Language::Turkish : Mux::Language::English, Mux::Status::demo(), nullptr, true);
        auto *tabs = dialog.findChild<QTabWidget *>(QStringLiteral("aboutTabs"));
        QVERIFY(tabs);
        QCOMPARE(tabs->count(), 3);
        QCOMPARE(tabs->currentIndex(), 1);
        auto *notes = dialog.findChild<QTextBrowser *>(QStringLiteral("releaseNotes"));
        QVERIFY(notes);
        QVERIFY(notes->toPlainText().contains(QStringLiteral("0.4.0")));
        QVERIFY(notes->toPlainText().contains(QStringLiteral("0.3.0")));
        QVERIFY(!notes->openLinks());
        QVERIFY(!notes->openExternalLinks());
        QVERIFY(notes->toPlainText().contains(turkish ? QStringLiteral("Sürüm notları") : QStringLiteral("Changelog")));
    }
    void summaryIsPlainTextAndCopyIsExplicit() {
        const QString before = QStringLiteral("unchanged until the user clicks Copy");
        QApplication::clipboard()->setText(before);
        auto status = Mux::Status::demo();
        status.model = QStringLiteral("<b>synthetic model</b>");
        Mux::AboutDialog dialog(Mux::Language::Turkish, status);
        QCOMPARE(QApplication::clipboard()->text(), before);
        auto *snapshot = dialog.findChild<QPlainTextEdit *>(QStringLiteral("diagnosticsSnapshot"));
        QVERIFY(snapshot && snapshot->isReadOnly());
        QVERIFY(snapshot->toPlainText().contains(status.model));
        QVERIFY(!snapshot->toPlainText().contains(QStringLiteral("byte5")));
        QVERIFY(!snapshot->toPlainText().contains(QStringLiteral("MsiDCVarData")));
        QPushButton *copy = nullptr;
        for (auto *button : dialog.findChildren<QPushButton *>())
            if (button->text() == QStringLiteral("Sistem özetini kopyala")) copy = button;
        QVERIFY(copy);
        copy->click();
        QCOMPARE(QApplication::clipboard()->text(), snapshot->toPlainText());
    }
    void diagnosticsKeepCurrentAndRequestedSeparate() {
        auto status = Mux::Status::demo();
        status.current = Mux::Mode::Discrete;
        status.target = Mux::Mode::Hybrid;
        status.pendingShutdown = true;
        const auto text = Mux::AboutDialog::diagnosticsText(Mux::Language::English, status);
        QVERIFY(text.contains(QStringLiteral("Current: Discrete")));
        QVERIFY(text.contains(QStringLiteral("Requested: Hybrid")));
        QVERIFY(text.contains(QStringLiteral("Power cycle pending: yes")));
    }
};

QTEST_MAIN(AboutTests)
#include "test_about.moc"
